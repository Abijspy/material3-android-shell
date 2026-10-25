#include "shell/bar/Bar.h"

#include "core/Config.h"
#include "quickshare/QuickShare.h"
#include "render/Images.h"
#include "services/Firewall.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Printers.h"
#include "services/SystemStats.h"
#include "services/Vpn.h"
#include "services/Weather.h"
#include "services/Wellbeing.h"
#include "shell/Panels.h"
#include "shell/bar/Island.h"
#include "shell/bar/Widgets.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <algorithm>
#include <ctime>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

Bar::Bar(wl::Output* output, OpenPopup openPopup) : m_output(output), m_openPopup(std::move(openPopup)) {
    LayerSurface::Options opts;
    opts.ns = "material3ui-bar";
    opts.layer = LayerSurface::Layer::Top;
    opts.anchor = anchor();
    (barVertical() ? opts.width : opts.height) = totalHeight();
    opts.exclusiveZone = totalHeight();
    m_surface = std::make_unique<LayerSurface>(output, opts);
    rebuild();
}

uint32_t Bar::anchor() {
    switch (barEdge()) {
    case BarEdge::Bottom: return LayerSurface::Bottom | LayerSurface::Left | LayerSurface::Right;
    case BarEdge::Left: return LayerSurface::Left | LayerSurface::Top | LayerSurface::Bottom;
    case BarEdge::Right: return LayerSurface::Right | LayerSurface::Top | LayerSurface::Bottom;
    case BarEdge::Top: break;
    }
    return LayerSurface::Top | LayerSurface::Left | LayerSurface::Right;
}

int Bar::thickness() {
    return static_cast<int>(Config::get().value<double>("/statusBar/height", 40));
}

bool Bar::framed() {
    return Config::get().value<int>("/statusBar/backgroundStyle", 0) == 3;
}

int Bar::height() const {
    return thickness();
}

int Bar::zone() {
    const bool floating = Config::get().value<int>("/statusBar/backgroundStyle", 0) == 2;
    return thickness() + (floating ? static_cast<int>(Config::get().value<double>("/statusBar/gap", 6)) : 0);
}

