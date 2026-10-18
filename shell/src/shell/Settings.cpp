// The Settings app: sidebar navigation and pages. Settings are read from and
// written to config.json (shared with the QML shell) through Config.
#include "build_config.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Audio.h"
#include "services/Clipboard.h"
#include "services/Hyprland.h"
#include "services/Keybinds.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Packages.h"
#include "services/Search.h"
#include "services/SystemStats.h"
#include "services/Wallpaper.h"
#include "services/Weather.h"
#include "shell/Dock.h"
#include "shell/LockLayout.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/bar/Bar.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cmath>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// ── Building blocks ──
//
// M3 Expressive: lists are segmented (each row its own surface, a hairline
// apart, large outer and small inner corners), choices are connected button
// groups, and pressed controls spring into a tighter shape.

constexpr float kOuter = 24, kInner = 6;

// Rebuilds a page by id (after a reset, so its controls show the defaults)
std::function<void(const std::string&)> reshow;

// The rows of one group. Their backgrounds are painted here, shaped by their
// place in the group, so rows added or hidden later reshape the stack.
class Segments : public Flex {
public:
    Segments() : Flex(Direction::Column) { setAlignItems(Align::Stretch)->setSpacing(2); }

protected:
    void doArrange() override {
        Flex::doArrange();
        const auto rows = shownRows();
        for (size_t i = 0; i < rows.size(); ++i) {
            const Radii want = shape(i, rows.size());
            const Radii& has = rows[i]->radius();
            // Hover and press layers follow the segment's corners
            if (has.tl != want.tl || has.bl != want.bl) rows[i]->setRadius(want);
        }
    }
    void doPaint(Renderer& r) override {
        const Color bg = ColorRef("surface_container_high", std::min(1.0f, Theme::get().panelOpacity() + 0.06f)).resolve();
        const float k = Theme::get().roundness();
        const auto rows = shownRows();
        for (size_t i = 0; i < rows.size(); ++i) {
            const Radii s = shape(i, rows.size());
            const float half = std::min(rows[i]->rect().w, rows[i]->rect().h) / 2;
            auto f = [&](float v) { return std::min(v * k, half); };
            r.rect(rows[i]->rect(), bg, {f(s.tl), f(s.tr), f(s.br), f(s.bl)});
        }
    }

private:
    std::vector<Flex*> shownRows() const {
        std::vector<Flex*> out;
        for (const auto& c : children())
            if (c->shown())
                if (auto* f = dynamic_cast<Flex*>(c.get())) out.push_back(f);
        return out;
    }
    static Radii shape(size_t i, size_t n) {
        const float top = i == 0 ? kOuter : kInner, bottom = i + 1 == n ? kOuter : kInner;
        return {top, top, bottom, bottom};
    }
};

// Children left to right, wrapping onto new lines (chips)
class Flow : public Node {
public:
    float spacing = 6;

protected:
    SizeF doMeasure(SizeF available) override { return place(available.w, false); }
    void doArrange() override { place(rect().w, true); }

private:
    SizeF place(float width, bool arrange) {
        float x = 0, y = 0, line = 0, widest = 0;
        for (auto& c : children()) {
            if (!c->shown()) continue;
            const SizeF s = c->measure({width, 1e6f});
            if (x > 0 && x + s.w > width) {
                x = 0;
                y += line + spacing;
                line = 0;
            }
            if (arrange) c->arrange({rect().x + x, rect().y + y, s.w, s.h});
            x += s.w + spacing;
            line = std::max(line, s.h);
            widest = std::max(widest, x - spacing);
        }
        return {widest, y + line};
    }
};

// A titled group of rows
Flex* group(Node* page, const std::string& title, const std::string& subtitle = {}) {
    auto* head = page->add<Flex>(Flex::Direction::Column);
    head->setAlignItems(Align::Start)->setSpacing(2)->setPadding({8, 10, 8, 2});
    head->add<Label>(title)->setSize(14)->setWeight(600)->setColor("primary", false);
    if (!subtitle.empty()) head->add<Label>(subtitle)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(640)->setMaxLines(2);
    return page->add<Segments>();
}

// A group holding one block of controls on a single surface (popup
// layouts reused inside Settings)
Flex* panelGroup(Node* page, const std::string& title, const std::string& subtitle = {}) {
    auto* inner = group(page, title, subtitle)->add<Flex>(Flex::Direction::Column);
    inner->setAlignItems(Align::Stretch)->setSpacing(12)->setPadding({20, 16});
    return inner;
}

// One row: icon, title and subtitle, and a trailing slot for the control
struct Row {
    Flex* row;
    Label* title;
    Label* subtitle;
};

Row row(Node* group, const std::string& icon, const std::string& title, const std::string& subtitle = {}) {
    auto* r = group->add<Flex>(Flex::Direction::Row);
    r->setPadding({20, 12})->setSpacing(16);
    r->minHeight = 60;
    if (!icon.empty()) r->add<Icon>(icon, 22)->setColor("on_surface_variant", false);
    auto* texts = r->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(1);
    texts->grow = true;
    auto* t = texts->add<Label>(title);
    t->setSize(15)->setWeight(500)->setMaxWidth(420);
    auto* s = texts->add<Label>(subtitle);
    s->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(440)->setMaxLines(2);
    s->setVisible(!subtitle.empty());
    return {r, t, s};
}

Button* pillButton(Node* parent, const std::string& label, bool primary, std::function<void()> fn, const std::string& icon = {}) {
    auto* b = parent->add<Button>();
    pill(b->setBackground(primary ? "primary" : "secondary_container", false))->setPadding({18, 10})->setSpacing(8);
    b->pressedRadius = 10;
    if (!icon.empty()) b->add<Icon>(icon, 18)->setColor(primary ? "on_primary" : "on_secondary_container", false);
    b->add<Label>(label)->setSize(14)->setWeight(500)->setColor(primary ? "on_primary" : "on_secondary_container", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT && fn) fn();
    };
    return b;
}

// Config-bound controls

void switchRow(Node* g, const std::string& icon, const std::string& title, const std::string& sub,
               const std::string& pointer, bool fallback, std::function<void(bool)> after = {}) {
    auto r = row(g, icon, title, sub);
    auto* sw = r.row->add<Switch>();
    sw->setChecked(Config::get().value<bool>(pointer, fallback), false);
    sw->onToggled = [pointer, after](bool on) {
        Config::get().set(pointer, on);
        if (after) after(on);
    };
}

// M3 Expressive connected button group: segments a hairline apart with tight
// inner corners; the selected one turns primary and rounds off completely.
template <typename T>
Flex* connectedGroup(Node* parent, T current, std::vector<std::pair<T, std::string>> options, std::function<void(T)> pick) {
    auto* chips = parent->add<Flex>(Flex::Direction::Row);
    chips->setSpacing(2);
    auto buttons = std::make_shared<std::vector<std::pair<T, Button*>>>();
    auto paint = [buttons](const T& selected, bool animate) {
        const size_t n = buttons->size();
        for (size_t i = 0; i < n; ++i) {
            auto& [val, btn] = (*buttons)[i];
            const bool on = val == selected;
            const float l = on || i == 0 ? 100 : 8, rr = on || i + 1 == n ? 100 : 8;
            btn->setRadius({l, rr, rr, l});
            btn->setBackground(on ? ColorRef("primary") : ColorRef("surface_container_highest"), animate);
            for (auto& c : btn->children())
                if (auto* lab = dynamic_cast<Label*>(c.get())) lab->setColor(on ? "on_primary" : "on_surface", animate);
        }
    };
    for (const auto& [value, label] : options) {
        auto* b = chips->add<Button>();
        b->setPadding({14, 9})->setSpacing(6);
        b->pressedRadius = 4;
        b->add<Label>(label)->setSize(13)->setWeight(500);
        buttons->push_back({value, b});
        b->onClicked = [=, v = value](uint32_t) {
            paint(v, true);
            if (pick) pick(v);
        };
    }
    paint(current, false);
    return chips;
}

template <typename T>
void choiceRow(Node* g, const std::string& icon, const std::string& title, const std::string& sub, const std::string& pointer,
               T fallback, std::vector<std::pair<T, std::string>> options, std::function<void(T)> after = {}) {
    auto r = row(g, icon, title, sub);
    connectedGroup<T>(r.row, Config::get().value<T>(pointer, fallback), std::move(options), [pointer, after](T v) {
        Config::get().set(pointer, v);
        if (after) after(v);
    });
}

void sliderRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer, double fallback,
               double min, double max, double step, const std::function<std::string(double)>& format,
               std::function<void(double)> after = {}) {
    auto r = row(g, icon, title, format(Config::get().value<double>(pointer, fallback)));
    auto* sl = r.row->add<Slider>();
    sl->fixedWidth = 240;
    sl->setRange(static_cast<float>(min), static_cast<float>(max))->setStep(static_cast<float>(step / (max - min)));
    sl->setValue(static_cast<float>(Config::get().value<double>(pointer, fallback)), false);
    auto* sub = r.subtitle;
    sub->setVisible(true);
    sl->onChanged = [sub, format, step](float v) { sub->setText(format(std::round(v / step) * step)); };
    sl->onCommitted = [pointer, step, after](float v) {
        const double snapped = std::round(v / step) * step;
        Config::get().set(pointer, snapped);
        if (after) after(snapped);
    };
}

void textRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer, const std::string& fallback) {
    auto r = row(g, icon, title, "Enter to apply");
    auto* f = r.row->add<TextField>();
    f->fixedWidth = 280;
    f->setText(Config::get().value<std::string>(pointer, fallback));
    f->onSubmit = [pointer](const std::string& t) { Config::get().set(pointer, t); };
}

