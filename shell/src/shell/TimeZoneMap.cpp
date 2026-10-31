#include "shell/TimeZoneMap.h"

#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/Alarms.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cairo.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <librsvg/rsvg.h>
#include <linux/input-event-codes.h>
#include <numbers>
#include <set>

namespace m3::shell {

using namespace m3::ui;

namespace {

// The map file's latitudes (data/maps/world.svg): the inhabited world,
// Greenland's north coast to Cape Horn
constexpr double kNorth = 84, kSouth = -58;

constexpr double rad(double deg) { return deg * std::numbers::pi / 180; }

// The zone /etc/localtime points into
std::string currentZone() {
    std::error_code ec;
    const std::string target = std::filesystem::read_symlink("/etc/localtime", ec).string();
    if (const auto at = target.find("zoneinfo/"); at != std::string::npos) return target.substr(at + 9);
    return "UTC";
}

// A zone's offset from UTC right now (daylight saving included), in minutes
std::optional<int> offsetMinutes(const std::string& id) {
    try {
        const auto info = std::chrono::locate_zone(id)->get_info(std::chrono::system_clock::now());
        return static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(info.offset).count());
    } catch (...) {
        return std::nullopt;
    }
}

std::string offsetText(std::optional<int> m) {
    if (!m) return {};
    if (*m == 0) return "UTC";
    const int a = std::abs(*m);
    return std::format("UTC{}{:02}:{:02}", *m < 0 ? '-' : '+', a / 60, a % 60);
}

// The time there now, as the shell's clock writes it
std::string timeThere(const std::string& id) {
    try {
        const std::chrono::zoned_time now{id, std::chrono::system_clock::now()};
        const auto local = now.get_local_time();
        const std::chrono::hh_mm_ss hms{std::chrono::floor<std::chrono::minutes>(local - std::chrono::floor<std::chrono::days>(local))};
        return Alarms::timeText(static_cast<int>(hms.hours().count()), static_cast<int>(hms.minutes().count()));
    } catch (...) {
        return {};
    }
}

// "Kolkata, India", or the id for a zone without a place (Etc/GMT+3)
std::string placeOf(const std::string& id) {
    const auto& zones = tz::system();
    const int at = tz::find(zones, id);
    if (at < 0) return id;
    const auto& z = zones[static_cast<size_t>(at)];
    return z.country.empty() ? z.city : z.city + ", " + z.country;
}

// One line: "Kolkata, India  ·  UTC+05:30  ·  18:32"
std::string summary(const std::string& id) {
    std::string text = placeOf(id);
    for (const auto& part : {offsetText(offsetMinutes(id)), timeThere(id)})
        if (!part.empty()) text += "  ·  " + part;
    return text;
}

// A zone in detail: "Asia/Tokyo  ·  JST, UTC+09:00  ·  22:00", and its next
// change of clocks ("Clocks go back an hour on Sunday 1 November")
std::pair<std::string, std::string> details(const std::string& id) {
    using namespace std::chrono;
    try {
        const auto* zone = locate_zone(id);
        // Whole seconds: a zone that never changes ends at the end of time,
        // which finer units overflow
        const auto now = floor<seconds>(system_clock::now());
        const auto info = zone->get_info(now);
        // Letters only: a numeric abbreviation ("+04") repeats the offset
        const bool named = !info.abbrev.empty() && info.abbrev[0] != '+' && info.abbrev[0] != '-';
        const auto offset = static_cast<int>(duration_cast<minutes>(info.offset).count());
        std::string line = id + "  ·  " + (named ? info.abbrev + ", " : "") + offsetText(offset) + "  ·  " + timeThere(id);
        std::string change = "No daylight saving time";
        if (info.end - now < days(400)) {
            const auto next = zone->get_info(info.end);
            const int by = static_cast<int>(duration_cast<minutes>(next.offset - info.offset).count());
            if (by != 0) {
                const auto local = zoned_time{zone, info.end}.get_local_time();
                const year_month_day date{floor<days>(local)};
                const std::string amount = std::abs(by) == 60 ? "an hour"
                                           : std::abs(by) == 30 ? "half an hour"
                                           : std::abs(by) % 60 == 0 ? std::format("{} hours", std::abs(by) / 60)
                                                                    : std::format("{} minutes", std::abs(by));
                change = std::format("{}Clocks go {} {} on {:%A} {} {:%B}", info.save != minutes(0) ? "Daylight saving time now. " : "",
                                     by > 0 ? "forward" : "back", amount, weekday{floor<days>(local)},
                                     static_cast<unsigned>(date.day()), date.month());
            }
        }
        return {line, change};
    } catch (...) {
        return {id, {}};
    }
}

// ── The map's pictures ──

struct Texture {
    GLuint id = 0;
    int w = 0, h = 0;
};

void upload(Texture& t, int w, int h, const unsigned char* pixels) {
    if (!t.id) {
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    t.w = w;
    t.h = h;
}

// One layer of the map file (#land, #lakes, #borders), drawn once per size
// into a mask the map tints with the theme
GLuint layer(Texture& t, const char* id, int w, int h) {
    static RsvgHandle* svg = [] {
        const std::string path = paths::dataFile("maps/world.svg");
        return path.empty() ? nullptr : rsvg_handle_new_from_file(path.c_str(), nullptr);
    }();
    if (!svg || w <= 0 || h <= 0) return 0;
    if (t.id && t.w == w && t.h == h) return t.id;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    const RsvgRectangle viewport{0, 0, static_cast<double>(w), static_cast<double>(h)};
    rsvg_handle_render_layer(svg, cr, id, &viewport, nullptr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    upload(t, w, h, cairo_image_surface_get_data(surface));
    cairo_surface_destroy(surface);
    return t.id;
}

// Where the sun is overhead at `t` (the Astronomical Almanac's low-precision
// formulas, good to a fraction of a degree)
void subsolar(std::time_t t, double& lat, double& lon) {
    const double n = static_cast<double>(t) / 86400.0 - 10957.5; // days since 2000-01-01 12:00 UTC
    const double g = rad(std::fmod(357.529 + 0.98560028 * n, 360));
    const double q = std::fmod(280.459 + 0.98564736 * n, 360);
    const double ecliptic = rad(q + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g));
    const double tilt = rad(23.439 - 0.00000036 * n);
    const double ra = std::atan2(std::cos(tilt) * std::sin(ecliptic), std::cos(ecliptic)) * 180 / std::numbers::pi;
    const double gmst = std::fmod(18.697374558 + 24.06570982441908 * n, 24.0) * 15;
    lat = std::asin(std::sin(tilt) * std::sin(ecliptic)) * 180 / std::numbers::pi;
    lon = std::remainder(ra - gmst, 360.0);
}

// The night side: clear where the sun is up, darkening through civil
// twilight (the sun 6° under the horizon) to full night. A degree a pixel;
// drawn again each minute
GLuint night() {
    static Texture t;
    static std::time_t made = -1;
    const std::time_t now = std::time(nullptr);
    if (t.id && now / 60 == made / 60) return t.id;
    constexpr int W = 360, H = static_cast<int>(kNorth - kSouth);
    double sunLat = 0, sunLon = 0;
    subsolar(now, sunLat, sunLon);
    const double sd = std::sin(rad(sunLat)), cd = std::cos(rad(sunLat)), twilight = std::sin(rad(6));
    std::vector<unsigned char> px(static_cast<size_t>(W * H * 4), 255);
    for (int y = 0; y < H; ++y) {
        const double lat = rad(kNorth - (y + 0.5) * (kNorth - kSouth) / H);
        for (int x = 0; x < W; ++x) {
            const double lon = rad(-180 + (x + 0.5) * 360.0 / W);
            const double altitude = std::sin(lat) * sd + std::cos(lat) * cd * std::cos(lon - rad(sunLon));
            px[static_cast<size_t>((y * W + x) * 4 + 3)] = static_cast<unsigned char>(std::clamp(-altitude / twilight, 0.0, 1.0) * 255);
        }
    }
    upload(t, W, H, px.data());
    made = now;
    return t.id;
}

// A label on a pill, laid on the map by its owner
Flex* chip(Node* map, const char* background, const char* text, Label*& label) {
    auto* c = map->add<Flex>(Flex::Direction::Row);
    pill(c->setBackground(background, false))->setPadding({10, 4});
    label = c->add<Label>("");
    label->setSize(12)->setWeight(600)->setColor(text, false)->setMaxWidth(260);
    c->setVisible(false);
    return c;
}

// The map's place in the popup, at the map's size whatever is shown in it:
// the map, or while searching the matches
class MapStack : public Node {
protected:
    SizeF doMeasure(SizeF available) override {
        const float w = std::max(available.w, 200.0f);
        return {w, w * static_cast<float>((kNorth - kSouth) / 360)};
    }
    void doArrange() override {
        for (auto& c : children()) c->arrange(rect());
    }
};

std::string lower(std::string t) {
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t;
}

} // namespace

Signal<>& timeZoneChanged() {
    static Signal<> signal;
    return signal;
}

// ── The map ──

TimeZoneMap::TimeZoneMap(const std::vector<tz::Zone>& zones) {
    for (const auto& z : zones)
        if (z.lat <= kNorth && z.lat >= kSouth) {
            m_zones.push_back(z);
            m_offsets.push_back(offsetMinutes(z.id));
        }
    m_pinChip = chip(this, "primary", "on_primary", m_pinLabel);
    m_hoverChip = chip(this, "inverse_surface", "inverse_on_surface", m_hoverLabel);
}

float TimeZoneMap::toX(double lon) const { return rect().x + static_cast<float>((lon + 180) / 360) * rect().w; }
float TimeZoneMap::toY(double lat) const { return rect().y + static_cast<float>((kNorth - lat) / (kNorth - kSouth)) * rect().h; }

int TimeZoneMap::placeAt(float x, float y) const {
    const auto& b = rect();
    if (b.w <= 0 || b.h <= 0) return -1;
    const double lon = (x - b.x) / b.w * 360 - 180, lat = kNorth - (y - b.y) / b.h * (kNorth - kSouth);
    return tz::nearest(m_zones, lat, lon);
}

void TimeZoneMap::select(const std::string& id) {
    m_selected = tz::find(m_zones, id);
    m_offsetMinutes = offsetMinutes(id);
    if (m_selected >= 0) m_pinLabel->setText(m_zones[static_cast<size_t>(m_selected)].city);
    m_pinChip->setVisible(m_selected >= 0);
    if (m_hover == m_selected) m_hoverChip->setVisible(false);
    requestLayout();
}

void TimeZoneMap::hoverOn(int index) {
    if (index == m_hover) return;
    m_hover = index;
    const bool show = index >= 0 && index != m_selected;
    if (show) {
        const auto& z = m_zones[static_cast<size_t>(index)];
        std::string text = z.country.empty() ? z.city : z.city + ", " + z.country;
        if (const auto at = timeThere(z.id); !at.empty()) text += "  ·  " + at;
        m_hoverLabel->setText(text);
    }
    m_hoverChip->setVisible(show);
    requestLayout();
}

void TimeZoneMap::onHover(bool on) {
    if (!on) hoverOn(-1);
}

void TimeZoneMap::onPointerMove(float x, float y) {
    if (rect().contains(x, y)) hoverOn(placeAt(x, y));
}

void TimeZoneMap::onPress(uint32_t button, float x, float y) {
    if (button != BTN_LEFT) return;
    const int at = placeAt(x, y);
    if (at < 0) return;
    const tz::Zone zone = m_zones[static_cast<size_t>(at)];
    select(zone.id);
    if (onPicked) onPicked(zone);
}

SizeF TimeZoneMap::doMeasure(SizeF available) {
    const float w = std::max(available.w, 200.0f);
    return {w, w * static_cast<float>((kNorth - kSouth) / 360)};
}

void TimeZoneMap::doArrange() {
    const RectF b = rect();
    // Each name over its place, or under it at the top edge (or where the
    // pin's name already is), kept on the map
    RectF pinned{};
    auto place = [&](Flex* c, int index) {
        if (index < 0 || !c->visible()) return;
        const auto& z = m_zones[static_cast<size_t>(index)];
        const SizeF s = c->measure({b.w, b.h});
        const float x = toX(z.lon), y = toY(z.lat);
        const float cx = std::clamp(x - s.w / 2, b.x + 6, std::max(b.x + 6, b.right() - s.w - 6));
        RectF at{cx, y - 14 - s.h, s.w, s.h};
        const auto overlaps = [&](const RectF& a) {
            return a.w > 0 && at.x < a.right() + 4 && a.x < at.right() + 4 && at.y < a.bottom() + 16 && a.y < at.bottom() + 4;
        };
        if (at.y < b.y + 6 || overlaps(pinned)) at.y = y + 14;
        c->arrange(at);
        if (c == m_pinChip) pinned = at;
    };
    place(m_pinChip, m_selected);
    place(m_hoverChip, m_hover);
}

void TimeZoneMap::doPaint(Renderer& r) {
    static Texture land, lakes, borders;
    const auto& theme = Theme::get();
    const RectF b = rect();
    const Color sea = theme.color("surface_container_highest"), primary = theme.color("primary");
    r.rect(b, sea, Radii(20));
    const int w = static_cast<int>(std::lround(b.w * r.scale())), h = static_cast<int>(std::lround(b.h * r.scale()));
    if (const GLuint t = layer(land, "#land", w, h)) r.mask(t, b, theme.color("outline").withAlpha(0.55f));
    if (const GLuint t = layer(lakes, "#lakes", w, h)) r.mask(t, b, sea);
    if (const GLuint t = layer(borders, "#borders", w, h)) r.mask(t, b, sea.withAlpha(0.8f));
    r.mask(night(), b, Color{0, 0, 0, 0.32f});
    // The chosen zone's hours: a column 15° of longitude wide (an hour),
    // where UTC's is at Greenwich
    if (m_offsetMinutes) {
        const double centre = *m_offsetMinutes / 4.0;
        for (const double turn : {-360.0, 0.0, 360.0}) {
            const float x0 = std::max(b.x, toX(centre - 7.5 + turn)), x1 = std::min(b.right(), toX(centre + 7.5 + turn));
            if (x1 - x0 > 1) r.rect({x0, b.y + 6, x1 - x0, b.h - 12}, primary.withAlpha(0.2f), Radii(std::min(12.0f, (x1 - x0) / 2)));
        }
    }
    // Every place: those on the chosen zone's time now lit
    const Color dot = theme.color("on_surface").withAlpha(0.45f);
    for (size_t i = 0; i < m_zones.size(); ++i) {
        const float x = toX(m_zones[i].lon), y = toY(m_zones[i].lat);
        if (m_offsetMinutes && m_offsets[i] == m_offsetMinutes) r.rect({x - 2.5f, y - 2.5f, 5, 5}, primary, Radii(2.5f));
        else r.rect({x - 1.5f, y - 1.5f, 3, 3}, dot, Radii(1.5f));
    }
    if (m_hover >= 0 && m_hover != m_selected) {
        const auto& z = m_zones[static_cast<size_t>(m_hover)];
        const float x = toX(z.lon), y = toY(z.lat);
        r.rect({x - 7, y - 7, 14, 14}, Color{0, 0, 0, 0}, Radii(7), 2, theme.color("on_surface"));
    }
    if (m_selected >= 0) {
        const auto& z = m_zones[static_cast<size_t>(m_selected)];
        const float x = toX(z.lon), y = toY(z.lat);
        r.rect({x - 13, y - 13, 26, 26}, primary.withAlpha(0.25f), Radii(13));
        r.rect({x - 7, y - 7, 14, 14}, primary, Radii(7), 2.5f, sea);
    }
}

// ── Settings → System ──

void addTimeZoneRow(Flex* group) {
    auto* live = group->add<kit::Live>();
    auto r = panels::row(live, "public", "Time zone");
    panels::pillButton(r.row, "Change", false, [] {
        if (panels::open) panels::open("timezone");
    }, "map");
    live->sync = [subtitle = r.subtitle] {
        subtitle->setText(summary(currentZone()));
        subtitle->setVisible(true);
    };
    live->watch(timeZoneChanged());
    live->sync();
}

// ── The popup ──

void timeZoneDialog(Flex* card, Popup& popup) {
    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    head->add<Icon>("public", 26)->setColor("primary", false);
    auto* title = head->add<Label>("Time zone");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    kit::iconButton(head, "close", [&popup] { popup.close(); });

    auto* search = card->add<TextField>();
    search->setPlaceholder("Search for a city, country or zone");
    popup.initialFocus = search;

    // The map, or while searching the matches in its place
    auto* stack = card->add<MapStack>();
    auto* map = stack->add<TimeZoneMap>(tz::system());
    const auto alive = map->alive();
    auto* scroll = stack->add<ScrollView>();
    scroll->setVisible(false);
    auto* results = scroll->add<Flex>(Flex::Direction::Column);
    results->setSpacing(2)->setAlignItems(Align::Stretch);

    // The chosen zone in detail, and the buttons
    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(12)->setAlignItems(Align::Center)->setPadding({4, 4, 0, 0});
    auto* texts = foot->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(2);
    texts->grow = true;
    auto* place = texts->add<Label>("");
    place->setSize(18)->setWeight(600)->setMaxWidth(560);
    auto* line = texts->add<Label>("");
    line->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(560);
    auto* change = texts->add<Label>("");
    change->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(560);
    auto* status = texts->add<Label>("");
    status->setSize(13)->setColor("error", false)->setMaxWidth(560);
    status->setVisible(false);

    auto chosen = std::make_shared<std::string>(currentZone());
    auto show = [=](const std::string& id) {
        *chosen = id;
        map->select(id);
        place->setText(placeOf(id));
        const auto [l, c] = details(id);
        line->setText(l);
        change->setText(c);
        change->setVisible(!c.empty());
        status->setVisible(false);
    };
    show(*chosen);
    map->onPicked = [show](const tz::Zone& z) { show(z.id); };

    auto set = [=, &popup] {
        if (*chosen == currentZone()) return popup.close();
        status->setText("Setting the time zone…");
        status->setColor("on_surface_variant", false);
        status->setVisible(true);
        // The password prompt, when one comes, takes this popup's place;
        // Settings hears either way and reads the zone again
        m3::run({"timedatectl", "set-timezone", *chosen}, [alive, status, &popup](int code, const std::string&) {
            timeZoneChanged().emit();
            if (!*alive) return;
            if (code == 0) return popup.close();
            status->setText("Not changed: the password was refused, or the prompt closed");
            status->setColor("error", false);
        });
    };
    panels::pillButton(foot, "Cancel", false, [&popup] { popup.close(); });
    panels::pillButton(foot, "Set time zone", true, set, "check");

    // tzdata's other names (UTC, Etc/GMT+3, old names), found by search alone
    auto others = std::make_shared<std::vector<std::string>>();
    m3::run({"timedatectl", "list-timezones"}, [alive, others](int, const std::string& out) {
        if (!*alive) return;
        std::set<std::string> placed;
        for (const auto& z : tz::system()) placed.insert(z.id);
        size_t start = 0;
        while (start < out.size()) {
            const size_t end = std::min(out.find('\n', start), out.size());
            if (std::string id = out.substr(start, end - start); !id.empty() && !placed.contains(id)) others->push_back(std::move(id));
            start = end + 1;
        }
    });

    // Matches: places whose name starts with the words, then whose country
    // does, then any place or country with them, then zone ids and notes
    // (New South Wales), then tzdata's other names
    auto first = std::make_shared<std::string>();
    auto fill = std::make_shared<std::function<void()>>();
    // Again on the next turn of the loop (never from inside a result's own
    // click: that result goes), and only while the popup is open
    auto refill = [alive, weak = std::weak_ptr(fill)] {
        EventLoop::get().defer([alive, weak] {
            if (auto f = weak.lock(); f && *alive) (*f)();
        });
    };
    *fill = [=] {
        const auto& zones = tz::system();
        results->clearChildren();
        first->clear();
        std::string q = lower(search->text());
        q.erase(0, q.find_first_not_of(' '));
        q.erase(q.find_last_not_of(' ') + 1);
        map->setVisible(q.empty());
        scroll->setVisible(!q.empty());
        if (q.empty()) return;
        std::vector<std::string> picked;
        auto add = [&](const std::string& id, const std::string& title, const std::string& sub) {
            if (picked.size() >= 30 || std::ranges::find(picked, id) != picked.end()) return;
            picked.push_back(id);
            if (first->empty()) *first = id;
            auto* item = results->add<Button>(Flex::Direction::Row);
            pill(item)->setPadding({16, 10})->setSpacing(14);
            if (id == *chosen) item->setBackground("secondary_container", false);
            item->add<Icon>("location_on", 20)->setColor("on_surface_variant", false);
            auto* texts = item->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(title)->setSize(15)->setWeight(500)->setMaxWidth(560);
            if (!sub.empty()) texts->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(560);
            item->add<Label>(offsetText(offsetMinutes(id)) + "  ·  " + timeThere(id))->setSize(13)->setColor("on_surface_variant", false);
            item->onClicked = [=](uint32_t button) {
                if (button != BTN_LEFT) return;
                show(id);
                search->setText("");
                refill();
            };
        };
        auto placeName = [](const tz::Zone& z) { return z.country.empty() ? z.city : z.city + ", " + z.country; };
        auto detail = [](const tz::Zone& z) { return z.note.empty() ? z.id : z.id + "  ·  " + z.note; };
        const std::vector<std::function<bool(const tz::Zone&)>> passes = {
            [&](const tz::Zone& z) { return lower(z.city).starts_with(q); },
            [&](const tz::Zone& z) { return lower(z.country).starts_with(q); },
            [&](const tz::Zone& z) { return lower(z.city + " " + z.country).find(q) != std::string::npos; },
            [&](const tz::Zone& z) { return lower(z.id + " " + z.note).find(q) != std::string::npos; },
        };
        for (const auto& matches : passes)
            for (const auto& z : zones)
                if (matches(z)) add(z.id, placeName(z), detail(z));
        for (const auto& id : *others)
            if (lower(id).find(q) != std::string::npos) add(id, id, {});
        if (picked.empty()) results->add<Label>("No time zone matches")->setSize(14)->setColor("on_surface_variant", false);
        scroll->scrollTo(0, false);
    };
    // The search field keeps the list's function alive
    search->onChanged = [fill, refill](const std::string&) { refill(); };
    // Return takes the first match; with nothing typed, it sets the zone
    search->onSubmit = [=](const std::string&) {
        if (search->text().empty()) return set();
        if (first->empty()) return;
        show(*first);
        search->setText("");
        refill();
    };
}

} // namespace m3::shell
