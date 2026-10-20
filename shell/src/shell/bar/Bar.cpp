#include "shell/bar/Bar.h"

#include "core/Config.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/SystemStats.h"
#include "services/Weather.h"
#include "shell/Panels.h"
#include "shell/bar/Island.h"
#include "shell/bar/Widgets.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <algorithm>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

Bar::Bar(wl::Output* output, OpenPopup openPopup) : m_output(output), m_openPopup(std::move(openPopup)) {
    LayerSurface::Options opts;
    opts.ns = "material3ui-bar";
    opts.layer = LayerSurface::Layer::Top;
    opts.anchor = anchor();
    opts.height = totalHeight();
    opts.exclusiveZone = totalHeight();
    m_surface = std::make_unique<LayerSurface>(output, opts);
    rebuild();
}

uint32_t Bar::anchor() {
    const bool bottom = Config::get().value<std::string>("/statusBar/position", "top") == "bottom";
    return (bottom ? LayerSurface::Bottom : LayerSurface::Top) | LayerSurface::Left | LayerSurface::Right;
}

int Bar::height() const {
    return static_cast<int>(Config::get().value<double>("/statusBar/height", 40));
}

int Bar::totalHeight() const {
    const bool floating = Config::get().value<int>("/statusBar/backgroundStyle", 0) == 2;
    return height() + (floating ? static_cast<int>(Config::get().value<double>("/statusBar/gap", 6)) : 0);
}

namespace {

const char* kZones[] = {"left", "center", "right"};

// The bar's surface in the Full width and Floating bar styles: one backdrop
// with the modules over it. Floating, both stand off the screen's edges,
// `width` of it wide and centred, with rounded ends.
class BarFrame : public Node {
public:
    bool floating = false, bottom = false;
    float width = 1, gap = 6;

protected:
    SizeF doMeasure(SizeF available) override {
        for (auto& c : children()) c->measure(available);
        return available;
    }
    void doArrange() override {
        RectF r = rect();
        if (floating) {
            const float w = (r.w - gap * 2) * std::clamp(width, 0.3f, 1.0f);
            r = {r.x + (r.w - w) / 2, r.y + (bottom ? 0 : gap), w, r.h - gap};
        }
        for (auto& c : children()) c->arrange(r);
    }
};

// Live download/upload for the bar
class NetSpeed : public Flex {
public:
    NetSpeed() : Flex(Direction::Row) {
        setSpacing(6);
        m_down = add<Label>();
        m_up = add<Label>();
        for (Label* l : {m_down, m_up}) {
            l->setSize(12)->setWeight(500);
            l->minWidth = 64;
        }
        m_lease = Network::get().watchTraffic();
        m_conn = Network::get().detailsChanged.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        const auto& d = Network::get().details();
        m_down->setText("↓ " + panels::formatRate(d.rxRate));
        m_up->setText("↑ " + panels::formatRate(d.txRate));
    }
    Label *m_down, *m_up;
    Network::Lease m_lease;
    Connection m_conn;
};

// A capsule-sized button: icon, optional text, opens a panel
class Chip : public Button {
public:
    Chip(const std::string& icon, std::function<void()> click) : Button(Direction::Row) {
        pill(this)->setPadding({8, 4})->setSpacing(6);
        pressedRadius = 8;
        m_icon = add<Icon>(icon, 20);
        m_icon->setColor("on_surface", false);
        m_text = add<Label>();
        m_text->setSize(13)->setWeight(500);
        m_text->setVisible(false);
        onClicked = [click = std::move(click)](uint32_t b) {
            if (b == BTN_LEFT && click) click();
        };
    }
    void set(const std::string& icon, const std::string& text) {
        m_icon->setText(icon);
        m_text->setText(text);
        m_text->setVisible(!text.empty());
    }

protected:
    Icon* m_icon;
    Label* m_text;
};

// Temperature and sky, refreshed by the weather service
class WeatherChip : public Chip {
public:
    explicit WeatherChip(std::function<void()> click) : Chip("partly_cloudy_day", std::move(click)) {
        m_conn = Weather::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& w = Weather::get();
        set(w.ready() ? w.icon() : "cloud_off", w.ready() ? w.temp(w.temperature()) : "–");
    }
    Connection m_conn;
};

// "Sun 18 Oct", turning over at midnight
class DateChip : public Chip {
public:
    explicit DateChip(std::function<void()> click) : Chip("calendar_month", std::move(click)) { tick(); }
    ~DateChip() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

private:
    void tick() {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char buf[32];
        std::strftime(buf, sizeof buf, "%a %-d %b", &tm);
        set("calendar_month", buf);
        m_timer = EventLoop::get().addTimer((60 - tm.tm_sec) * 1000 + 50, [this] {
            m_timer = 0;
            tick();
        });
    }
    EventLoop::TimerId m_timer = 0;
};

// Bell with the number of notifications; struck through in Do Not Disturb
class NotificationChip : public Chip {
public:
    explicit NotificationChip(std::function<void()> click) : Chip("notifications", std::move(click)) {
        m_conn = Notifications::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& n = Notifications::get();
        const size_t count = n.history().size();
        set(n.doNotDisturb() ? "notifications_off" : count ? "notifications_unread" : "notifications",
            count ? std::to_string(count) : "");
    }
    Connection m_conn;
};

// CPU and memory use, sampled while on the bar
class SysmonChip : public Chip {
public:
    explicit SysmonChip(std::function<void()> click) : Chip("memory", std::move(click)) {
        m_lease = SystemStats::get().lease();
        m_conn = SystemStats::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& s = SystemStats::get();
        const double mem = s.memTotal() ? 100.0 * static_cast<double>(s.memUsed()) / static_cast<double>(s.memTotal()) : 0;
        set("memory", std::format("{:.0f}%  ·  {:.0f}%", s.cpu(), mem));
    }
    SystemStats::Lease m_lease;
    Connection m_conn;
};

} // namespace