std::string percent(double v) {
    return std::format("{:.0f}%", v * 100);
}

std::string readFirstLine(const std::string& path, const std::string& prefix = {}) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
        if (prefix.empty() || line.starts_with(prefix)) return prefix.empty() ? line : line.substr(prefix.size());
    return {};
}

// Re-run Matugen (after scheme, mode or accent changes)
void recolour() {
    Wallpaper::get().recolour();
}

// ── Pages ──

void networkPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(6);

    // The connected network's own segment: its name, then live speeds on
    // tonal tiles
    auto* g = group(live, "Connection");
    auto* current = g->add<Flex>(Flex::Direction::Column);
    current->setAlignItems(Align::Stretch)->setSpacing(4)->setPadding({0, 0, 0, 16});
    auto status = row(current, "wifi", "Not connected");
    status.row->setPadding({20, 12, 20, 4});
    auto* hero = current->add<Flex>(Flex::Direction::Row);
    hero->setSpacing(8)->setAlignItems(Align::Stretch)->setPadding({16, 0});
    auto stat = [hero](const char* icon, const char* title, const char* bg, const char* fg) {
        auto* t = hero->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(2)->setPadding({16, 12})->setRadius(Radii(20))->setBackground(bg, false);
        t->grow = true;
        auto* top = t->add<Flex>(Flex::Direction::Row);
        top->setSpacing(6);
        top->add<Icon>(icon, 18)->setColor(fg, false);
        top->add<Label>(title)->setSize(12)->setWeight(500)->setColor(fg, false);
        auto* v = t->add<Label>("–");
        v->setSize(24)->setWeight(600)->setColor(fg, false);
        return v;
    };
    auto* down = stat("arrow_downward", "Download", "primary_container", "on_primary_container");
    auto* up = stat("arrow_upward", "Upload", "tertiary_container", "on_tertiary_container");
    auto* linkTile = stat("speed", "Link speed", "secondary_container", "on_secondary_container");

    struct Detail {
        Row row;
        Label* value;
    };
    auto detail = [g](const char* icon, const char* title, const char* sub = "") {
        auto r = row(g, icon, title, sub);
        auto* v = r.row->add<Label>();
        v->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(300);
        return Detail{r, v};
    };
    auto band = detail("radio", "Band");
    auto security = detail("shield_lock", "Security");
    auto ipv4 = detail("lan", "IPv4 address");
    auto gateway = detail("router", "Gateway");
    auto dns = detail("dns", "DNS");
    auto ipv6 = detail("public", "IPv6 address");
    auto mac = detail("fingerprint", "Hardware address");
    auto iface = detail("settings_ethernet", "Interface");
    auto used = detail("data_usage", "Data used", "Since the interface came up");

    auto lease = Network::get().watchTraffic();
    live->sync = [=] {
        (void)lease; // sampling runs while the page is open
        auto& n = Network::get();
        const auto& d = n.details();
        const bool on = n.kind() != Network::Kind::None;
        hero->setVisible(on);
        current->setPadding({0, 0, 0, on ? 16.0f : 0.0f});
        down->setText(formatRate(d.rxRate));
        up->setText(formatRate(d.txRate));
        linkTile->setText(d.linkRate > 0 ? std::format("{} Mb/s", d.linkRate) : "–");
        static_cast<Icon*>(status.row->children()[0].get())->setText(n.icon());
        status.title->setText(!on ? (n.connecting() ? "Connecting…" : "Not connected") : n.name());
        status.subtitle->setText(n.kind() == Network::Kind::Wifi ? std::format("Wi-Fi · signal {}%", n.strength())
                                 : n.kind() == Network::Kind::Ethernet ? "Ethernet" : on ? "Connected" : "");
        status.subtitle->setVisible(!status.subtitle->text().empty());
        auto set = [on](const Detail& x, const std::string& v) {
            x.value->setText(v);
            x.row.row->setVisible(on && !v.empty());
        };
        std::string rate = d.band();
        if (d.maxRate > 0) rate += (rate.empty() ? "" : " · ") + std::format("up to {} Mb/s", d.maxRate);
        set(band, n.kind() == Network::Kind::Wifi ? rate : "");
        set(security, d.security);
        set(ipv4, d.ipv4);
        set(gateway, d.gateway);
        set(dns, d.dns);
        set(ipv6, d.ipv6);
        set(mac, d.mac);
        set(iface, d.iface);
        set(used, d.iface.empty() ? "" : std::format("↓ {}  ·  ↑ {}", formatBytes(d.rxTotal), formatBytes(d.txTotal)));
    };
    live->watch(Network::get().changed);
    live->watch(Network::get().detailsChanged);
    live->sync();

    wifiView(panelGroup(page, "Wi-Fi networks", "Tap one to connect; saved networks connect at once"), {}, false);
}

void bluetoothPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    auto* g = group(live, "Bluetooth");
    auto power = row(g, "bluetooth", "Bluetooth");
    auto* powerSw = power.row->add<Switch>();
    powerSw->onToggled = [](bool on) { Bluetooth::get().setPowered(on); };
    auto discover = row(g, "bluetooth_searching", "Discover nearby devices", "Makes new devices show up below");
    auto* discoverSw = discover.row->add<Switch>();
    discoverSw->onToggled = [](bool on) { Bluetooth::get().setDiscovering(on); };
    auto* list = group(live, "Devices");

    live->sync = [=] {
        auto& b = Bluetooth::get();
        powerSw->setChecked(b.powered());
        discoverSw->setChecked(b.discovering());
        power.subtitle->setText(!b.available() ? "No Bluetooth adapter" : b.powered() ? "On" : "Off");
        power.subtitle->setVisible(true);
        list->clearChildren();
        if (b.devices().empty()) {
            auto r = row(list, "", b.powered() ? "No devices yet. Turn on discovery to find some." : "Bluetooth is off");
            r.title->setColor("on_surface_variant", false);
            return;
        }
        for (const auto& d : b.devices()) {
            const std::string glyph = d.icon.find("headset") != std::string::npos || d.icon.find("headphone") != std::string::npos ? "headphones"
                                    : d.icon.find("audio") != std::string::npos ? "speaker"
                                    : d.icon.find("phone") != std::string::npos ? "smartphone"
                                    : d.icon.find("keyboard") != std::string::npos ? "keyboard"
                                    : d.icon.find("mouse") != std::string::npos ? "mouse" : "bluetooth";
            auto r = row(list, glyph, d.name, d.connected ? "Connected" : d.paired ? "Paired" : "Available");
            const std::string path = d.path;
            if (d.connected) pillButton(r.row, "Disconnect", false, [path] { Bluetooth::get().disconnect(path); });
            else if (d.paired) pillButton(r.row, "Connect", true, [path] { Bluetooth::get().connect(path); });
            else pillButton(r.row, "Pair", true, [path] { Bluetooth::get().pair(path); });
            if (d.paired) iconButton(r.row, "delete", [path] { Bluetooth::get().forget(path); });
        }
    };
    live->watch(Bluetooth::get().changed);
    live->sync();
}

void soundPage(Flex* page, Popup& popup) {
    audio(panelGroup(page, "Output and input", "Volume, balance, channels and devices"), popup);
}

void displayPage(Flex* page, Popup&) {
    auto* g = group(page, "Brightness");
    if (Brightness::get().available()) {
        auto r = row(g, "brightness_medium", "Screen brightness");
        auto* sl = r.row->add<Slider>();
        sl->fixedWidth = 240;
        sl->setValue(Brightness::get().value(), false);
        sl->onChanged = [](float v) { Brightness::get().set(v); };
    } else {
        row(g, "brightness_medium", "No adjustable backlight", "External monitors are controlled from their own menus");
    }

    auto* night = group(page, "Night light", "Warmer colours in the evening, via hyprsunset");
    auto applyNight = [] {
        if (Config::get().value<bool>("/nightLight/enabled", false))
            spawn(std::format("pkill -x hyprsunset; hyprsunset -t {}", Config::get().value<int>("/nightLight/temperature", 4500)));
        else
            spawn("pkill -x hyprsunset");
    };
    switchRow(night, "nightlight", "Night light", "", "/nightLight/enabled", false, [applyNight](bool) { applyNight(); });
    sliderRow(night, "thermostat", "Colour temperature", "/nightLight/temperature", 4500, 2500, 6500, 100,
              [](double v) { return std::format("{:.0f} K", v); }, [applyNight](double) { applyNight(); });

    auto* mons = group(page, "Monitors", "Arrangement and modes are set in your Hyprland config (monitors.lua)");
    if (auto j = Hyprland::get().requestJson("monitors"); j && j->is_array()) {
        for (const auto& m : *j)
            row(mons, "monitor", m.value("description", "").empty() ? m.value("name", "") : std::format("{}  ·  {}", m.value("name", ""), m.value("description", "")),
                std::format("{}×{} at {:.0f} Hz · scale {:.2f}{}", m.value("width", 0), m.value("height", 0),
                            m.value("refreshRate", 0.0), m.value("scale", 1.0), m.value("focused", false) ? " · focused" : ""));
    } else {
        row(mons, "monitor", "Monitor details need Hyprland");
    }
}

