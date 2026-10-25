#include "shell/WidgetStyles.h"

#include "core/EventLoop.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "quickshare/QuickShare.h"
#include "services/Calendar.h"
#include "services/Mpris.h"
#include "services/Printers.h"
#include "services/Vpn.h"
#include "services/Weather.h"
#include "services/Wellbeing.h"
#include "shell/ClockFaces.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::tm localNow(long offsetDays = 0) {
    const std::time_t t = std::time(nullptr) + offsetDays * 86400;
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

std::string format(const char* fmt, const std::tm& tm) {
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

// A translucent card that reads on any wallpaper
Flex* card(Node* parent, Flex::Direction dir = Flex::Direction::Column) {
    auto* c = parent->add<Flex>(dir);
    c->setBackground(ColorRef("surface_container_high", 0.8f), false)->setRadius(Radii(28))->setPadding({18, 16})->setSpacing(10);
    c->blurBehind = true;
    return c;
}

// A widget that rebuilds when its services change, and each minute (the date
// turning over)
class Styled : public Live {
public:
    Styled(std::string id, bool greeter, Align align) : m_id(std::move(id)), m_greeter(greeter) {
        setAlignItems(align)->setSpacing(0);
        sync = [this] { build(); };
        if (m_id.starts_with("weather")) watch(Weather::get().changed);
        if (m_id == "cal-agenda") watch(m3::Calendar::get().changed);
        if (m_id.starts_with("screentime") || m_id == "wellbeing-modes") watch(Wellbeing::get().changed);
        if (m_id == "vpn") watch(Vpn::get().changed);
        if (m_id == "quickshare") watch(quickshare::QuickShare::get().changed);
        if (m_id == "printers") {
            watch(Printers::get().changed);
            m_printers = Printers::get().watch(); // the queue, while it shows
        }
        if (m_id.starts_with("music")) {
            watch(Mpris::get().changed);
            // The progress moves on by itself
            m_ticker = std::make_unique<SecondTicker>([this] { progress(); });
        }
        minute();
        build();
    }
    ~Styled() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

private:
    void minute() {
        m_timer = EventLoop::get().addTimer((60 - localNow().tm_sec) * 1000 + 50, [this] {
            m_timer = 0;
            syncSoon();
            minute();
        });
    }
    void build();
    void calendarDay();
    void calendarWeek();
    void calendarMonth();
    void calendarAgenda();
    void musicDisc(const Mpris::Player& p);
    void musicBar(const Mpris::Player& p);
    void weatherBig();
    void weatherCard();
    void screenTime();
    void screenTimeWeek();
    void wellbeingModes();
    void vpn();
    void quickShare();
    void printers();
    Flex* controls(Node* parent, const Mpris::Player& p, float big);
    void progress() {
        const auto* p = Mpris::get().active();
        if (!m_progress || !p || p->length <= 0) return;
        m_progress->set(static_cast<float>(static_cast<double>(p->positionNow()) / static_cast<double>(p->length)), p->status == "Playing");
    }

    std::string m_id;
    bool m_greeter;
    float m_k = widgetScale(); // Widget size
    EventLoop::TimerId m_timer = 0;
    std::unique_ptr<SecondTicker> m_ticker;
    Printers::Lease m_printers;
    WavyProgress* m_progress = nullptr;
};

// The cover, rounded (a circle for the disc); a note when there's none
class ArtView : public Flex {
public:
    ArtView(std::string path, float size, float radius) : Flex(Direction::Row), m_path(std::move(path)), m_radius(radius) {
        fixedWidth = fixedHeight = size;
        setJustify(Align::Center)->setRadius(Radii(radius))->setBackground("secondary_container", false);
        if (m_path.empty()) add<Icon>("music_note", size * 0.4f)->setColor("on_secondary_container", false);
    }
    ~ArtView() override { *m_alive = false; }

protected:
    void doPaint(Renderer& r) override {
        Flex::doPaint(r);
        if (m_path.empty()) return;
        const auto& b = rect();
        const int px = static_cast<int>(std::lround(b.w * r.scale()));
        if (auto* t = Images::get().picture(m_path, px, px, true, [this, alive = m_alive] {
                if (*alive) requestRedraw();
            }))
            r.image(t->id, b, 1, Radii(std::min(m_radius, b.w / 2)));
    }

private:
    std::string m_path;
    float m_radius;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

void Styled::build() {
    clearChildren();
    m_progress = nullptr;
    setVisible(true);
    if (m_id == "cal-day") return calendarDay();
    if (m_id == "cal-week") return calendarWeek();
    if (m_id == "cal-month") return calendarMonth();
    if (m_id == "cal-agenda") return calendarAgenda();
    // Nothing personal on the login screen
    if (m_greeter && (m_id.starts_with("screentime") || m_id == "wellbeing-modes" || m_id == "vpn" || m_id == "quickshare" ||
                      m_id == "printers"))
        return setVisible(false);
    if (m_id == "screentime") return screenTime();
    if (m_id == "screentime-week") return screenTimeWeek();
    if (m_id == "wellbeing-modes") return wellbeingModes();
    if (m_id == "vpn") return vpn();
    if (m_id == "quickshare") return quickShare();
    if (m_id == "printers") return printers();
    if (m_id.starts_with("music")) {
        // Never on the login screen: nobody's music plays there
        const auto* p = m_greeter ? nullptr : Mpris::get().active();
        if (!p) return setVisible(false);
        return m_id == "music-disc" ? musicDisc(*p) : musicBar(*p);
    }
    if (!Weather::get().ready()) return setVisible(false);
    if (m_id == "weather-big") return weatherBig();
    weatherCard();
}

// ── Calendars ──

// Today as a tile: the weekday, a big day number and the month
void Styled::calendarDay() {
    const std::tm tm = localNow();
    auto* tile = add<Flex>(Flex::Direction::Column);
    tile->setBackground(ColorRef("primary_container", 0.92f), false)->setRadius(Radii(32))->setPadding({26, 16});
    tile->setAlignItems(Align::Center)->setSpacing(-8);
    tile->blurBehind = true;
    tile->add<Label>(format("%A", tm))->setSize(16)->setWeight(600)->setColor("on_primary_container", false);
    tile->add<Label>(format("%-d", tm))->setSize(80)->setWeight(700)->setColor("on_primary_container", false);
    tile->add<Label>(format("%B", tm))->setSize(15)->setWeight(500)->setColor("on_primary_container", false);
}

// This week, Monday to Sunday, today in a filled circle
void Styled::calendarWeek() {
    const std::tm today = localNow();
    const int fromMonday = (today.tm_wday + 6) % 7;
    auto* row = card(this, Flex::Direction::Row);
    row->setSpacing(4)->setPadding({12, 12});
    for (int i = 0; i < 7; ++i) {
        const std::tm d = localNow(i - fromMonday);
        const bool isToday = i == fromMonday;
        auto* col = row->add<Flex>(Flex::Direction::Column);
        col->setAlignItems(Align::Center)->setSpacing(6);
        col->fixedWidth = 44 * m_k;
        col->add<Label>(format("%a", d).substr(0, 1))->setSize(12)->setWeight(600)->setColor(isToday ? "primary" : "on_surface_variant", false);
        auto* num = col->add<Flex>(Flex::Direction::Row);
        pill(num)->setJustify(Align::Center);
        num->fixedWidth = num->fixedHeight = 36 * m_k;
        if (isToday) num->setBackground("primary", false);
        num->add<Label>(format("%-d", d))->setSize(15)->setWeight(isToday ? 700 : 500)->setColor(isToday ? "on_primary" : "on_surface", false);
    }
}

// The month as a grid, today picked out
void Styled::calendarMonth() {
    const std::tm today = localNow();
    auto* c = card(this);
    c->setSpacing(4)->setPadding({16, 14});
    c->add<Label>(format("%B %Y", today))->setSize(16)->setWeight(600)->setColor("primary", false);
    const float k = m_k;
    auto cell = [k](Node* parent, const std::string& text, bool header, bool isToday, bool dim) {
        auto* f = parent->add<Flex>(Flex::Direction::Row);
        pill(f)->setJustify(Align::Center);
        f->fixedWidth = 36 * k;
        f->fixedHeight = (header ? 22 : 32) * k;
        if (isToday) f->setBackground("primary", false);
        f->add<Label>(text)
            ->setSize(header ? 11 : 13)
            ->setWeight(header || isToday ? 600 : 400)
            ->setColor(isToday ? "on_primary" : header || dim ? "on_surface_variant" : "on_surface", false);
    };
    auto* head = c->add<Flex>(Flex::Direction::Row);
    for (const char* d : {"M", "T", "W", "T", "F", "S", "S"}) cell(head, d, true, false, false);
    // From the Monday on or before the 1st, six weeks at most
    std::tm first = today;
    first.tm_mday = 1;
    first.tm_hour = 12;
    std::mktime(&first);
    const int lead = (first.tm_wday + 6) % 7;
    const int month = today.tm_mon;
    for (int week = 0; week < 6; ++week) {
        if (week > 0) {
            std::tm probe = first;
            probe.tm_mday = 1 - lead + week * 7;
            std::mktime(&probe);
            if (probe.tm_mon != month) break; // past the month's end
        }
        auto* row = c->add<Flex>(Flex::Direction::Row);
        for (int i = 0; i < 7; ++i) {
            std::tm d = first;
            d.tm_mday = 1 - lead + week * 7 + i;
            std::mktime(&d);
            const bool in = d.tm_mon == month;
            cell(row, in ? std::to_string(d.tm_mday) : "", false, in && d.tm_mday == today.tm_mday, !in);
        }
    }
}

// What's next: the coming events, by day (not on the login screen: they're
// yours)
void Styled::calendarAgenda() {
    if (m_greeter) return setVisible(false);
    const std::time_t now = std::time(nullptr);
    const auto items = m3::Calendar::get().between(now, now + 14L * 86400);
    auto* c = card(this);
    c->fixedWidth = 340 * m_k;
    c->setSpacing(8)->setAlignItems(Align::Stretch);
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8)->setAlignItems(Align::Center);
    head->add<Icon>("event", 20)->setColor("primary", false);
    head->add<Label>("Coming up")->setSize(15)->setWeight(600)->setColor("primary", false);
    if (items.empty()) {
        c->add<Label>("Nothing in the next two weeks")->setSize(13)->setColor("on_surface_variant", false);
        return;
    }
    const std::tm today = localNow();
    int lastDay = -1;
    for (size_t i = 0; i < items.size() && i < 5; ++i) {
        const auto& o = items[i];
        std::tm tm{};
        localtime_r(&o.start, &tm);
        if (tm.tm_yday != lastDay) {
            lastDay = tm.tm_yday;
            const bool isToday = tm.tm_yday == today.tm_yday && tm.tm_year == today.tm_year;
            const bool isTomorrow = (tm.tm_yday == today.tm_yday + 1 && tm.tm_year == today.tm_year);
            c->add<Label>(isToday ? std::string("Today") : isTomorrow ? std::string("Tomorrow") : format("%A %-d %B", tm))
                ->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
        }
        auto* r = c->add<Flex>(Flex::Direction::Row);
        r->setSpacing(10)->setAlignItems(Align::Center);
        auto* bar = r->add<Flex>(Flex::Direction::Row);
        pill(bar->setBackground(o.event->source.empty() ? "primary" : "tertiary", false));
        bar->fixedWidth = 4;
        bar->fixedHeight = 30 * m_k;
        auto* texts = r->add<Flex>(Flex::Direction::Column);
        texts->setSpacing(0)->setAlignItems(Align::Start);
        texts->grow = true;
        texts->add<Label>(o.event->title.empty() ? "(No title)" : o.event->title)->setSize(14)->setWeight(500)->setMaxWidth(270 * m_k);
        texts->add<Label>(o.event->allDay ? std::string("All day") : format("%H:%M", tm) + (o.event->location.empty() ? "" : " · " + o.event->location))
            ->setSize(12)->setMaxWidth(270 * m_k)->setColor("on_surface_variant", false);
    }
}

// ── Music ──

Flex* Styled::controls(Node* parent, const Mpris::Player& p, float big) {
    auto* row = parent->add<Flex>(Flex::Direction::Row);
    row->setSpacing(8)->setAlignItems(Align::Center);
    auto button = [&](const char* icon, bool main, std::function<void()> fn) {
        auto* b = row->add<Button>();
        pill(b)->setJustify(Align::Center);
        b->fixedWidth = b->fixedHeight = main ? big : big * 0.75f;
        if (main) b->setBackground("primary", false);
        b->add<Icon>(icon, main ? big * 0.5f : big * 0.42f)->setColor(main ? "on_primary" : "on_surface", false);
        b->onClicked = [fn = std::move(fn)](uint32_t button) {
            if (button == BTN_LEFT) fn();
        };
    };
    button("skip_previous", false, [] { Mpris::get().previous(); });
    button(p.status == "Playing" ? "pause" : "play_arrow", true, [] { Mpris::get().playPause(); });
    button("skip_next", false, [] { Mpris::get().next(); });
    return row;
}

// The cover as a disc in the wavy progress ring, the song and controls under it
void Styled::musicDisc(const Mpris::Player& p) {
    auto* col = add<Flex>(Flex::Direction::Column);
    col->setAlignItems(Align::Center)->setSpacing(8);
    auto* ring = col->add<WavyRing>(176 * m_k);
    ring->add<ArtView>(Mpris::get().artPath(p), 144 * m_k, 72 * m_k);
    col->add<Label>(p.title.empty() ? p.identity : p.title)->setSize(18)->setWeight(600)->setMaxWidth(280)->setColor("on_surface", false);
    if (!p.artist.empty()) col->add<Label>(p.artist)->setSize(14)->setMaxWidth(280)->setColor("on_surface_variant", false);
    controls(col, p, 52 * m_k);
}

// A slim bar: cover, song, wavy progress and play/pause beside it
void Styled::musicBar(const Mpris::Player& p) {
    auto* bar = card(this, Flex::Direction::Row);
    bar->setRadius(Radii(32))->setPadding({10, 10, 14, 10})->setSpacing(14)->setAlignItems(Align::Center);
    bar->fixedWidth = 440 * m_k;
    bar->add<ArtView>(Mpris::get().artPath(p), 64 * m_k, 20 * m_k);
    auto* text = bar->add<Flex>(Flex::Direction::Column);
    text->setSpacing(2)->setAlignItems(Align::Stretch);
    text->grow = true;
    text->add<Label>(p.title.empty() ? p.identity : p.title)->setSize(15)->setWeight(600)->setMaxWidth(220)->setColor("on_surface", false);
    text->add<Label>(p.artist.empty() ? p.identity : p.artist)->setSize(13)->setMaxWidth(220)->setColor("on_surface_variant", false);
    m_progress = text->add<WavyProgress>();
    if (p.canSeek && p.length > 0) m_progress->onSeek = [](float f) { Mpris::get().seek(f); };
    progress();
    auto* row = bar->add<Flex>(Flex::Direction::Row);
    row->setSpacing(4);
    auto* play = row->add<Button>();
    pill(play)->setJustify(Align::Center)->setBackground("primary", false);
    play->fixedWidth = play->fixedHeight = 44 * m_k;
    play->add<Icon>(p.status == "Playing" ? "pause" : "play_arrow", 24)->setColor("on_primary", false);
    play->onClicked = [](uint32_t b) {
        if (b == BTN_LEFT) Mpris::get().playPause();
    };
    auto* next = row->add<Button>();
    pill(next)->setJustify(Align::Center);
    next->fixedWidth = next->fixedHeight = 40 * m_k;
    next->add<Icon>("skip_next", 22)->setColor("on_surface", false);
    next->onClicked = [](uint32_t b) {
        if (b == BTN_LEFT) Mpris::get().next();
    };
}

// ── Weather ──

// The temperature, large, beside the sky
void Styled::weatherBig() {
    auto& w = Weather::get();
    auto* row = add<Flex>(Flex::Direction::Row);
    row->setSpacing(16)->setAlignItems(Align::Center);
    row->add<Icon>(w.icon(), 84)->setFill(true)->setColor("primary", false);
    auto* col = row->add<Flex>(Flex::Direction::Column);
    col->setSpacing(-4);
    col->add<Label>(w.temp(w.temperature()))->setSize(72)->setWeight(400)->setColor("on_surface", false);
    std::string line = Weather::describe(w.code());
    if (!w.days().empty()) line += "  ·  " + w.temp(w.days()[0].max) + " / " + w.temp(w.days()[0].min);
    col->add<Label>(line)->setSize(15)->setWeight(500)->setColor("on_surface_variant", false);
}

// Now and the next four days, on a card
void Styled::weatherCard() {
    auto& w = Weather::get();
    auto* c = card(this);
    c->fixedWidth = 360 * m_k;
    c->setSpacing(12)->setPadding({20, 18})->setAlignItems(Align::Stretch);
    auto* top = c->add<Flex>(Flex::Direction::Row);
    top->setAlignItems(Align::Center);
    auto* now = top->add<Flex>(Flex::Direction::Column);
    now->setSpacing(0)->setAlignItems(Align::Start);
    now->grow = true;
    now->add<Label>(w.place().substr(0, w.place().find(',')))->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    now->add<Label>(w.temp(w.temperature()))->setSize(48)->setWeight(500)->setColor("on_surface", false);
    now->add<Label>(Weather::describe(w.code()))->setSize(14)->setColor("on_surface", false);
    top->add<Icon>(w.icon(), 64)->setFill(true)->setColor("primary", false);
    auto* days = c->add<Flex>(Flex::Direction::Row);
    days->setSpacing(6);
    for (size_t i = 1; i < std::min<size_t>(w.days().size(), 5); ++i) {
        const auto& d = w.days()[i];
        std::tm tm{};
        strptime(d.date.c_str(), "%Y-%m-%d", &tm);
        auto* day = days->add<Flex>(Flex::Direction::Column);
        day->setAlignItems(Align::Center)->setSpacing(4)->setPadding({0, 8})->setRadius(Radii(18));
        day->setBackground(ColorRef("surface_container_highest", 0.7f), false);
        day->grow = true;
        day->add<Label>(format("%a", tm))->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
        day->add<Icon>(Weather::icon(d.code, true), 22)->setColor("primary", false);
        day->add<Label>(w.temp(d.max))->setSize(13)->setWeight(600)->setColor("on_surface", false);
        day->add<Label>(w.temp(d.min))->setSize(12)->setColor("on_surface_variant", false);
    }
}

// ── Digital Wellbeing, VPN, Quick Share ──

namespace {

std::string minutes(int seconds) {
    if (seconds > 0 && seconds < 60) return "< 1 min";
    const int h = seconds / 3600, m = seconds / 60 % 60;
    return h ? std::format("{} h {} min", h, m) : std::format("{} min", m);
}

// A tonal pill that switches something, filled while on
Button* togglePill(Node* parent, const std::string& icon, const std::string& label, bool on, float k, std::function<void()> flip) {
    auto* b = parent->add<Button>(Flex::Direction::Row);
    pill(b)->setPadding({14 * k, 10 * k})->setSpacing(8 * k);
    b->setBackground(on ? ColorRef("primary") : ColorRef("surface_container_highest", 0.8f), false);
    b->pressedRadius = 10;
    b->grow = true;
    b->add<Icon>(icon, 20 * k)->setFill(on)->setColor(on ? "on_primary" : "on_surface", false);
    b->add<Label>(label)->setSize(14 * k)->setWeight(600)->setColor(on ? "on_primary" : "on_surface", false);
    b->onClicked = [flip = std::move(flip)](uint32_t button) {
        if (button == BTN_LEFT) flip();
    };
    return b;
}

} // namespace

// Today's screen time: the ring of apps, the total, the top three
void Styled::screenTime() {
    const auto& today = Wellbeing::get().today();
    std::vector<std::pair<std::string, int>> apps(today.seconds.begin(), today.seconds.end());
    std::ranges::sort(apps, std::greater{}, &std::pair<std::string, int>::second);
    std::vector<std::pair<std::string, int>> parts;
    int shown = 0;
    for (size_t i = 0; i < apps.size() && i < 4; ++i) {
        parts.push_back(apps[i]);
        shown += apps[i].second;
    }
    if (today.total() > shown) parts.push_back({"", today.total() - shown});
    auto* c = card(this, Flex::Direction::Row);
    c->setSpacing(18 * m_k)->setAlignItems(Align::Center);
    // No sweep: it rebuilds as the minutes pass
    c->add<UsageRing>(84 * m_k, false)->set(parts, today.total());
    auto* text = c->add<Flex>(Flex::Direction::Column);
    text->setSpacing(4 * m_k)->setAlignItems(Align::Start);
    text->add<Label>(minutes(today.total()))->setSize(26 * m_k)->setWeight(500)->setColor("on_surface", false);
    text->add<Label>("Screen time today")->setSize(12 * m_k)->setColor("on_surface_variant", false);
    for (size_t i = 0; i < parts.size() && i < 3; ++i) {
        if (parts[i].first.empty()) continue;
        auto* k = text->add<Flex>(Flex::Direction::Row);
        k->setSpacing(6 * m_k)->setAlignItems(Align::Center);
        auto* dot = k->add<Flex>(Flex::Direction::Row);
        pill(dot->setBackground(UsageRing::kRoles[i], false));
        dot->fixedWidth = dot->fixedHeight = 8 * m_k;
        k->add<Label>(std::format("{} · {}", Wellbeing::nameOf(parts[i].first), minutes(parts[i].second)))
            ->setSize(12 * m_k)
            ->setMaxWidth(200 * m_k)
            ->setColor("on_surface", false);
    }
}

// The week's screen time: a bar a day (today's in the primary colour), the
// average and today's total
void Styled::screenTimeWeek() {
    auto& w = Wellbeing::get();
    std::vector<int> days;
    std::vector<std::string> names;
    int sum = 0, counted = 0, most = 1;
    for (int ago = 6; ago >= 0; --ago) {
        const int t = w.day(ago).total();
        days.push_back(t);
        sum += t;
        counted += t > 0;
        most = std::max(most, t);
        names.push_back(format("%a", localNow(-ago)).substr(0, 1));
    }
    auto* c = card(this);
    c->setSpacing(12 * m_k)->setAlignItems(Align::Stretch);
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8 * m_k)->setAlignItems(Align::Center);
    head->add<Icon>("bar_chart", 20 * m_k)->setColor("primary", false);
    auto* title = head->add<Label>(std::format("This week · {} a day", minutes(counted ? sum / counted : 0)));
    title->setSize(13 * m_k)->setWeight(600)->setColor("on_surface", false);
    title->grow = true;
    head->add<Label>("Today " + minutes(days.back()))->setSize(12 * m_k)->setColor("on_surface_variant", false);
    auto* bars = c->add<Flex>(Flex::Direction::Row);
    bars->setSpacing(10 * m_k)->setAlignItems(Align::End);
    const float tall = 72 * m_k, thick = 18 * m_k;
    for (size_t i = 0; i < days.size(); ++i) {
        const bool today = i + 1 == days.size();
        auto* col = bars->add<Flex>(Flex::Direction::Column);
        col->setSpacing(6 * m_k)->setAlignItems(Align::Center);
        col->grow = true; // the days share the card's width
        auto* bar = col->add<Flex>(Flex::Direction::Column);
        pill(bar->setBackground(today ? "primary" : days[i] ? "secondary_container" : "surface_container_highest", false));
        bar->fixedWidth = thick;
        bar->fixedHeight = std::max(thick, tall * static_cast<float>(days[i]) / static_cast<float>(most));
        col->add<Label>(names[i])->setSize(11 * m_k)->setWeight(today ? 700 : 500)->setColor(today ? "primary" : "on_surface_variant", false);
    }
}