namespace {

const char* kZones[] = {"left", "center", "right"};

// The bar's surface in the Full width and Floating bar styles: one backdrop
// with the modules over it. Floating, both stand off the screen's edges,
// `width` of it long and centred, with rounded ends.
class BarFrame : public Node {
public:
    bool floating = false;
    BarEdge edge = BarEdge::Top;
    float width = 1, gap = 6;

protected:
    SizeF doMeasure(SizeF available) override {
        for (auto& c : children()) c->measure(available);
        return available;
    }
    void doArrange() override {
        RectF r = rect();
        if (floating) {
            const float k = std::clamp(width, 0.3f, 1.0f);
            if (edge == BarEdge::Left || edge == BarEdge::Right) {
                const float h = (r.h - gap * 2) * k;
                r = {r.x + (edge == BarEdge::Left ? gap : 0), r.y + (r.h - h) / 2, r.w - gap, h};
            } else {
                const float w = (r.w - gap * 2) * k;
                r = {r.x + (r.w - w) / 2, r.y + (edge == BarEdge::Bottom ? 0 : gap), w, r.h - gap};
            }
        }
        for (auto& c : children()) c->arrange(r);
    }
};

// The system's own logo (os-release's LOGO), drawn in the accent like an
// icon; Spotlight's symbol when there's none to find
std::string logoIcon() {
    static const std::string found = [] {
        std::ifstream in("/etc/os-release");
        std::string name;
        for (std::string line; std::getline(in, line);)
            if (line.starts_with("LOGO=")) {
                name = line.substr(5);
                std::erase(name, '"');
            }
        if (name.empty()) return std::string();
        // As an outline it can be tinted: SVG only
        for (const std::string& path : {Images::get().findIcon(name, 64), "/usr/share/pixmaps/" + name + ".svg"})
            if (path.ends_with(".svg")) return path;
        return std::string();
    }();
    return found.empty() ? "apps" : found;
}

// Live download/upload for the bar
class NetSpeed : public Flex {
public:
    NetSpeed() : Flex(barVertical() ? Direction::Column : Direction::Row), m_compact(barVertical()) {
        setSpacing(m_compact ? 2 : 6);
        m_down = add<Label>();
        m_up = add<Label>();
        for (Label* l : {m_down, m_up}) {
            l->setSize(m_compact ? 10 : 12)->setWeight(500);
            l->minWidth = m_compact ? 0 : 64;
        }
        m_lease = Network::get().watchTraffic();
        m_conn = Network::get().detailsChanged.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        const auto& d = Network::get().details();
        // A side bar's narrow: "1.2M" for "1.2 MB/s"
        auto rate = [this](double r) {
            std::string t = panels::formatRate(r);
            if (!m_compact) return " " + t;
            std::erase(t, ' ');
            if (const auto at = t.find("B/s"); at != std::string::npos) t.erase(at);
            return t;
        };
        m_down->setText("↓" + rate(d.rxRate));
        m_up->setText("↑" + rate(d.txRate));
    }
    bool m_compact;
    Label *m_down, *m_up;
    Network::Lease m_lease;
    Connection m_conn;
};

// A capsule-sized button: icon, optional text, opens a panel. On a side bar
// the text sits small under the icon
class Chip : public Button {
public:
    Chip(const std::string& icon, std::function<void()> click)
        : Button(barVertical() ? Direction::Column : Direction::Row), m_vertical(barVertical()) {
        pill(this)->setPadding(m_vertical ? Insets{2, 6} : Insets{8, 4})->setSpacing(m_vertical ? 2 : 6);
        pressedRadius = 8;
        m_icon = add<Icon>(icon, 20);
        m_icon->setColor("on_surface", false);
        m_text = add<Label>();
        m_text->setSize(m_vertical ? 11 : 13)->setWeight(500);
        if (m_vertical) m_text->setMaxWidth(36)->setMaxLines(2)->setCentered(true);
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
    bool m_vertical;
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
        std::strftime(buf, sizeof buf, m_vertical ? "%-d" : "%a %-d %b", &tm);
        set("calendar_month", buf);
        std::strftime(buf, sizeof buf, "%A %-d %B", &tm);
        tooltip = buf;
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
        const size_t count = n.current().size();
        set(n.doNotDisturb() ? "notifications_off" : count ? "notifications_unread" : "notifications",
            count ? std::to_string(count) : "");
    }
    Connection m_conn;
};

// Today's screen time (Digital Wellbeing), to the minute
class ScreenTimeChip : public Chip {
public:
    explicit ScreenTimeChip(std::function<void()> click) : Chip("digital_wellbeing", std::move(click)) {
        m_conn = Wellbeing::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        const int t = Wellbeing::get().today().total(), h = t / 3600, m = t / 60 % 60;
        const std::string full = h ? std::format("{}h {:02}m", h, m) : std::format("{}m", m);
        // A side bar's narrow: the hours alone, once there are some
        set("digital_wellbeing", m_vertical && h ? std::format("{}h", h) : full);
        tooltip = std::format("Screen time today: {}{}", full, Wellbeing::get().focus() ? " · Focus mode on" : "");
    }
    Connection m_conn;
};

// A VPN's name while one is up; the key alone (dimmed) while none is
class VpnChip : public Chip {
public:
    explicit VpnChip(std::function<void()> click) : Chip("vpn_key", std::move(click)) {
        m_conn = Vpn::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& v = Vpn::get();
        const bool on = v.anyActive();
        set(on ? "vpn_lock" : "vpn_key", !v.busy().empty() ? "…" : on && !m_vertical ? v.activeName() : "");
        m_text->setMaxWidth(140);
        m_icon->setFill(on)->setColor(on ? "primary" : "on_surface_variant");
        tooltip = on ? "VPN: " + v.activeName() + " (click to disconnect, right-click for settings)"
                     : "VPN off (click to connect, right-click for settings)";
    }
    Connection m_conn;
};

// Quick Share: its symbol, a transfer's progress while one runs
class QuickShareChip : public Chip {
public:
    explicit QuickShareChip(std::function<void()> click) : Chip(quickshare::QuickShare::symbol(), std::move(click)) {
        m_conn = quickshare::QuickShare::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& q = quickshare::QuickShare::get();
        std::string progress;
        for (const auto& [_, t] : q.transfers())
            if (t.state == quickshare::QuickShare::State::Receiving && t.total > 0)
                progress = std::format("{}%", t.received * 100 / t.total);
        set(quickshare::QuickShare::symbol(), progress);
        m_icon->setColor(q.receiving() ? "on_surface" : "on_surface_variant");
        tooltip = q.receiving() ? "Quick Share: visible as " + q.deviceName() + " (click to send files)" : "Quick Share (click to send files)";
    }
    Connection m_conn;
};

// A switch as a chip: lit while on (Focus mode, Bedtime)
class ToggleChip : public Chip {
public:
    ToggleChip(const std::string& icon, std::function<bool()> on, std::string name, Signal<>& changed)
        : Chip(icon, {}), m_on(std::move(on)), m_name(std::move(name)), m_iconName(icon) {
        m_conn = changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        const bool on = m_on();
        set(m_iconName, on ? "On" : "");
        m_icon->setFill(on)->setColor(on ? "on_primary" : "on_surface");
        m_text->setColor(on ? "on_primary" : "on_surface");
        setBackground(on ? ColorRef("primary") : ColorRef::none());
        tooltip = m_name + (on ? ": on (click to turn off)" : ": off (click to turn on)");
    }
    std::function<bool()> m_on;
    std::string m_name, m_iconName;
    Connection m_conn;
};

// Printing: the jobs waiting, while the bar shows it (it watches the queue)
class PrintersChip : public Chip {
public:
    explicit PrintersChip(std::function<void()> click) : Chip("print", std::move(click)) {
        m_lease = Printers::get().watch();
        m_conn = Printers::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& p = Printers::get();
        const size_t n = p.jobs().size();
        bool paused = false;
        for (const auto& x : p.printers()) paused = paused || (x.isDefault && x.state == "stopped");
        set(paused ? "print_disabled" : n ? "print_connect" : "print", n ? std::to_string(n) : "");
        m_icon->setColor(paused ? "error" : "on_surface");
        tooltip = "Printers: " + p.summary();
    }
    Printers::Lease m_lease;
    Connection m_conn;
};

// The firewall: a shield, struck through while it's off
class FirewallChip : public Chip {
public:
    explicit FirewallChip(std::function<void()> click) : Chip("shield_lock", std::move(click)) {
        m_conn = Firewall::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& f = Firewall::get();
        const bool on = f.installed() && f.enabled();
        set(on ? "shield_lock" : "remove_moderator", "");
        m_icon->setColor(on ? "on_surface" : "error");
        tooltip = !f.installed() ? "No firewall" : on ? "Firewall on" : "Firewall off";
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
        set("memory", m_vertical ? std::format("{:.0f}%\n{:.0f}%", s.cpu(), mem) : std::format("{:.0f}%  ·  {:.0f}%", s.cpu(), mem));
        tooltip = std::format("CPU {:.0f}% · memory {:.0f}%", s.cpu(), mem);
    }
    SystemStats::Lease m_lease;
    Connection m_conn;
};

} // namespace