const std::vector<Bar::Module>& Bar::modules() {
    static const std::vector<Module> all = {
        {"launcher", "apps", "Launcher", "Opens Spotlight"},
        {"workspaces", "view_week", "Workspaces", "Dots for each workspace; click to switch"},
        {"title", "title", "Window title", "The focused window"},
        {"island", "radio_button_checked", "Island", "Clock that grows for media and notifications"},
        {"clock", "schedule", "Clock", "Time in your chosen format"},
        {"calendar", "calendar_month", "Date", "Today's date; opens the dashboard calendar"},
        {"weather", "partly_cloudy_day", "Weather", "Live temperature and sky"},
        {"media", "music_note", "Media", "Now playing; click to pause, scroll to skip"},
        {"netspeed", "swap_vert", "Network speed", "Live download and upload"},
        {"sysmon", "memory", "CPU & memory", "Usage now; opens the system monitor"},
        {"tray", "more_horiz", "System tray", "App icons and their menus"},
        {"clipboard", "content_paste", "Clipboard", "Opens the clipboard history"},
        {"screenshot", "screenshot_region", "Screenshot", "Opens the screenshot tool"},
        {"notifications", "notifications", "Notifications", "Bell with a count; opens the centre"},
        {"quicksettings", "tune", "Quick Settings", "Opens Quick Settings"},
        {"status", "wifi", "Status icons", "Network, Bluetooth, volume; opens Quick Settings"},
        {"battery", "battery_full", "Battery", "Charge level (hidden on desktops)"},
        {"power", "power_settings_new", "Power", "Opens the power menu"},
    };
    return all;
}

Bar::Layout Bar::layout() {
    Layout out;
    const auto j = Config::get().value<nlohmann::json>("/statusBar/layout", nlohmann::json());
    if (j.is_object()) {
        for (int z = 0; z < 3; ++z)
            if (auto it = j.find(kZones[z]); it != j.end() && it->is_array())
                for (const auto& e : *it) {
                    Group g;
                    if (e.is_string()) g.push_back(e.get<std::string>());
                    else if (e.is_array())
                        for (const auto& id : e)
                            if (id.is_string()) g.push_back(id.get<std::string>());
                    if (!g.empty()) out[static_cast<size_t>(z)].push_back(g);
                }
        return out;
    }
    // Default, minus anything switched off with the older per-module toggles
    auto on = [](const char* id) { return Config::get().value<bool>(std::string("/statusBar/modules/") + id, true); };
    for (const char* id : {"workspaces", "title"})
        if (on(id)) out[0].push_back({id});
    out[1] = {{"island"}};
    for (const char* id : {"tray", "status", "battery"})
        if (on(id) || std::string(id) == "status") out[2].push_back({id});
    return out;
}