// Focus mode and Bedtime, a tap each
void Styled::wellbeingModes() {
    auto& w = Wellbeing::get();
    auto* c = card(this);
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8 * m_k)->setAlignItems(Align::Center);
    head->add<Icon>("digital_wellbeing", 20 * m_k)->setColor("primary", false);
    head->add<Label>(std::format("Digital Wellbeing · {}", minutes(w.today().total())))->setSize(13 * m_k)->setWeight(600)->setColor(
        "on_surface", false);
    auto* row = c->add<Flex>(Flex::Direction::Row);
    row->setSpacing(8 * m_k);
    togglePill(row, "self_improvement", "Focus", w.focus(), m_k, [] { Wellbeing::get().setFocus(!Wellbeing::get().focus()); });
    togglePill(row, "bedtime", "Bedtime", w.bedtime(), m_k, [] { Wellbeing::get().setBedtimeNow(!Wellbeing::get().bedtime()); });
}

// The VPN in use, and a switch
void Styled::vpn() {
    auto& v = Vpn::get();
    const bool on = v.anyActive(), busy = !v.busy().empty();
    auto* c = card(this, Flex::Direction::Row);
    c->setSpacing(14 * m_k)->setAlignItems(Align::Center);
    auto* bubble = c->add<Flex>(Flex::Direction::Row);
    pill(bubble->setBackground(on ? "primary" : "surface_container_highest", false))->setJustify(Align::Center);
    bubble->fixedWidth = bubble->fixedHeight = 44 * m_k;
    bubble->add<Icon>(on ? "vpn_lock" : "vpn_key", 22 * m_k)->setFill(on)->setColor(on ? "on_primary" : "on_surface", false);
    auto* text = c->add<Flex>(Flex::Direction::Column);
    text->setSpacing(2)->setAlignItems(Align::Start);
    text->grow = true;
    text->add<Label>(on ? v.activeName() : "VPN")->setSize(15 * m_k)->setWeight(600)->setMaxWidth(180 * m_k)->setColor("on_surface", false);
    text->add<Label>(busy ? "Connecting…" : on ? "Connected" : v.connections().empty() && v.warp() == Vpn::Warp::Absent ? "None set up" : "Off")
        ->setSize(12 * m_k)
        ->setColor("on_surface_variant", false);
    auto* sw = c->add<Switch>();
    sw->setChecked(on || busy, false);
    sw->onToggled = [](bool) { Vpn::get().toggleLast(); };
}