void appearancePage(Flex* page, Popup& popup) {
    auto* wp = group(page, "Wallpaper");
    auto current = row(wp, "", "Current wallpaper", Wallpaper::get().path().empty() ? "None" : Wallpaper::get().path());
    // Thumbnail in front of the text
    const std::string path = Wallpaper::get().path();
    auto* thumb = std::make_unique<ImageView>(96, 54, [path](float scale, int& w, int& h) -> GLuint {
        if (path.empty()) return 0;
        auto* t = Images::get().picture(path, static_cast<int>(96 * scale), static_cast<int>(54 * scale), true);
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    }).release();
    thumb->cover = true;
    current.row->adopt(std::unique_ptr<Node>(thumb), 0);
    pillButton(current.row, "Choose…", true, [&popup] {
        popup.close();
        if (open) open("wallpapers");
    }, "wallpaper");
    pillButton(current.row, "Next", false, [] { Wallpaper::get().next(1); });
    choiceRow<std::string>(wp, "animation", "Transition", "", "/background/wallpaperTransition", "random",
                           {{"random", "Random"}, {"fade", "Fade"}, {"wipe", "Wipe"}, {"grow", "Grow"}});
    switchRow(wp, "schedule", "Desktop clock", "A large clock on the wallpaper", "/desktop/clock", true);

    auto* colour = group(page, "Colours", "Material You colours come from the wallpaper, or from an accent you pick");
    switchRow(colour, "dark_mode", "Dark theme", "", "/background/darkmode", true, [](bool) { recolour(); });
    choiceRow<std::string>(colour, "palette", "Scheme", "", "/background/matugenScheme", "scheme-content",
                           {{"scheme-content", "Content"}, {"scheme-tonal-spot", "Tonal"}, {"scheme-expressive", "Expressive"},
                            {"scheme-vibrant", "Vibrant"}, {"scheme-monochrome", "Mono"}},
                           [](std::string) { recolour(); });

    // Accent: the wallpaper, or a seed colour for Matugen
    auto accent = row(colour, "colorize", "Accent", "");
    auto* swatches = accent.row->add<Flex>(Flex::Direction::Row);
    swatches->setSpacing(6);
    const std::vector<std::string> seeds = {"", "#6750a4", "#0061a4", "#006d3b", "#8b5000", "#b3261e", "#984061", "#006a6a"};
    auto chosen = std::make_shared<std::string>(Config::get().value<std::string>("/appearance/accent", ""));
    auto dots = std::make_shared<std::vector<Button*>>();
    auto mark = [dots, seeds, chosen] {
        for (size_t i = 0; i < dots->size(); ++i) (*dots)[i]->setBorder(seeds[i] == *chosen ? 3 : 0, "on_surface");
    };
    for (const auto& seed : seeds) {
        auto* d = swatches->add<Button>();
        pill(d)->fixedWidth = 30;
        d->fixedHeight = 30;
        d->pressedRadius = 6;
        if (seed.empty()) {
            // "From the wallpaper"
            d->setBackground("surface_container_highest", false)->setJustify(Align::Center);
            d->add<Icon>("wallpaper", 16)->setColor("on_surface_variant", false);
        } else {
            d->setBackground(ColorRef(*Color::fromHex(seed)), false);
        }
        d->onClicked = [seed, chosen, mark](uint32_t) {
            *chosen = seed;
            mark();
            Config::get().set("/appearance/accent", seed);
            recolour();
        };
        dots->push_back(d);
    }
    mark();

    auto* fonts = group(page, "Fonts");
    textRow(fonts, "text_fields", "Interface", "/appearance/fonts/main", "Google Sans Flex");
    textRow(fonts, "code", "Monospace", "/appearance/fonts/monospace", "JetBrains Mono NF");
}

void stylePage(Flex* page, Popup&) {
    auto* fx = group(page, "Blur & transparency", "Panels let the desktop show through, softened by the compositor's blur");
    switchRow(fx, "blur_on", "Blur", Hyprland::get().available() ? "Behind the bar, panels, notifications and OSD"
                                                                  : "Needs Hyprland, or a compositor with ext-background-effect",
              "/appearance/blur/enabled", true);
    sliderRow(fx, "blur_medium", "Blur strength", "/appearance/blur/size", 8, 2, 20, 1,
              [](double v) { return std::format("Radius {:.0f} · applies to windows too", v); });
    sliderRow(fx, "toolbar", "Bar opacity", "/appearance/transparency/bar", 0.8, 0.3, 1, 0.05, percent);
    sliderRow(fx, "select_window", "Panel opacity", "/appearance/transparency/panels", 0.9, 0.3, 1, 0.05, percent);

    auto* shape = group(page, "Shape & motion");
    choiceRow<double>(shape, "rounded_corner", "Corners", "Every card, button and panel", "/appearance/roundness", 1.0,
                      {{0.35, "Sharp"}, {0.7, "Soft"}, {1.0, "Round"}, {1.3, "Extra"}});
    choiceRow<double>(shape, "animation", "Animation speed", "", "/appearance/animationSpeed", 1.0,
                      {{0.0, "Off"}, {0.6, "Fast"}, {1.0, "Normal"}, {1.5, "Relaxed"}});

    auto* reset = group(page, "Defaults");
    auto r = row(reset, "restart_alt", "Reset look & feel", "Blur, opacity, corners and motion");
    pillButton(r.row, "Reset", false, [] {
        for (const char* key : {"/appearance/blur/enabled", "/appearance/blur/size", "/appearance/transparency/bar",
                                "/appearance/transparency/panels", "/appearance/roundness", "/appearance/animationSpeed"})
            Config::get().set(key, nullptr);
        // Not inside the click: the rebuild destroys this button
        EventLoop::get().defer([] {
            if (reshow) reshow("style");
        });
    });
}

void barPage(Flex* page, Popup&) {
    auto* layout = group(page, "Layout");
    choiceRow<std::string>(layout, "vertical_align_top", "Position", "", "/statusBar/position", "top",
                           {{"top", "Top"}, {"bottom", "Bottom"}});
    choiceRow<int>(layout, "toolbar", "Style", "", "/statusBar/backgroundStyle", 0, {{0, "Floating"}, {1, "Full width"}});
    sliderRow(layout, "height", "Height", "/statusBar/height", 40, 32, 56, 2, [](double v) { return std::format("{:.0f} px", v); });

    // Layout editor: each zone's modules as chips that move left and right
    // (across zones at the ends) or come off; "+" adds one to a zone
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto adding = std::make_shared<int>(-1); // the zone showing its add list
    live->sync = [live, adding] {
        live->clearChildren();
        auto* g = group(live, "Widgets", "Add, remove and move widgets here, or on the bar itself");
        auto onBar = row(g, "edit", "Edit on the bar", "Move and remove widgets in place; right-click the bar does this too");
        pillButton(onBar.row, "Edit bar", true, [] {
            if (editBar) editBar(true);
        }, "edit");
        auto lay = shell::Bar::layout();
        const char* zoneNames[] = {"Left", "Centre", "Right"};
        auto save = [live, adding](const shell::Bar::Layout& l) {
            shell::Bar::setLayout(l);
            live->syncSoon();
        };
        auto meta = [](const std::string& id) {
            for (const auto& m : shell::Bar::modules())
                if (id == m.id) return m;
            return shell::Bar::Module{"", "extension", "Unknown"};
        };
        for (size_t z = 0; z < 3; ++z) {
            auto* seg = g->add<Flex>(Flex::Direction::Column);
            seg->setAlignItems(Align::Stretch)->setSpacing(10)->setPadding({20, 14});
            auto* head = seg->add<Flex>(Flex::Direction::Row);
            head->setSpacing(10);
            auto* name = head->add<Label>(zoneNames[z]);
            name->setSize(15)->setWeight(600);
            name->grow = true;
            auto* add = head->add<Button>();
            pill(add->setBackground(*adding == static_cast<int>(z) ? "primary" : "secondary_container", false))
                ->setPadding({12, 6})->setSpacing(4);
            add->pressedRadius = 6;
            const char* fg = *adding == static_cast<int>(z) ? "on_primary" : "on_secondary_container";
            add->add<Icon>(*adding == static_cast<int>(z) ? "close" : "add", 16)->setColor(fg, false);
            add->add<Label>(*adding == static_cast<int>(z) ? "Done" : "Add")->setSize(13)->setWeight(500)->setColor(fg, false);
            add->onClicked = [adding, live, z](uint32_t) {
                *adding = *adding == static_cast<int>(z) ? -1 : static_cast<int>(z);
                live->syncSoon();
            };

            auto* chips = seg->add<Flow>();
            if (lay[z].empty()) caption(chips, "Empty");
            for (size_t i = 0; i < lay[z].size(); ++i) {
                const auto m = meta(lay[z][i]);
                auto* chip = chips->add<Flex>(Flex::Direction::Row);
                pill(chip->setBackground("surface_container_highest", false))->setPadding({10, 4, 4, 4})->setSpacing(2);
                chip->add<Icon>(m.icon, 18)->setColor("primary", false);
                chip->add<Label>(m.name)->setSize(13)->setWeight(500)->setMaxWidth(110);
                // Left: earlier in the zone, else the end of the zone before
                const bool canLeft = i > 0 || z > 0, canRight = i + 1 < lay[z].size() || z < 2;
                auto move = [lay, z, i, save](int dir) mutable {
                    const std::string id = lay[z][i];
                    lay[z].erase(lay[z].begin() + static_cast<long>(i));
                    if (dir < 0 && i > 0) lay[z].insert(lay[z].begin() + static_cast<long>(i - 1), id);
                    else if (dir < 0) lay[z - 1].push_back(id);
                    else if (i < lay[z].size()) lay[z].insert(lay[z].begin() + static_cast<long>(i + 1), id);
                    else lay[z + 1].insert(lay[z + 1].begin(), id);
                    save(lay);
                };
                if (canLeft) iconButton(chip, "chevron_left", [move]() mutable { move(-1); }, 16);
                if (canRight) iconButton(chip, "chevron_right", [move]() mutable { move(1); }, 16);
                iconButton(chip, "close", [lay, z, i, save]() mutable {
                    lay[z].erase(lay[z].begin() + static_cast<long>(i));
                    save(lay);
                }, 16);
            }
            if (*adding != static_cast<int>(z)) continue;
            // Modules not on the bar yet
            auto* avail = seg->add<Flow>();
            size_t unused = 0;
            for (const auto& m : shell::Bar::modules()) {
                bool used = false;
                for (const auto& zone : lay) used = used || std::ranges::find(zone, std::string(m.id)) != zone.end();
                if (used) continue;
                ++unused;
                // Gallery tile: icon, name and what it does
                auto* b = avail->add<Button>(Flex::Direction::Column);
                b->setBackground("tertiary_container", false)->setRadius(Radii(20))->setPadding({14, 12})->setSpacing(4);
                b->setAlignItems(Align::Start);
                b->pressedRadius = 12;
                b->fixedWidth = 204;
                auto* top = b->add<Flex>(Flex::Direction::Row);
                top->setSpacing(8);
                top->add<Icon>(m.icon, 20)->setColor("on_tertiary_container", false);
                top->add<Label>(m.name)->setSize(14)->setWeight(600)->setColor("on_tertiary_container", false);
                b->add<Label>(m.about)->setSize(12)->setMaxWidth(176)->setMaxLines(2)->setColor("on_tertiary_container", false);
                const std::string id = m.id;
                b->onClicked = [lay, z, id, save](uint32_t) mutable {
                    lay[z].push_back(id);
                    save(lay);
                };
            }
            if (unused == 0) caption(avail, "Every widget is on the bar");
        }
        auto* reset = live->add<Flex>(Flex::Direction::Row);
        reset->setJustify(Align::End);
        pillButton(reset, "Default layout", false, [live] {
            Config::get().set("/statusBar/layout", nullptr);
            live->syncSoon();
        }, "restart_alt");
    };
    live->sync();

    auto* island = group(page, "Island & clock");
    switchRow(island, "view_carousel", "Show workspace switches in the island", "", "/statusBar/islandWorkspace", true);
    choiceRow<std::string>(island, "schedule", "Clock", "", "/time/format", "hh:mm",
                           {{"hh:mm", "24-hour"}, {"h:mm AP", "12-hour"}, {"hh:mm:ss", "Seconds"}});
}