void Bar::setLayout(const Layout& layout) {
    nlohmann::json j = nlohmann::json::object();
    for (int z = 0; z < 3; ++z) {
        auto zone = nlohmann::json::array();
        for (const auto& g : layout[static_cast<size_t>(z)]) {
            if (g.size() == 1) zone.push_back(g.front());
            else if (!g.empty()) zone.push_back(g);
        }
        j[kZones[z]] = zone;
    }
    Config::get().set("/statusBar/layout", j);
}

bool Bar::placed(const Layout& l, const std::string& id) {
    for (const auto& zone : l)
        for (const auto& g : zone)
            if (std::ranges::find(g, id) != g.end()) return true;
    return false;
}

void Bar::removeWidget(Layout& l, const std::string& id) {
    for (auto& zone : l) {
        for (auto& g : zone) std::erase(g, id);
        std::erase_if(zone, [](const Group& g) { return g.empty(); });
    }
}

void Bar::add(Layout& l, size_t zone, const std::string& id) {
    removeWidget(l, id);
    l[std::min<size_t>(zone, 2)].push_back({id});
}

void Bar::moveWidget(Layout& l, const std::string& id, int dir) {
    // Where it is: zone, group
    for (size_t z = 0; z < 3; ++z)
        for (size_t gi = 0; gi < l[z].size(); ++gi) {
            auto& g = l[z][gi];
            const auto it = std::ranges::find(g, id);
            if (it == g.end()) continue;
            if (g.size() > 1) {
                // Leave the group, landing just beside it
                g.erase(it);
                l[z].insert(l[z].begin() + static_cast<long>(dir < 0 ? gi : gi + 1), Group{id});
                return;
            }
            l[z].erase(l[z].begin() + static_cast<long>(gi));
            if (dir < 0) {
                if (gi > 0) l[z].insert(l[z].begin() + static_cast<long>(gi - 1), Group{id});
                else if (z > 0) l[z - 1].push_back({id});
                else l[z].insert(l[z].begin(), Group{id});
            } else {
                if (gi < l[z].size()) l[z].insert(l[z].begin() + static_cast<long>(gi + 1), Group{id});
                else if (z < 2) l[z + 1].insert(l[z + 1].begin(), Group{id});
                else l[z].push_back({id});
            }
            return;
        }
}

void Bar::groupWidgets(Layout& l, size_t zone, const std::vector<std::string>& ids) {
    if (zone > 2 || ids.size() < 2) return;
    // In the zone's order, from wherever they are in it
    Group merged;
    long at = -1;
    for (size_t gi = 0; gi < l[zone].size(); ++gi)
        for (const auto& id : l[zone][gi])
            if (std::ranges::find(ids, id) != ids.end()) {
                if (at < 0) at = static_cast<long>(gi);
                merged.push_back(id);
            }
    if (merged.size() < 2) return;
    for (auto& g : l[zone]) std::erase_if(g, [&](const std::string& id) { return std::ranges::find(merged, id) != merged.end(); });
    // Keep the slot of the first one's group, even if it emptied
    l[zone].insert(l[zone].begin() + at, merged);
    std::erase_if(l[zone], [](const Group& g) { return g.empty(); });
}

void Bar::ungroup(Layout& l, const std::string& id) {
    for (auto& zone : l)
        for (size_t gi = 0; gi < zone.size(); ++gi)
            if (std::ranges::find(zone[gi], id) != zone[gi].end()) {
                const Group g = zone[gi];
                zone.erase(zone.begin() + static_cast<long>(gi));
                for (size_t k = 0; k < g.size(); ++k) zone.insert(zone.begin() + static_cast<long>(gi + k), Group{g[k]});
                return;
            }
}