const std::vector<Bar::Module>& Bar::modules() {
    static const std::string quickShare = quickshare::QuickShare::symbol(); // its own symbol, as a path
    static const std::vector<Module> all = {
        {"logo", "token", "Logo", "Your system's logo; opens Spotlight"},
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
        {"screentime", "digital_wellbeing", "Screen time", "Today's total; opens Digital Wellbeing"},
        {"focus", "self_improvement", "Focus mode", "Pauses distracting apps; click to switch"},
        {"bedtime", "bedtime", "Bedtime", "Greyscale and Do not disturb; click to switch"},
        {"vpn", "vpn_key", "VPN", "The VPN in use; click to connect or disconnect"},
        {"quickshare", quickShare.c_str(), "Quick Share", "Transfers' progress; opens Send files"},
        {"firewall", "shield_lock", "Firewall", "On or off; opens Firewall settings"},
        {"printers", "print", "Printers", "Jobs waiting; opens Printers"},
        {"quicksettings", "tune", "Quick Settings", "Opens Quick Settings"},
        {"status", "wifi", "Status icons", "Network, Bluetooth, volume; opens Quick Settings"},
        {"battery", "battery_full", "Battery", "Charge level (hidden on desktops)"},
        {"power", "power_settings_new", "Power", "Opens the power menu"},
    };
    return all;
}