void dockPage(Flex* page, Popup&) {
    auto* g = group(page, "Dock", "Pinned and running apps on the bottom edge");
    choiceRow<std::string>(g, "dock_to_bottom", "Show", "", "/dock/mode", "autohide",
                           {{"off", "Off"}, {"autohide", "Auto-hide"}, {"always", "Always"}});
    sliderRow(g, "photo_size_select_small", "Icon size", "/dock/iconSize", 44, 32, 64, 2,
              [](double v) { return std::format("{:.0f} px", v); });
    switchRow(g, "apps", "Running apps", "Apps that are open but not pinned, after a divider", "/dock/showRunning", true);
    row(g, "mouse", "Click to focus or open", "Middle click opens a new window · right click pins or unpins");

    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto query = std::make_shared<std::string>();
    live->sync = [live, query] {
        live->clearChildren();
        auto* pins = group(live, "Pinned");
        auto ids = Dock::pinned();
        if (ids.empty()) row(pins, "push_pin", "Nothing pinned", "Search below, or right-click an app in the dock");
        for (size_t i = 0; i < ids.size(); ++i) {
            const Apps::App* app = nullptr;
            for (const auto& a : Apps::get().all())
                if (a.id == ids[i] || a.id == ids[i] + ".desktop") app = &a;
            auto r = row(pins, "", app ? app->name : ids[i], app ? app->comment : "Not installed");
            const std::string icon = app ? app->icon : ids[i];
            r.row->adopt(std::make_unique<ImageView>(32, [icon](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().icon(icon, static_cast<int>(std::lround(32 * scale)));
                if (!t) return 0;
                w = t->width;
                h = t->height;
                return t->id;
            }), 0);
            auto move = [ids, i](int d) mutable {
                const size_t j = static_cast<size_t>(static_cast<long>(i) + d);
                if (j >= ids.size()) return;
                std::swap(ids[i], ids[j]);
                Dock::setPinned(ids);
            };
            if (i > 0) iconButton(r.row, "arrow_upward", [move]() mutable { move(-1); });
            if (i + 1 < ids.size()) iconButton(r.row, "arrow_downward", [move]() mutable { move(1); });
            iconButton(r.row, "close", [ids, i]() mutable {
                ids.erase(ids.begin() + static_cast<long>(i));
                Dock::setPinned(ids);
            });
        }

        auto* add = group(live, "Pin an app");
        auto* fieldRow = add->add<Flex>(Flex::Direction::Row);
        fieldRow->setPadding({16, 12});
        auto* field = fieldRow->add<TextField>();
        field->grow = true;
        field->setPlaceholder("Search apps");
        field->setText(*query);
        field->onChanged = [query, live](const std::string& t) {
            *query = t;
            live->syncSoon();
        };
        if (!query->empty()) field->focus();
        std::vector<std::pair<double, const Apps::App*>> hits;
        for (const auto& a : Apps::get().all()) {
            const double sc = query->empty() ? 0 : fuzzyScore(*query, a.name);
            if (sc > 0) hits.push_back({sc, &a});
        }
        std::ranges::sort(hits, std::greater{}, &std::pair<double, const Apps::App*>::first);
        for (size_t k = 0; k < hits.size() && k < 6; ++k) {
            const auto* a = hits[k].second;
            std::string id = a->id;
            if (id.ends_with(".desktop")) id.resize(id.size() - 8);
            const bool pinnedAlready = std::ranges::find(ids, id) != ids.end();
            auto r = row(add, "apps", a->name, a->comment);
            if (pinnedAlready) r.row->add<Label>("Pinned")->setSize(13)->setColor("on_surface_variant", false);
            else pillButton(r.row, "Pin", true, [id, query] {
                auto now = Dock::pinned();
                now.push_back(id);
                query->clear();
                Dock::setPinned(now);
            }, "push_pin");
        }
    };
    // Config writes come back through the watcher: rebuild then
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

void toolsPage(Flex* page, Popup& popup) {
    auto* clip = group(page, "Clipboard", "History through cliphist (and wl-clipboard)");
    switchRow(clip, "history", "Keep history", "Starts the wl-paste watchers if nothing else runs them", "/clipboard/history", true,
              [](bool on) {
                  if (on) Clipboard::get().ensureWatchers();
              });
    auto r = row(clip, "content_paste", "Clipboard history");
    pillButton(r.row, "Clear", false, [] { Clipboard::get().wipe(); }, "delete_sweep");
    pillButton(r.row, "Open", true, [&popup] {
        popup.close();
        if (open) open("clipboard");
    });

    auto* shots = group(page, "Screenshots", "grim and slurp; edited in satty or swappy");
    textRow(shots, "folder", "Save to", "/screenshot/directory", "~/Pictures/Screenshots");
    switchRow(shots, "content_copy", "Copy to the clipboard", "", "/screenshot/copy", true);
    switchRow(shots, "edit", "Open in the editor", "", "/screenshot/edit", false);
    switchRow(shots, "notifications", "Notify when saved", "", "/screenshot/notify", true);
    auto take = row(shots, "screenshot_region", "Take a screenshot", "Also: material3ui msg screenshot region|window|screen");
    pillButton(take.row, "Open tool", true, [&popup] {
        popup.close();
        if (open) open("screenshot");
    }, "photo_camera");
}

void weatherPage(Flex* page, Popup&) {
    auto* g = group(page, "Weather", "From Open-Meteo, refreshed every 15 minutes");
    switchRow(g, "partly_cloudy_day", "Show weather", "On the bar, the dashboard and the lock screen", "/weather/enabled", true,
              [](bool on) {
                  if (on) Weather::get().refresh();
              });
    choiceRow<std::string>(g, "thermostat", "Units", "", "/weather/units", "metric", {{"metric", "°C · km/h"}, {"imperial", "°F · mph"}});

    // Location: detected, or searched for
    auto* loc = page->add<Live>();
    loc->setSpacing(6);
    auto query = std::make_shared<std::string>();
    auto results = std::make_shared<std::vector<Weather::Place>>();
    loc->sync = [loc, query, results] {
        loc->clearChildren();
        auto* g = group(loc, "Location");
        const auto place = Config::get().value<nlohmann::json>("/weather/location", nlohmann::json());
        auto current = row(g, "location_on", place.is_object() ? place.value("name", "Unknown") : "Not set",
                           place.is_object() ? std::format("{:.2f}, {:.2f}", place.value("lat", 0.0), place.value("lon", 0.0))
                                             : "Found from your IP address on first use");
        pillButton(current.row, "Use my location", false, [] { Weather::get().detect(); }, "my_location");
        pillButton(current.row, "Refresh", true, [] { Weather::get().refresh(); }, "refresh");
        auto* fieldRow = g->add<Flex>(Flex::Direction::Row);
        fieldRow->setPadding({16, 12});
        auto* field = fieldRow->add<TextField>();
        field->grow = true;
        field->setPlaceholder("Search for a city");
        field->setText(*query);
        if (!query->empty()) field->focus();
        field->onChanged = [query, results, loc](const std::string& t) {
            *query = t;
            Weather::get().search(t, [query, results, loc, t](std::vector<Weather::Place> found) {
                if (*query != t) return; // a newer search is on its way
                *results = std::move(found);
                loc->syncSoon();
            });
        };
        for (const auto& p : *results) {
            auto r = row(g, "place", p.name, std::format("{:.2f}, {:.2f}", p.lat, p.lon));
            pillButton(r.row, "Use", true, [p, query, results] {
                query->clear();
                results->clear();
                Weather::get().setPlace(p);
            });
        }
    };
    loc->conns.push_back(Config::get().changed.connect([loc] { loc->syncSoon(); }));
    loc->sync();

    // Live conditions
    auto* now = page->add<Live>();
    now->setSpacing(6)->setPadding({0, 10, 0, 0});
    now->sync = [now] {
        now->clearChildren();
        auto& w = Weather::get();
        auto* card = now->add<Flex>(Flex::Direction::Column);
        card->setPadding({24, 20})->setSpacing(14)->setAlignItems(Align::Stretch)->setRadius(Radii(28));
        card->setBackground("primary_container", false);
        if (!w.ready()) {
            card->add<Label>(w.error().empty() ? "Getting the weather…" : w.error())->setSize(16)->setColor("on_primary_container", false);
            return;
        }
        auto* head = card->add<Flex>(Flex::Direction::Row);
        head->setSpacing(18);
        head->add<Icon>(w.icon(), 64)->setFill(true)->setColor("on_primary_container", false);
        auto* texts = head->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(2);
        texts->grow = true;
        texts->add<Label>(w.temp(w.temperature()))->setSize(48)->setWeight(600)->setColor("on_primary_container", false);
        texts->add<Label>(Weather::describe(w.code()) + "  ·  feels like " + w.temp(w.feelsLike()))->setSize(15)->setColor("on_primary_container", false);
        auto* right = head->add<Flex>(Flex::Direction::Column);
        right->setAlignItems(Align::End)->setSpacing(4);
        right->add<Label>(w.place())->setSize(14)->setWeight(600)->setMaxWidth(260)->setColor("on_primary_container", false);
        right->add<Label>(std::format("Humidity {}%  ·  Wind {}", w.humidity(), w.windText()))->setSize(13)->setColor("on_primary_container", false);
        if (w.updated() > 0) {
            const std::time_t t = static_cast<std::time_t>(w.updated());
            std::tm tm{};
            localtime_r(&t, &tm);
            char buf[32];
            std::strftime(buf, sizeof buf, "Updated %H:%M", &tm);
            right->add<Label>(buf)->setSize(12)->setColor("on_primary_container", false);
        }
        // The week, as tonal tiles
        auto* week = card->add<Flex>(Flex::Direction::Row);
        week->setSpacing(6)->setAlignItems(Align::Stretch);
        for (size_t i = 0; i < w.days().size(); ++i) {
            const auto& d = w.days()[i];
            std::tm tm{};
            strptime(d.date.c_str(), "%Y-%m-%d", &tm);
            char day[16];
            std::strftime(day, sizeof day, "%a", &tm);
            auto* tile = week->add<Flex>(Flex::Direction::Column);
            tile->setSpacing(4)->setPadding({4, 10})->setRadius(Radii(18))->setBackground(ColorRef("surface", 0.35f), false);
            tile->grow = true;
            tile->add<Label>(i == 0 ? std::string("Today") : std::string(day))->setSize(13)->setWeight(600)->setColor("on_primary_container", false);
            tile->add<Icon>(Weather::icon(d.code, true), 24)->setColor("on_primary_container", false);
            tile->add<Label>(w.temp(d.max))->setSize(14)->setWeight(500)->setColor("on_primary_container", false);
            tile->add<Label>(w.temp(d.min))->setSize(13)->setColor("on_primary_container", false);
        }
        if (!w.error().empty()) card->add<Label>(w.error())->setSize(12)->setColor("on_primary_container", false);
    };
    now->watch(Weather::get().changed);
    now->sync();
}

void lockScreenPage(Flex* page, Popup& popup) {
    auto* clock = group(page, "Clock", "Place it on the board below; the login screen shows the same");
    choiceRow<std::string>(clock, "schedule", "Style", "", "/lock/clock/style", "stacked",
                           {{"stacked", "Stacked"}, {"inline", "One line"}});
    choiceRow<int>(clock, "format_bold", "Weight", "", "/lock/clock/weight", 400, {{300, "Light"}, {400, "Regular"}, {600, "Bold"}});
    sliderRow(clock, "format_size", "Size", "/lock/clock/size", 150, 72, 200, 4, [](double v) { return std::format("{:.0f} px", v); });

    // Layout: a board of the lock screen's grid over the wallpaper
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto sel = std::make_shared<std::pair<int, int>>(-1, -1); // selected cell (col, row)
    live->sync = [live, sel] {
        live->clearChildren();
        using namespace lockgrid;
        auto items = load();
        auto at = [&](int c, int r) -> Item* {
            for (auto& it : items)
                if (it.col == c && it.row == r) return &it;
            return nullptr;
        };
        const char* colName[] = {"left", "centre", "right"};
        const char* rowName[] = {"Top", "Upper", "Lower", "Bottom"};

        auto* g = group(live, "Layout", "The lock and login screens, as a grid: click a cell to add a widget, or a widget to move or remove it");
        auto full = row(g, "fullscreen", "Edit on the lock screen", "The real screen at full size, with your widgets live");
        pillButton(full.row, "Open editor", true, [] {
            if (open) open("lockeditor");
        }, "edit");
        auto* holder = g->add<Flex>(Flex::Direction::Column);
        holder->setAlignItems(Align::Stretch)->setSpacing(12)->setPadding({16, 16});

        // What the selection allows
        auto* tools = holder->add<Flex>(Flex::Direction::Column);
        tools->setAlignItems(Align::Stretch)->setSpacing(10);
        const int c = sel->first, r = sel->second;
        if (c < 0) {
            caption(tools, "Select a cell on the board below.");
        } else if (Item* it = at(c, r)) {
            auto* bar = tools->add<Flex>(Flex::Direction::Row);
            bar->setSpacing(8);
            const auto* d = find(it->id);
            auto* name = bar->add<Label>(std::format("{}  ·  {} {}", d->name, rowName[r], colName[c]));
            name->setSize(14)->setWeight(600);
            name->grow = true;
            // Moving onto a taken cell swaps the two
            auto move = [items, c, r, sel](int dc, int dr) mutable {
                const int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) return;
                for (auto& o : items)
                    if (o.col == nc && o.row == nr) {
                        o.col = c;
                        o.row = r;
                    } else if (o.col == c && o.row == r) {
                        o.col = nc;
                        o.row = nr;
                    }
                *sel = {nc, nr};
                save(items);
            };
            if (c > 0) iconButton(bar, "arrow_back", [move]() mutable { move(-1, 0); });
            if (r > 0) iconButton(bar, "arrow_upward", [move]() mutable { move(0, -1); });
            if (r < kRows - 1) iconButton(bar, "arrow_downward", [move]() mutable { move(0, 1); });
            if (c < kCols - 1) iconButton(bar, "arrow_forward", [move]() mutable { move(1, 0); });
            pillButton(bar, "Remove", false, [items, c, r, sel]() mutable {
                std::erase_if(items, [&](const Item& o) { return o.col == c && o.row == r; });
                *sel = {-1, -1};
                save(items);
            }, "delete");
        } else {
            auto* l = tools->add<Label>(std::format("Add to {} {}", rowName[r], colName[c]));
            l->setSize(14)->setWeight(600);
            l->alignSelf = Align::Start;
            auto* gallery = tools->add<Flow>();
            size_t free = 0;
            for (const auto& d : widgets()) {
                if (std::ranges::any_of(items, [&](const Item& o) { return o.id == d.id; })) continue;
                ++free;
                auto* b = gallery->add<Button>(Flex::Direction::Column);
                b->setBackground("tertiary_container", false)->setRadius(Radii(18))->setPadding({12, 10})->setSpacing(2);
                b->setAlignItems(Align::Start);
                b->pressedRadius = 10;
                b->fixedWidth = 160;
                auto* top = b->add<Flex>(Flex::Direction::Row);
                top->setSpacing(6);
                top->add<Icon>(d.icon, 18)->setColor("on_tertiary_container", false);
                top->add<Label>(d.name)->setSize(13)->setWeight(600)->setColor("on_tertiary_container", false);
                b->add<Label>(d.about)->setSize(12)->setMaxWidth(136)->setColor("on_tertiary_container", false);
                const std::string id = d.id;
                b->onClicked = [items, id, c, r, sel](uint32_t) mutable {
                    items.push_back({id, c, r});
                    *sel = {c, r};
                    save(items);
                };
            }
            if (free == 0) caption(gallery, "Every widget is placed. Remove one to put it here.");
        }
        // Wallpaper under the cells, like the real screen
        auto* board = holder->add<Node>();
        board->fixedHeight = 4 * 70 + 3 * 6 + 24;
        const std::string wp = Wallpaper::get().lockPath();
        auto* bg = board->add<ImageView>(10, 10, [wp](float scale, int& w, int& h) -> GLuint {
            if (wp.empty()) return 0;
            auto* t = Images::get().picture(wp, static_cast<int>(700 * scale), static_cast<int>(320 * scale), true);
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        });
        bg->cover = true;
        auto* scrim = board->add<Flex>(Flex::Direction::Column);
        scrim->setBackground(ColorRef("surface", 0.55f), false)->setRadius(Radii(20))->setPadding(Insets(12))->setSpacing(6);
        scrim->setAlignItems(Align::Stretch);
        for (int r = 0; r < kRows; ++r) {
            auto* rowFlex = scrim->add<Flex>(Flex::Direction::Row);
            rowFlex->setSpacing(6)->setAlignItems(Align::Stretch)->setJustify(Align::Center);
            for (int c = 0; c < kCols; ++c) {
                const Item* it = at(c, r);
                const bool selected = sel->first == c && sel->second == r;
                auto* cell = rowFlex->add<Button>(Flex::Direction::Row);
                cell->fixedWidth = 208; // equal thirds of the board
                cell->fixedHeight = 70;
                cell->setRadius(Radii(16))->setSpacing(8)->setJustify(Align::Center);
                cell->pressedRadius = 8;
                if (it) {
                    const auto* d = find(it->id);
                    cell->setBackground(selected ? "primary" : "secondary_container", false);
                    cell->add<Icon>(d->icon, 22)->setFill(true)->setColor(selected ? "on_primary" : "on_secondary_container", false);
                    cell->add<Label>(d->name)->setSize(14)->setWeight(600)->setColor(selected ? "on_primary" : "on_secondary_container", false);
                } else {
                    cell->setBackground(selected ? ColorRef("primary", 0.25f) : ColorRef("surface_container_highest", 0.35f), false);
                    cell->setBorder(selected ? 2 : 1, selected ? ColorRef("primary") : ColorRef("outline_variant"));
                    cell->add<Icon>("add", 22)->setColor(selected ? "primary" : "on_surface_variant", false);
                }
                cell->onClicked = [sel, live, c, r](uint32_t) {
                    *sel = sel->first == c && sel->second == r ? std::pair{-1, -1} : std::pair{c, r};
                    live->syncSoon();
                };
            }
        }

        auto* foot = holder->add<Flex>(Flex::Direction::Row);
        foot->setJustify(Align::End);
        pillButton(foot, "Default layout", false, [sel] {
            *sel = {-1, -1};
            Config::get().set("/lock/grid", nullptr);
            Config::get().set("/lock/widgets", nullptr);
        }, "restart_alt");
    };
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();

    auto* login = group(page, "Login screen", "greetd's greeter, installed with install.sh --greeter");
    const bool installed = access("/var/lib/material3ui-greeter", F_OK) == 0;
    switchRow(login, "sync", "Match the lock screen", "Copies the clock, widgets, weather, wallpaper and colours when they change",
              "/lock/syncGreeter", true);
    auto status = row(login, "login", installed ? "Login screen installed" : "Login screen not installed",
                      installed ? "Sync copies your look to it now" : "Run: greeter/material3ui-greeter install --enable");
    if (installed) pillButton(status.row, "Sync now", false, [] {
        m3::run({"material3ui-greeter", "sync"}, [](int code, const std::string& out) {
            m3::run({"notify-send", "-a", "Settings", code == 0 ? "Login screen synced" : "Login screen sync failed", out}, nullptr);
        });
    }, "sync");
    auto preview = row(login, "visibility", "Preview", "Locks the screen now: unlock with your password");
    pillButton(preview.row, "Lock now", true, [&popup] {
        popup.close();
        if (lock) lock();
    }, "lock");
}