void Bar::addModule(Flex* zone, const std::string& id, bool filled) {
    if (id == "workspaces") {
        auto* ws = zone->add<Workspaces>(m_output->name);
        if (const auto colour = filled ? pillColour() : std::nullopt) {
            pill(ws->setBackground(*colour, false));
            if (Config::get().value<std::string>("/statusBar/pillColor", "auto") == "outline") ws->setBorder(1.5f, ColorRef("outline", 0.7f));
            ws->blurBehind = barBlur();
        }
    } else if (id == "title") {
        capsule(zone, filled)->add<WindowTitle>();
    } else if (id == "island") {
        zone->add<Island>(m_openPopup);
    } else if (id == "clock") {
        capsule(zone, filled)->add<Clock>("bar");
    } else if (id == "netspeed") {
        capsule(zone, filled)->add<NetSpeed>();
    } else if (id == "tray") {
        auto* c = capsule(zone, filled);
        c->setPadding({4, 0});
        c->add<TrayIcons>(m_openPopup);
    } else if (id == "status") {
        auto* c = capsule(zone, filled);
        c->setPadding({4, 0});
        c->add<StatusIcons>(m_openPopup);
    } else if (id == "battery") {
        // The button fills the capsule (2 px in all round) so its hover
        // pill follows the capsule's shape
        auto* c = capsule(zone, filled);
        c->setPadding({2, 0});
        c->add<BatteryStatus>(m_openPopup);
    } else if (id == "media") {
        capsule(zone, filled)->add<Media>();
    } else if (id == "weather" || id == "calendar" || id == "notifications" || id == "sysmon") {
        auto* c = capsule(zone, filled);
        c->setPadding({4, 0});
        Chip* chip = nullptr;
        auto open = [this](const std::string& name, Node* from) {
            m_openPopup(name, from->rect().x + from->rect().w / 2);
        };
        if (id == "weather") chip = c->add<WeatherChip>(std::function<void()>{});
        else if (id == "calendar") chip = c->add<DateChip>(std::function<void()>{});
        else if (id == "notifications") chip = c->add<NotificationChip>(std::function<void()>{});
        else chip = c->add<SysmonChip>(std::function<void()>{});
        const std::string target = id == "weather" ? "settings weather" : id == "calendar" ? "dashboard"
                                 : id == "notifications" ? "notifications" : "sysmon";
        chip->tooltip = id == "weather" ? "Weather" : id == "calendar" ? "Calendar" : id == "notifications" ? "Notifications" : "System monitor";
        chip->onClicked = [open, chip, target](uint32_t b) {
            if (b == BTN_LEFT) open(target, chip);
        };
    } else {
        // One-tap buttons that open a panel
        const auto it = std::ranges::find_if(modules(), [&](const Module& m) { return id == m.id; });
        if (it == modules().end()) return;
        auto* c = capsule(zone, filled);
        c->setPadding({4, 0});
        auto* b = c->add<Button>();
        pill(b)->setPadding(Insets(5));
        b->pressedRadius = 6;
        b->add<Icon>(it->icon, 20)->setColor("on_surface", false);
        b->tooltip = it->name;
        b->onClicked = [this, b, id](uint32_t button) {
            if (button == BTN_LEFT) m_openPopup(id, b->rect().x + b->rect().w / 2);
        };
    }
}