// Quick Share: visible or not, a transfer's progress, Send
void Styled::quickShare() {
    auto& q = quickshare::QuickShare::get();
    auto* c = card(this, Flex::Direction::Row);
    c->setSpacing(14 * m_k)->setAlignItems(Align::Center);
    auto* bubble = c->add<Flex>(Flex::Direction::Row);
    pill(bubble->setBackground("primary_container", false))->setJustify(Align::Center);
    bubble->fixedWidth = bubble->fixedHeight = 44 * m_k;
    bubble->add<Icon>(quickshare::QuickShare::symbol(), 24 * m_k)->setColor("on_primary_container", false);
    auto* text = c->add<Flex>(Flex::Direction::Column);
    text->setSpacing(2)->setAlignItems(Align::Start);
    text->grow = true;
    text->add<Label>("Quick Share")->setSize(15 * m_k)->setWeight(600)->setColor("on_surface", false);
    std::string status = q.receiving() ? "Visible as " + q.deviceName() : "Not visible";
    for (const auto& [_, t] : q.transfers())
        if (t.state == quickshare::QuickShare::State::Receiving && t.total > 0)
            status = std::format("{} {} · {}%", t.outgoing ? "To" : "From", t.sender, t.received * 100 / t.total);
    text->add<Label>(status)->setSize(12 * m_k)->setMaxWidth(190 * m_k)->setColor("on_surface_variant", false);
    auto* send = c->add<Button>(Flex::Direction::Row);
    pill(send->setBackground("primary", false))->setPadding({14 * m_k, 8 * m_k})->setSpacing(6 * m_k);
    send->add<Icon>("send", 18 * m_k)->setColor("on_primary", false);
    send->add<Label>("Send")->setSize(13 * m_k)->setWeight(600)->setColor("on_primary", false);
    send->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT && panels::open) panels::open("quickshare-send ");
    };
}