void notificationsPage(Flex* page, Popup&) {
    auto* g = group(page, "Notifications");
    auto dnd = row(g, "do_not_disturb_on", "Do not disturb", "Only critical notifications show while this is on");
    auto* sw = dnd.row->add<Switch>();
    sw->setChecked(Notifications::get().doNotDisturb(), false);
    sw->onToggled = [](bool on) { Notifications::get().setDoNotDisturb(on); };
    choiceRow<int>(g, "timer", "Pop-ups stay for", "", "/notifications/timeout", 6, {{3, "3 s"}, {6, "6 s"}, {10, "10 s"}, {0, "Until closed"}});
    auto daemon = row(g, "info", Notifications::get().active() ? "Material3UI is your notification daemon"
                                                              : "Another notification daemon is running",
                      Notifications::get().active() ? "" : "Stop it (mako, dunst, swaync…) and restart the shell");
    (void)daemon;
}

void lockPage(Flex* page, Popup&) {
    auto* g = group(page, "Lock & idle");
    const std::vector<std::pair<int, std::string>> times = {{0, "Never"}, {60, "1 min"}, {300, "5 min"}, {600, "10 min"}, {1800, "30 min"}};
    choiceRow<int>(g, "lock_clock", "Lock after", "", "/idle/lockAfter", 600, times);
    choiceRow<int>(g, "monitor", "Screen off after", "", "/idle/screenOffAfter", 900,
                   {{0, "Never"}, {120, "2 min"}, {600, "10 min"}, {900, "15 min"}, {3600, "1 hour"}});
    switchRow(g, "bedtime", "Lock before sleep", "", "/lock/lockBeforeSleep", true);
    switchRow(g, "wallpaper", "Separate lock-screen wallpaper", "Uses lock.wallpaperPath", "/lock/useSeparateWallpaper", false);
    auto test = row(g, "lock", "Lock now");
    pillButton(test.row, "Lock", true, [] {
        if (lock) lock();
    });
}