void Bar::rebuild() {
    // backgroundStyle: 0 each module in its own pill, 1 one bar-wide surface,
    // 2 a floating bar (inset from the edges, rounded ends). The pills are
    // filled in the pill colour in any of them (Pill highlight)
    const auto& config = Config::get();
    const int style = config.value<int>("/statusBar/backgroundStyle", 0);
    const bool filled = pillColour().has_value();

    auto root = std::make_unique<BarLayout>();
    auto* bg = root.get();
    // Right click on empty bar space: edit the bar in place
    bg->onContextMenu = [] {
        if (panels::editBar) panels::editBar(true);
    };
    const auto zones = layout();
    for (size_t z = 0; z < 3; ++z) {
        auto* zone = bg->add<Flex>(Flex::Direction::Row);
        zone->setSpacing(8);
        if (!editing) {
            for (const auto& g : zones[z]) {
                if (g.size() == 1) {
                    addModule(zone, g.front(), filled);
                    continue;
                }
                // A group: one capsule around all of its widgets
                auto* cap = capsule(zone, filled);
                cap->setPadding({4, 0})->setSpacing(2);
                for (const auto& id : g) addModule(cap, id, false);
            }
            continue;
        }
        // Edit mode: the zone outlined, each widget with ◀ ✕ ▶, then "+"
        pill(zone->setBorder(1.5f, ColorRef("primary", 0.8f)))->setPadding({4, 2})->setSpacing(4);
        zone->setBackground(ColorRef("surface_container", 0.85f), false);
        zone->fixedHeight = static_cast<float>(height()) - 4;
        Group flat;
        for (const auto& g : zones[z]) flat.insert(flat.end(), g.begin(), g.end());
        for (size_t i = 0; i < flat.size(); ++i) {
            auto* item = zone->add<Flex>(Flex::Direction::Row);
            pill(item->setBackground("surface_container_highest", false))->setPadding({2, 0})->setSpacing(0);
            addModule(item, flat[i], false);
            auto control = [&](const char* icon, std::function<void(Layout&)> change) {
                auto* b = item->add<Button>();
                pill(b)->setPadding(Insets(3));
                b->pressedRadius = 4;
                b->add<Icon>(icon, 16)->setColor(icon == std::string("close") ? "error" : "on_surface_variant", false);
                b->onClicked = [change = std::move(change)](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    auto l = layout();
                    change(l);
                    setLayout(l); // the bars rebuild when config.json changes
                };
            };
            const std::string id = flat[i];
            if (i > 0 || z > 0) control("chevron_left", [id](Layout& l) { moveWidget(l, id, -1); });
            control("close", [id](Layout& l) { removeWidget(l, id); });
            if (i + 1 < flat.size() || z < 2) control("chevron_right", [id](Layout& l) { moveWidget(l, id, 1); });
        }
        auto* add = zone->add<Button>();
        pill(add->setBackground("primary_container", false))->setPadding({8, 4})->setSpacing(4);
        add->pressedRadius = 6;
        add->add<Icon>("add", 18)->setColor("on_primary_container", false);
        const char* names[] = {"left", "centre", "right"};
        add->onClicked = [this, add, z, names](uint32_t button) {
            if (button == BTN_LEFT) m_openPopup(std::string("barwidgets ") + names[z], add->rect().x + add->rect().w / 2);
        };
        if (z == 2) {
            auto* done = zone->add<Button>();
            pill(done->setBackground("primary", false))->setPadding({12, 4})->setSpacing(4);
            done->pressedRadius = 6;
            done->add<Icon>("check", 18)->setColor("on_primary", false);
            done->add<Label>("Done")->setSize(13)->setWeight(600)->setColor("on_primary", false);
            done->onClicked = [](uint32_t) {
                if (panels::editBar) panels::editBar(false);
            };
        }
    }

    auto* surface = m_surface.get();
    if (style != 0) {
        // One surface under everything: edge to edge, or floating
        auto frame = std::make_unique<BarFrame>();
        frame->floating = style == 2;
        frame->bottom = config.value<std::string>("/statusBar/position", "top") == "bottom";
        frame->width = static_cast<float>(config.value<double>("/statusBar/width", 1.0));
        frame->gap = static_cast<float>(config.value<double>("/statusBar/gap", 6));
        auto* back = frame->add<Flex>(Flex::Direction::Row);
        back->setBackground(ColorRef("surface_container", Theme::get().barOpacity()), false);
        back->blurBehind = barBlur();
        if (frame->floating) {
            back->setRadius(Radii(static_cast<float>(config.value<double>("/statusBar/endRadius", 100))));
            // Clear of the rounded ends
            bg->margin = 8 + std::min(12.0f, static_cast<float>(config.value<double>("/statusBar/endRadius", 100)) / 4);
        }
        frame->adopt(std::move(root));
        surface->setRoot(std::move(frame));
    } else {
        surface->setRoot(std::move(root));
    }
    m_surface->setAnchor(anchor());
    m_surface->setSize(0, totalHeight());
    m_surface->setExclusiveZone(totalHeight());
}

void Bar::setVisible(bool visible) {
    m_surface->setVisible(visible);
}

} // namespace m3::shell