const char* Bar::layoutKey() {
    return barVertical() ? "/statusBar/layoutSide" : "/statusBar/layout";
}

Bar::Layout Bar::layout() {
    Layout out;
    auto j = Config::get().value<nlohmann::json>(layoutKey(), nlohmann::json());
    // A side bar without its own yet: the top bar's, down the side
    if (!j.is_object() && barVertical()) j = Config::get().value<nlohmann::json>("/statusBar/layout", nlohmann::json());
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
    Config::get().set(layoutKey(), j);
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
    const bool vertical = barVertical();
    if (id == "workspaces") {
        auto* ws = zone->add<Workspaces>(m_output->name, vertical);
        if (const auto colour = filled ? pillColour() : std::nullopt) {
            shapePill(ws->setBackground(*colour, false));
            ws->blurBehind = barBlur();
        }
    } else if (id == "title") {
        capsule(zone, filled)->add<WindowTitle>(vertical);
    } else if (id == "island") {
        zone->add<Island>(m_openPopup);
    } else if (id == "clock") {
        // As the island's clock does: the calendar (the dashboard) on a
        // click, the notifications on a right click
        auto* c = capsule(zone, filled);
        c->setPadding(vertical ? Insets{0, 2} : Insets{2, 0});
        auto* b = c->add<Button>();
        pill(b)->setPadding(vertical ? Insets{2, 8} : Insets{10, 4});
        b->pressedRadius = 8;
        b->setJustify(Align::Center);
        auto* clock = b->add<Clock>("bar");
        if (vertical) clock->setStacked(true)->setWeight(600);
        b->tooltip = "Calendar";
        b->onClicked = [this, b](uint32_t button) {
            if (button == BTN_LEFT) m_openPopup("dashboard", alongBar(b));
            else if (button == BTN_RIGHT) m_openPopup("notifications", alongBar(b));
        };
    } else if (id == "logo") {
        // Caelestia's corner: the system's logo, unboxed, in the accent
        auto* b = zone->add<Button>();
        pill(b)->setPadding(Insets(5));
        b->pressedRadius = 8;
        b->add<Icon>(logoIcon(), 22)->setColor("primary", false);
        b->tooltip = "Apps";
        b->onClicked = [this, b](uint32_t button) {
            if (button == BTN_LEFT) m_openPopup("launcher", alongBar(b));
        };
    } else if (id == "netspeed") {
        capsule(zone, filled)->add<NetSpeed>();
    } else if (id == "tray") {
        auto* c = capsule(zone, filled);
        c->setPadding(vertical ? Insets{0, 4} : Insets{4, 0});
        c->add<TrayIcons>(m_openPopup, vertical);
    } else if (id == "status") {
        auto* c = capsule(zone, filled);
        c->setPadding(vertical ? Insets{0, 4} : Insets{4, 0});
        c->add<StatusIcons>(m_openPopup, vertical);
    } else if (id == "battery") {
        // The button fills the capsule (2 px in all round) so its hover
        // pill follows the capsule's shape
        auto* c = capsule(zone, filled);
        c->setPadding(vertical ? Insets{0, 2} : Insets{2, 0});
        c->add<BatteryStatus>(m_openPopup, vertical);
    } else if (id == "media") {
        auto* c = capsule(zone, filled);
        if (vertical) c->setPadding({0, 2});
        c->add<Media>(vertical);
    } else if (id == "screentime" || id == "vpn" || id == "quickshare" || id == "firewall" || id == "focus" || id == "bedtime" ||
               id == "printers") {
        auto* c = capsule(zone, filled);
        c->setPadding(vertical ? Insets{0, 4} : Insets{4, 0});
        auto open = [this](const std::string& name, Node* from) { m_openPopup(name, alongBar(from)); };
        Chip* chip = nullptr;
        if (id == "screentime") {
            chip = c->add<ScreenTimeChip>(std::function<void()>{});
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) open("settings wellbeing", chip);
            };
        } else if (id == "vpn") {
            chip = c->add<VpnChip>(std::function<void()>{});
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) Vpn::get().toggleLast();
                else if (b == BTN_RIGHT) open("settings vpn", chip);
            };
        } else if (id == "quickshare") {
            chip = c->add<QuickShareChip>(std::function<void()>{});
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) open("quickshare-send", chip);
                else if (b == BTN_RIGHT) open("settings bluetooth", chip);
            };
        } else if (id == "printers") {
            chip = c->add<PrintersChip>(std::function<void()>{});
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) open("settings printers", chip);
            };
        } else if (id == "firewall") {
            chip = c->add<FirewallChip>(std::function<void()>{});
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) open("settings firewall", chip);
            };
        } else if (id == "focus") {
            chip = c->add<ToggleChip>("self_improvement", [] { return Wellbeing::get().focus(); }, "Focus mode", Wellbeing::get().changed);
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) Wellbeing::get().setFocus(!Wellbeing::get().focus());
                else if (b == BTN_RIGHT) open("settings wellbeing", chip);
            };
        } else {
            chip = c->add<ToggleChip>("bedtime", [] { return Wellbeing::get().bedtime(); }, "Bedtime", Wellbeing::get().changed);
            chip->onClicked = [open, chip](uint32_t b) {
                if (b == BTN_LEFT) Wellbeing::get().setBedtimeNow(!Wellbeing::get().bedtime());
                else if (b == BTN_RIGHT) open("settings wellbeing", chip);
            };
        }
    } else if (id == "weather" || id == "calendar" || id == "notifications" || id == "sysmon") {
        auto* c = capsule(zone, filled);
        c->setPadding(vertical ? Insets{0, 4} : Insets{4, 0});
        Chip* chip = nullptr;
        auto open = [this](const std::string& name, Node* from) { m_openPopup(name, alongBar(from)); };
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
        c->setPadding(vertical ? Insets{0, 4} : Insets{4, 0});
        auto* b = c->add<Button>();
        pill(b)->setPadding(Insets(5));
        b->pressedRadius = 6;
        // Power stands out, as in Caelestia's bar
        b->add<Icon>(it->icon, 20)->setColor(id == "power" && vertical ? "error" : "on_surface", false);
        b->tooltip = it->name;
        b->onClicked = [this, b, id](uint32_t button) {
            if (button == BTN_LEFT) m_openPopup(id, alongBar(b));
        };
    }
}