// Key capture: click, then press a key
class KeyField : public Button {
public:
    std::string key;
    std::function<void()> onCaptured;
    explicit KeyField(std::string k) : Button(Direction::Row), key(std::move(k)) {
        pill(this)->setPadding({16, 9})->setBackground("surface_container_highest", false);
        m_label = add<Label>();
        m_label->setSize(14)->setWeight(500);
        sync();
        onClicked = [this](uint32_t) {
            m_capturing = true;
            focus();
            sync();
        };
    }
    bool focusable() const override { return true; }
    void onFocus(bool f) override {
        if (!f) m_capturing = false;
        sync();
    }
    bool onKey(const KeyEvent& e) override {
        if (!m_capturing) return false;
        char name[64];
        if (xkb_keysym_get_name(e.sym, name, sizeof name) <= 0) return true;
        const std::string n = name;
        // Modifiers alone are chips, not keys
        if (n.starts_with("Shift") || n.starts_with("Control") || n.starts_with("Alt") || n.starts_with("Meta") ||
            (n.starts_with("Super") && n != "Super_L"))
            return true;
        key = n.size() == 1 ? std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])))) : n;
        m_capturing = false;
        sync();
        if (onCaptured) onCaptured();
        return true;
    }

private:
    void sync() {
        m_label->setText(m_capturing ? "Press a key…" : key.empty() ? "Click, then press a key" : Keybinds::pretty(key));
        setBorder(m_capturing ? 2 : 0, "primary");
    }
    Label* m_label;
    bool m_capturing = false;
};

// Modifier chips plus a key field, with live conflict warnings
struct ComboEditor {
    Flex* root;
    std::shared_ptr<std::array<bool, 4>> mods;
    KeyField* key;
    std::string combo() const {
        static const char* names[] = {"SUPER", "CTRL", "ALT", "SHIFT"};
        std::string c;
        for (int i = 0; i < 4; ++i)
            if ((*mods)[static_cast<size_t>(i)]) c += std::string(names[i]) + " + ";
        return c + key->key;
    }
};

ComboEditor comboEditor(Node* parent, const std::string& initial, const std::string& exceptId) {
    auto* col = parent->add<Flex>(Flex::Direction::Column);
    col->setAlignItems(Align::Stretch)->setSpacing(8)->setPadding({20, 6});
    auto* rowFlex = col->add<Flex>(Flex::Direction::Row);
    rowFlex->setSpacing(6);
    auto mods = std::make_shared<std::array<bool, 4>>();
    const std::string norm = Keybinds::normalize(initial.empty() ? "SUPER + " : initial);
    std::string key = norm.substr(norm.rfind(" + ") == std::string::npos ? 0 : norm.rfind(" + ") + 3);
    static const char* names[] = {"SUPER", "CTRL", "ALT", "SHIFT"};
    static const char* labels[] = {"Super", "Ctrl", "Alt", "Shift"};
    for (int i = 0; i < 4; ++i) (*mods)[static_cast<size_t>(i)] = norm.find(names[i]) != std::string::npos;
    if (key == names[0] || key == names[1] || key == names[2] || key == names[3]) key.clear();
    auto* warn = col->add<Label>();
    warn->setSize(12)->setColor("error", false)->setMaxWidth(600);
    auto* keyField = std::make_unique<KeyField>(key).release();
    ComboEditor ed{col, mods, keyField};
    auto refresh = [warn, ed, exceptId] {
        const auto hits = ed.key->key.empty() ? std::vector<std::string>{} : Keybinds::get().conflictsFor(ed.combo(), exceptId);
        std::string text;
        for (const auto& h : hits) text += (text.empty() ? "" : ", ") + h;
        warn->setText(hits.empty() ? "" : "Also used by: " + text + ". Both will run.");
        warn->setVisible(!hits.empty());
    };
    for (int i = 0; i < 4; ++i) {
        auto* chip = rowFlex->add<Button>();
        pill(chip)->setPadding({14, 8});
        auto* l = chip->add<Label>(labels[i]);
        l->setSize(13)->setWeight(500);
        auto paint = [chip, l, mods, i] {
            const bool on = (*mods)[static_cast<size_t>(i)];
            chip->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_highest"));
            l->setColor(on ? "on_secondary_container" : "on_surface");
        };
        paint();
        chip->onClicked = [mods, i, paint, refresh](uint32_t) {
            (*mods)[static_cast<size_t>(i)] = !(*mods)[static_cast<size_t>(i)];
            paint();
            refresh();
        };
    }
    rowFlex->add<Label>("+")->setSize(15)->setColor("on_surface_variant", false);
    rowFlex->adopt(std::unique_ptr<Node>(keyField));
    keyField->grow = true;
    keyField->onCaptured = refresh;
    refresh();
    return ed;
}