// The default printer, its state and the queue
void Styled::printers() {
    auto& pr = Printers::get();
    const Printers::Printer* def = nullptr;
    for (const auto& p : pr.printers())
        if (p.isDefault || !def) def = &p;
    const size_t jobs = pr.jobs().size();
    const bool paused = def && def->state == "stopped";
    auto* c = card(this, Flex::Direction::Row);
    c->setSpacing(14 * m_k)->setAlignItems(Align::Center);
    auto* bubble = c->add<Flex>(Flex::Direction::Row);
    // Paused first: it's why anything waits
    pill(bubble->setBackground(paused ? "error_container" : jobs ? "primary" : "primary_container", false))->setJustify(Align::Center);
    bubble->fixedWidth = bubble->fixedHeight = 44 * m_k;
    bubble->add<Icon>(paused ? "print_disabled" : jobs ? "print_connect" : "print", 22 * m_k)
        ->setColor(paused ? "on_error_container" : jobs ? "on_primary" : "on_primary_container", false);
    auto* text = c->add<Flex>(Flex::Direction::Column);
    text->setSpacing(2)->setAlignItems(Align::Start);
    text->grow = true;
    text->add<Label>(def ? (def->description.empty() ? def->name : def->description) : "Printers")
        ->setSize(15 * m_k)
        ->setWeight(600)
        ->setMaxWidth(200 * m_k)
        ->setColor("on_surface", false);
    text->add<Label>(pr.summary())->setSize(12 * m_k)->setMaxWidth(200 * m_k)->setColor("on_surface_variant", false);
    // Settings → Printers, open or not
    auto* settings = c->add<Button>(Flex::Direction::Row);
    pill(settings->setBackground("surface_container_highest", false))->setPadding({10 * m_k, 8 * m_k});
    settings->add<Icon>("settings", 18 * m_k)->setColor("on_surface", false);
    settings->tooltip = "Printer settings";
    settings->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT && panels::open) panels::open("settings printers");
    };
}

} // namespace

Node* addStyledWidget(Node* parent, const std::string& id, bool greeter, Align align) {
    static const char* ids[] = {"cal-day",     "cal-week",     "cal-month",  "cal-agenda",      "music-disc", "music-bar",
                                "weather-big", "weather-card", "screentime", "screentime-week", "wellbeing-modes", "vpn", "quickshare",
                                "printers"};
    for (const char* known : ids)
        if (id == known) return parent->add<Styled>(id, greeter, align);
    return nullptr;
}

} // namespace m3::shell