void Bar::rebuild() {
    // backgroundStyle: 0 each module in its own pill, 1 one bar-wide surface,
    // 2 a floating bar (inset from the edges, rounded ends), 3 framed: the
    // bar's surface runs round the screen (Frame draws it). The pills are
    // filled in the pill colour in any of them (Pill highlight)
    const auto& config = Config::get();
    const int style = config.value<int>("/statusBar/backgroundStyle", 0);
    const bool filled = pillColour().has_value();
    const bool vertical = barVertical();
    // Editing in place needs a bar's length across: a side bar is edited in
    // Settings (Shell opens it)
    const bool editingHere = editing && !vertical;

    auto root = std::make_unique<BarLayout>();
    auto* bg = root.get();
    bg->vertical = vertical;
    bg->textArea = Theme::AreaBar;
    // Right click on empty bar space: edit the bar in place
    bg->onContextMenu = [] {
        if (panels::editBar) panels::editBar(true);
    };
    const auto zones = layout();
    for (size_t z = 0; z < 3; ++z) {
        auto* zone = bg->add<Flex>(vertical ? Flex::Direction::Column : Flex::Direction::Row);
        zone->setSpacing(8);
        if (!editingHere) {
            for (const auto& g : zones[z]) {
                if (g.size() == 1) {
                    addModule(zone, g.front(), filled);
                    continue;
                }
                // A group: one capsule around all of its widgets
                auto* cap = capsule(zone, filled);
                cap->setPadding(vertical ? Insets{0, 4} : Insets{4, 0})->setSpacing(2);
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
    if (style == 1 || style == 2) {
        // One surface under everything: edge to edge, or floating
        auto frame = std::make_unique<BarFrame>();
        frame->floating = style == 2;
        frame->edge = barEdge();
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
    if (vertical) m_surface->setPlacement(anchor(), totalHeight(), 0);
    else m_surface->setPlacement(anchor(), 0, totalHeight());
    m_surface->setExclusiveZone(totalHeight());
}

void Bar::setVisible(bool visible) {
    m_surface->setVisible(visible);
}

} // namespace m3::shell