void keybindsPage(Flex* page, Popup&) {
    auto& kb = Keybinds::get();
    if (!kb.supported()) {
        auto* g = group(page, "Keybinds");
        row(g, "info", "The keybind editor needs Hyprland's Lua config (0.55+)", "Edit binds in your hyprland.conf instead");
        return;
    }
    auto* live = page->add<Live>();
    auto editing = std::make_shared<std::string>(); // id being edited, "+" for a new custom bind
    live->sync = [live, editing] {
        live->clearChildren();
        auto& kb = Keybinds::get();
        auto* intro = live->add<Label>("Click a bind to change its keys. Changes apply at once and survive updates.");
        intro->setSize(13)->setColor("on_surface_variant", false);
        intro->alignSelf = Align::Start;

        std::vector<std::string> groups;
        for (const auto& b : kb.binds())
            if (std::ranges::find(groups, b.group) == groups.end()) groups.push_back(b.group);
        for (const auto& name : groups) {
            auto* g = group(live, name);
            for (const auto& b : kb.binds()) {
                if (b.group != name) continue;
                auto* r = g->add<Button>(Flex::Direction::Row);
                r->setPadding({20, 10})->setSpacing(12);
                r->minHeight = 52;
                auto* t = r->add<Label>(b.action);
                t->setSize(15);
                t->grow = true;
                if (b.conflict) r->add<Icon>("warning", 18)->setColor("error", false);
                if (b.changed) r->add<Icon>("edit", 16)->setColor("primary", false);
                r->add<Label>(Keybinds::pretty(b.combo))->setSize(13)->setWeight(500)->setColor("on_surface_variant", false);
                const auto bind = b;
                r->onClicked = [live, editing, bind](uint32_t) {
                    *editing = *editing == bind.id ? "" : bind.id;
                    live->syncSoon();
                };
                if (*editing != b.id) continue;
                auto ed = comboEditor(g, b.combo, b.id);
                auto* actions = ed.root->add<Flex>(Flex::Direction::Row);
                actions->setSpacing(8)->setJustify(Align::End);
                pillButton(actions, "Disable", false, [bind, editing] {
                    editing->clear();
                    Keybinds::get().disable(bind.id, bind.label);
                });
                if (b.changed) pillButton(actions, "Reset", false, [bind, editing] {
                    editing->clear();
                    Keybinds::get().reset(bind.id);
                });
                pillButton(actions, "Save", true, [ed, bind, editing] {
                    if (ed.key->key.empty()) return;
                    editing->clear();
                    Keybinds::get().remap(bind.id, ed.combo(), bind.label);
                });
            }
        }

        const auto disabled = kb.disabled();
        if (!disabled.empty()) {
            auto* g = group(live, "Disabled");
            for (const auto& d : disabled) {
                auto r = row(g, "block", d.action, d.group);
                pillButton(r.row, "Enable", false, [id = d.id] { Keybinds::get().reset(id); });
            }
        }

        auto* custom = group(live, "Custom", "Run any command from a key combination");
        for (size_t i = 0; i < kb.custom().size(); ++i) {
            const auto& c = kb.custom()[i];
            auto r = row(custom, "terminal", c.label, c.command + "  ·  " + Keybinds::pretty(c.keys));
            iconButton(r.row, "delete", [i] { Keybinds::get().removeCustom(i); });
        }
        if (*editing == "+") {
            auto* form = custom->add<Flex>(Flex::Direction::Column);
            form->setAlignItems(Align::Stretch)->setSpacing(8)->setPadding({20, 8});
            auto* name = form->add<TextField>();
            name->setPlaceholder("Name (e.g. Open VS Code)");
            auto* cmd = form->add<TextField>();
            cmd->setPlaceholder("Command (e.g. code)");
            auto ed = comboEditor(form, "", "");
            auto* actions = form->add<Flex>(Flex::Direction::Row);
            actions->setSpacing(8)->setJustify(Align::End);
            pillButton(actions, "Cancel", false, [editing, live] {
                editing->clear();
                live->syncSoon();
            });
            pillButton(actions, "Add", true, [ed, name, cmd, editing] {
                if (ed.key->key.empty() || cmd->text().empty()) return;
                editing->clear();
                Keybinds::get().addCustom(ed.combo(), cmd->text(), name->text());
            });
        } else {
            auto r = row(custom, "add", "Add a bind");
            pillButton(r.row, "Add", true, [editing, live] {
                *editing = "+";
                live->syncSoon();
            });
        }

        if (!kb.others().empty()) {
            auto* g = group(live, "Other binds", "From user.lua or other configs; edit them there");
            for (const auto& o : kb.others()) {
                auto r = row(g, "", o.label);
                r.title->setSize(13)->setColor("on_surface_variant", false);
                r.row->add<Label>(Keybinds::pretty(o.combo))->setSize(13)->setColor("on_surface_variant", false);
            }
        }
        auto* footer = live->add<Flex>(Flex::Direction::Row);
        footer->setJustify(Align::End);
        pillButton(footer, "Reset all to defaults", false, [] { Keybinds::get().resetAll(); }, "restart_alt");
    };
    live->watch(kb.changed);
    kb.refresh();
}

void packagesPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    auto tab = std::make_shared<int>(0); // 0 updates, 1 official, 2 AUR, 3 Flatpak
    auto* tabs = live->add<Flex>(Flex::Direction::Row);
    tabs->setSpacing(6);
    auto* body = live->add<Flex>(Flex::Direction::Column);
    body->setAlignItems(Align::Stretch)->setSpacing(12);
    auto query = std::make_shared<std::string>();
    const char* names[] = {"Updates", "Official", "AUR", "Flatpak"};

    std::function<void()> rebuild = [=] {
        tabs->clearChildren();
        for (int i = 0; i < 4; ++i) {
            auto* b = tabs->add<Button>();
            pill(b)->setPadding({16, 8});
            b->setBackground(i == *tab ? "primary" : "surface_container_high", false);
            b->add<Label>(names[i])->setSize(14)->setWeight(500)->setColor(i == *tab ? "on_primary" : "on_surface", false);
            b->onClicked = [=](uint32_t) {
                *tab = i;
                query->clear();
                if (i > 0) Packages::get().search(static_cast<Packages::Source>(i - 1), "");
                live->syncSoon();
            };
        }
        body->clearChildren();
        auto& p = Packages::get();
        if (*tab == 0) {
            auto* hero = body->add<Flex>(Flex::Direction::Row);
            hero->setPadding(Insets(20))->setSpacing(16)->setRadius(Radii(24))->setBackground("surface_container_high", false);
            hero->add<Icon>(p.updates().empty() ? "verified" : "system_update_alt", 36)->setColor("primary", false);
            auto* texts = hero->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(2);
            texts->grow = true;
            const std::string status = p.checking() ? "Checking for updates…"
                                     : !p.error().empty() ? p.error()
                                     : p.lastChecked() == 0 ? "Not checked yet"
                                     : p.updates().empty() ? "Your system is up to date"
                                     : std::format("{} updates ({} official, {} AUR, {} Flatpak)", p.updates().size(),
                                                   p.count(Packages::Source::Repo), p.count(Packages::Source::Aur),
                                                   p.count(Packages::Source::Flatpak));
            texts->add<Label>(status)->setSize(17)->setWeight(500)->setMaxWidth(420);
            if (!p.aurHelper().empty())
                texts->add<Label>("AUR helper: " + p.aurHelper())->setSize(13)->setColor("on_surface_variant", false);
            pillButton(hero, "Check now", false, [] { Packages::get().check(); }, "refresh");
            if (!p.updates().empty()) pillButton(hero, "Update now", true, [] { Packages::get().upgrade(); }, "download");
            if (!p.updates().empty()) {
                auto* list = group(body, "Pending");
                for (const auto& u : p.updates()) {
                    const char* src = u.source == Packages::Source::Aur ? "AUR" : u.source == Packages::Source::Flatpak ? "Flatpak" : "Official";
                    row(list, u.source == Packages::Source::Flatpak ? "apps" : "deployed_code", u.name,
                        std::format("{}{} → {}", src, u.from.empty() ? "" : "  ·  " + u.from, u.to));
                }
            }
            return;
        }
        auto* field = body->add<TextField>();
        field->setPlaceholder(*tab == 1 ? "Search the official repositories" : *tab == 2 ? "Search the AUR" : "Search Flathub");
        field->setText(*query);
        const auto source = static_cast<Packages::Source>(*tab - 1);
        field->onChanged = [query, source](const std::string& t) {
            *query = t;
            Packages::get().search(source, t);
        };
        field->focus();
        if (p.searching()) caption(body, "Searching…");
        auto* list = body->add<Flex>(Flex::Direction::Column);
        list->setAlignItems(Align::Stretch)->setSpacing(2);
        for (const auto& r : p.results()) {
            auto rr = row(list, r.source == Packages::Source::Flatpak ? "apps" : "deployed_code",
                          std::format("{}  {}", r.name, r.version), r.description);
            rr.row->add<Label>(r.repo)->setSize(12)->setWeight(600)->setColor("primary", false);
            if (r.installed) {
                auto* badge = rr.row->add<Flex>(Flex::Direction::Row);
                pill(badge->setBackground("secondary_container", false))->setPadding({12, 6})->setSpacing(4);
                badge->add<Icon>("check", 16)->setColor("on_secondary_container", false);
                badge->add<Label>("Installed")->setSize(12)->setColor("on_secondary_container", false);
            } else {
                pillButton(rr.row, "Install", true, [r] { Packages::get().install(r); }, "download");
            }
        }
    };
    // The search field keeps focus while results stream in: rebuild only the
    // list on result changes, the tabs and field on tab changes
    live->sync = rebuild;
    live->watch(Packages::get().changed);
    rebuild();
    if (Packages::get().lastChecked() == 0) Packages::get().check();
}

void aboutPage(Flex* page, Popup&) {
    auto* hero = page->add<Flex>(Flex::Direction::Column);
    hero->setSpacing(10)->setPadding({0, 12});
    const std::string logo = paths::dataFile("icons/Material3UI.svg");
    hero->add<ImageView>(112, [logo](float scale, int& w, int& h) -> GLuint {
        if (logo.empty()) return 0;
        auto* t = Images::get().icon(logo, static_cast<int>(std::lround(112 * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
    hero->add<Label>("Material3UI")->setSize(28)->setWeight(600);
    hero->add<Label>("Native shell " M3_VERSION " · C++, Wayland and OpenGL ES")->setSize(14)->setColor("on_surface_variant", false);

    auto* sys = group(page, "This device");
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    utsname u{};
    uname(&u);
    std::string os = readFirstLine("/etc/os-release", "PRETTY_NAME=");
    if (os.size() >= 2 && os.front() == '"') os = os.substr(1, os.size() - 2);
    std::string cpu = readFirstLine("/proc/cpuinfo", "model name");
    if (auto c = cpu.find(':'); c != std::string::npos) cpu = cpu.substr(c + 2);
    std::string hypr = "Not running";
    if (auto v = Hyprland::get().requestJson("version")) hypr = v->value("tag", v->value("version", "Hyprland"));
    auto& stats = SystemStats::get();
    auto lease = stats.lease(); // one sample for memory and uptime
    row(sys, "computer", host, "Device name");
    row(sys, "deployed_code", os.empty() ? "Linux" : os, std::string("Kernel ") + u.release);
    row(sys, "memory", cpu.empty() ? "CPU" : cpu, std::format("{} logical cores", stats.cores()));
    row(sys, "memory_alt", formatBytes(stats.memTotal()), "Memory");
    row(sys, "grid_view", "Hyprland " + hypr, "Compositor");

    auto* credits = group(page, "Credits");
    row(credits, "favorite", "NAnDoroid-shell by na-ive", "The project Material3UI grew from");
    row(credits, "architecture", "Noctalia", "The native shell follows its architecture (MIT); no code is shared");
    row(credits, "auto_awesome", "end-4, DankMaterialShell, Ambxst, pC, P3DROVFX, Vynx", "Design and interaction references");
}

struct PageDef {
    const char* id;
    const char* icon;
    const char* title;
    const char* blurb;
    void (*build)(Flex*, Popup&);
};

const PageDef kPages[] = {
    {"network", "wifi", "Network", "Wi-Fi networks and your connection", networkPage},
    {"bluetooth", "bluetooth", "Bluetooth", "Pair and connect devices", bluetoothPage},
    {"sound", "volume_up", "Sound", "Outputs, inputs, balance and apps", soundPage},
    {"display", "brightness_medium", "Display", "Brightness, night light and monitors", displayPage},
    {"appearance", "palette", "Wallpaper & colour", "Wallpaper, Material You colours and fonts", appearancePage},
    {"style", "blur_on", "Look & feel", "Blur, transparency, corners and motion", stylePage},
    {"bar", "toolbar", "Status bar", "Position, modules and the island", barPage},
    {"dock", "dock_to_bottom", "Dock", "Pinned apps, running apps and auto-hide", dockPage},
    {"tools", "construction", "Tools", "Clipboard history and screenshots", toolsPage},
    {"weather", "partly_cloudy_day", "Weather", "Location, units and the forecast", weatherPage},
    {"notifications", "notifications", "Notifications", "Do not disturb and pop-ups", notificationsPage},
    {"lockscreen", "lock_person", "Lock screen", "Clock, widgets and the login screen", lockScreenPage},
    {"lock", "lock_clock", "Lock & idle", "When the screen locks and sleeps", lockPage},
    {"keybinds", "keyboard", "Keybinds", "Remap, disable and add shortcuts", keybindsPage},
    {"packages", "deployed_code", "Packages & Updates", "Updates and software from repos, AUR and Flathub", packagesPage},
    {"about", "info", "About", "This device and the shell", aboutPage},
};

} // namespace

void settings(Flex* card, Popup& popup, const std::string& initial) {
    card->setPadding(Insets(12))->setSpacing(0);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setAlignItems(Align::Start)->setSpacing(12);

    // Navigation drawer; scrolls too when the screen is short
    auto* sideScroll = columns->add<ScrollView>();
    sideScroll->maxHeight = 680;
    sideScroll->fixedWidth = 244;
    auto* side = sideScroll->add<Flex>(Flex::Direction::Column);
    side->setAlignItems(Align::Stretch)->setSpacing(2)->setPadding({4, 12, 4, 8});
    auto* brand = side->add<Flex>(Flex::Direction::Row);
    brand->setSpacing(12)->setPadding({4, 0});
    brand->alignSelf = Align::Start;
    // The app's icon: installed in the icon theme, or shipped beside the binary
    brand->add<ImageView>(36, [](float scale, int& w, int& h) -> GLuint {
        const int px = static_cast<int>(std::lround(36 * scale));
        auto* t = Images::get().icon("material3ui-settings", px);
        if (!t) {
            const std::string file = paths::dataFile("icons/material3ui-settings.svg");
            if (!file.empty()) t = Images::get().icon(file, px);
        }
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
    brand->add<Label>("Settings")->setSize(28)->setWeight(600);
    auto* gap = side->add<Node>();
    gap->minHeight = 14;

    // The page: a fixed header with the close button, then what scrolls
    auto* right = columns->add<Flex>(Flex::Direction::Column);
    right->setAlignItems(Align::Stretch)->setSpacing(4);
    right->fixedWidth = 740;
    auto* hero = right->add<Flex>(Flex::Direction::Row);
    hero->setSpacing(16)->setPadding({8, 4, 0, 8})->setAlignItems(Align::Start);
    auto* badge = hero->add<Flex>(Flex::Direction::Row);
    badge->setJustify(Align::Center)->setRadius(Radii(18))->setBackground("primary_container", false);
    badge->fixedWidth = 56;
    badge->fixedHeight = 56;
    auto* badgeIcon = badge->add<Icon>("settings", 28);
    badgeIcon->setFill(true)->setColor("on_primary_container", false);
    auto* heroTexts = hero->add<Flex>(Flex::Direction::Column);
    heroTexts->setAlignItems(Align::Start)->setSpacing(2);
    heroTexts->grow = true;
    auto* heroTitle = heroTexts->add<Label>();
    heroTitle->setSize(32)->setWeight(600);
    auto* heroBlurb = heroTexts->add<Label>();
    heroBlurb->setSize(14)->setColor("on_surface_variant", false);
    // Close: a tonal round button in the corner
    auto* close = hero->add<Button>();
    pill(close->setBackground("surface_container_highest", false))->setPadding(Insets(10));
    close->pressedRadius = 12;
    close->add<Icon>("close", 22)->setColor("on_surface", false);
    close->onClicked = [&popup](uint32_t button) {
        if (button == BTN_LEFT) popup.close();
    };

    auto* scroll = right->add<ScrollView>();
    scroll->maxHeight = 600;
    auto* content = scroll->add<Flex>(Flex::Direction::Column);
    content->setAlignItems(Align::Stretch)->setSpacing(6)->setPadding({4, 4, 12, 12});

    struct NavItem {
        Button* button;
        Icon* icon;
        Label* label;
    };
    auto items = std::make_shared<std::vector<NavItem>>();
    auto show = std::make_shared<std::function<void(size_t)>>();
    *show = [content, scroll, items, badgeIcon, heroTitle, heroBlurb, sideScroll, side, &popup](size_t index) {
        const auto& def = kPages[index];
        content->clearChildren();
        // Expressive header: the page's icon in a tonal shape, a large title
        badgeIcon->setText(def.icon);
        heroTitle->setText(def.title);
        heroBlurb->setText(def.blurb);
        def.build(content, popup);
        scroll->scrollTo(0, false);
        // New page: rises into place
        content->setOpacity(0, false);
        content->setTranslateY(24, false);
        content->setOpacity(1);
        content->setTranslateY(0);
        for (size_t i = 0; i < items->size(); ++i) {
            const bool on = i == index;
            auto& it = (*items)[i];
            it.button->setBackground(on ? ColorRef("secondary_container") : ColorRef::none());
            it.icon->setFill(on)->setColor(on ? "on_secondary_container" : "on_surface_variant");
            it.label->setWeight(on ? 600 : 500)->setColor(on ? "on_secondary_container" : "on_surface_variant");
        }
        // Keep the active item in view (after the next layout)
        Button* active = (*items)[index].button;
        EventLoop::get().addTimer(30, [sideScroll, side, active] {
            const float y = active->rect().y - side->rect().y;
            sideScroll->scrollTo(y - sideScroll->rect().h / 2 + active->rect().h / 2);
        });
    };
    size_t first = 0;
    for (size_t i = 0; i < std::size(kPages); ++i) {
        auto* b = side->add<Button>(Flex::Direction::Row);
        pill(b)->setPadding({16, 12})->setSpacing(14);
        b->pressedRadius = 12;
        auto* icon = b->add<Icon>(kPages[i].icon, 22);
        icon->setColor("on_surface_variant", false);
        auto* label = b->add<Label>(kPages[i].title);
        label->setSize(15)->setWeight(500)->setColor("on_surface_variant", false);
        b->onClicked = [show, i](uint32_t) {
            // The page rebuild must not run inside the click that asked for it
            EventLoop::get().defer([show, i] { (*show)(i); });
        };
        items->push_back({b, icon, label});
        if (initial == kPages[i].id) first = i;
    }
    // Only called from inside this popup (a page's reset button)
    reshow = [show](const std::string& id) {
        for (size_t i = 0; i < std::size(kPages); ++i)
            if (id == kPages[i].id) (*show)(i);
    };
    (*show)(first);
}

} // namespace m3::shell::panels
