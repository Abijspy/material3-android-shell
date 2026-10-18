// The Settings app: sidebar navigation and pages. Settings are read from and
// written to config.json (shared with the QML shell) through Config.
#include "build_config.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Audio.h"
#include "services/Hyprland.h"
#include "services/Keybinds.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Packages.h"
#include "services/SystemStats.h"
#include "services/Wallpaper.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"

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

// A titled group of rows on one rounded surface
Flex* group(Node* page, const std::string& title, const std::string& subtitle = {}) {
    auto* head = page->add<Flex>(Flex::Direction::Column);
    head->setAlignItems(Align::Start)->setSpacing(2)->setPadding({4, 8, 4, 0});
    head->add<Label>(title)->setSize(15)->setWeight(600)->setColor("primary", false);
    if (!subtitle.empty()) head->add<Label>(subtitle)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(640)->setMaxLines(2);
    auto* g = page->add<Flex>(Flex::Direction::Column);
    g->setAlignItems(Align::Stretch)->setSpacing(0)->setRadius(Radii(24))->setBackground("surface_container_high", false);
    g->setPadding({0, 6});
    return g;
}

// One row: icon, title and subtitle, and a trailing slot for the control
struct Row {
    Flex* row;
    Label* title;
    Label* subtitle;
};

Row row(Node* group, const std::string& icon, const std::string& title, const std::string& subtitle = {}) {
    auto* r = group->add<Flex>(Flex::Direction::Row);
    r->setPadding({20, 10})->setSpacing(16);
    r->minHeight = 56;
    if (!icon.empty()) r->add<Icon>(icon, 22)->setColor("on_surface_variant", false);
    auto* texts = r->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(1);
    texts->grow = true;
    auto* t = texts->add<Label>(title);
    t->setSize(15)->setMaxWidth(420);
    auto* s = texts->add<Label>(subtitle);
    s->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(440)->setMaxLines(2);
    s->setVisible(!subtitle.empty());
    return {r, t, s};
}

Button* pillButton(Node* parent, const std::string& label, bool primary, std::function<void()> fn, const std::string& icon = {}) {
    auto* b = parent->add<Button>();
    pill(b->setBackground(primary ? "primary" : "secondary_container", false))->setPadding({18, 9})->setSpacing(8);
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

// Chips for a short list of values; the active one is filled
template <typename T>
void choiceRow(Node* g, const std::string& icon, const std::string& title, const std::string& sub, const std::string& pointer,
               T fallback, std::vector<std::pair<T, std::string>> options, std::function<void(T)> after = {}) {
    auto r = row(g, icon, title, sub);
    auto* chips = r.row->add<Flex>(Flex::Direction::Row);
    chips->setSpacing(4);
    auto current = std::make_shared<T>(Config::get().value<T>(pointer, fallback));
    auto buttons = std::make_shared<std::vector<std::pair<T, Button*>>>();
    for (const auto& [value, label] : options) {
        auto* b = chips->add<Button>();
        pill(b)->setPadding({12, 7});
        auto* l = b->add<Label>(label);
        l->setSize(13)->setWeight(500);
        buttons->push_back({value, b});
        b->onClicked = [=, v = value](uint32_t) {
            *current = v;
            Config::get().set(pointer, v);
            for (auto& [val, btn] : *buttons) {
                btn->setBackground(val == v ? ColorRef("primary") : ColorRef("surface_container_highest"));
                static_cast<Label*>(btn->children()[0].get())->setColor(val == v ? "on_primary" : "on_surface");
            }
            if (after) after(v);
        };
        b->setBackground(value == *current ? ColorRef("primary") : ColorRef("surface_container_highest"), false);
        l->setColor(value == *current ? "on_primary" : "on_surface", false);
    }
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

std::string readFirstLine(const std::string& path, const std::string& prefix = {}) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
        if (prefix.empty() || line.starts_with(prefix)) return prefix.empty() ? line : line.substr(prefix.size());
    return {};
}

// Re-run Matugen on the current wallpaper (after scheme or mode changes)
void recolour() {
    const auto p = Wallpaper::get().path();
    if (!p.empty()) Wallpaper::get().set(p);
}

// ── Pages ──

void networkPage(Flex* page, Popup&) {
    auto* holder = page->add<Flex>(Flex::Direction::Column);
    holder->setAlignItems(Align::Stretch);
    wifiView(holder, {});
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
    audio(page, popup);
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
            row(mons, "monitor", std::format("{}  ·  {}", m.value("name", ""), m.value("description", "")),
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

    auto* colour = group(page, "Colours", "Material You colours come from the wallpaper (Matugen)");
    switchRow(colour, "dark_mode", "Dark theme", "", "/background/darkmode", true, [](bool) { recolour(); });
    choiceRow<std::string>(colour, "palette", "Scheme", "", "/background/matugenScheme", "scheme-content",
                           {{"scheme-content", "Content"}, {"scheme-tonal-spot", "Tonal"}, {"scheme-expressive", "Expressive"},
                            {"scheme-vibrant", "Vibrant"}, {"scheme-monochrome", "Mono"}},
                           [](std::string) { recolour(); });

    auto* bar = group(page, "Status bar");
    choiceRow<int>(bar, "toolbar", "Style", "", "/statusBar/backgroundStyle", 0, {{0, "Floating"}, {1, "Full width"}});
    sliderRow(bar, "height", "Height", "/statusBar/height", 40, 32, 56, 2, [](double v) { return std::format("{:.0f} px", v); });
    switchRow(bar, "view_carousel", "Show workspace switches in the island", "", "/statusBar/islandWorkspace", true);
    choiceRow<std::string>(bar, "schedule", "Clock", "", "/time/format", "hh:mm",
                           {{"hh:mm", "24-hour"}, {"h:mm AP", "12-hour"}, {"hh:mm:ss", "Seconds"}});

    auto* fonts = group(page, "Fonts");
    textRow(fonts, "text_fields", "Interface", "/appearance/fonts/main", "Google Sans Flex");
    textRow(fonts, "code", "Monospace", "/appearance/fonts/monospace", "JetBrains Mono NF");
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
    const std::string logo = paths::dataFile("../assets/icons/Material3UI.svg");
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
    row(credits, "favorite", "NAnDoroid-shell by na-ive", "The project Material3UI grew from (AGPL-3.0)");
    row(credits, "architecture", "Noctalia", "The native shell follows its architecture (MIT); no code is shared");
    row(credits, "auto_awesome", "end-4, DankMaterialShell, Ambxst, pC, P3DROVFX, Vynx", "Design and interaction references");
}

struct PageDef {
    const char* id;
    const char* icon;
    const char* title;
    void (*build)(Flex*, Popup&);
};

const PageDef kPages[] = {
    {"network", "wifi", "Network", networkPage},
    {"bluetooth", "bluetooth", "Bluetooth", bluetoothPage},
    {"sound", "volume_up", "Sound", soundPage},
    {"display", "brightness_medium", "Display", displayPage},
    {"appearance", "palette", "Appearance", appearancePage},
    {"notifications", "notifications", "Notifications", notificationsPage},
    {"lock", "lock", "Lock & idle", lockPage},
    {"keybinds", "keyboard", "Keybinds", keybindsPage},
    {"packages", "deployed_code", "Packages & Updates", packagesPage},
    {"about", "info", "About", aboutPage},
};

} // namespace

void settings(Flex* card, Popup& popup, const std::string& initial) {
    card->setPadding(Insets(12))->setSpacing(0);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setAlignItems(Align::Start)->setSpacing(12);

    auto* side = columns->add<Flex>(Flex::Direction::Column);
    side->setAlignItems(Align::Stretch)->setSpacing(2)->setPadding({4, 8});
    side->fixedWidth = 232;
    side->minHeight = 660;
    auto* title = side->add<Label>("Settings");
    title->setSize(24)->setWeight(500);
    title->alignSelf = Align::Start;
    auto* gap = side->add<Node>();
    gap->minHeight = 12;

    auto* scroll = columns->add<ScrollView>();
    scroll->maxHeight = 660;
    scroll->fixedWidth = 720;
    auto* content = scroll->add<Flex>(Flex::Direction::Column);
    content->setAlignItems(Align::Stretch)->setSpacing(14)->setPadding({4, 8, 12, 8});

    auto buttons = std::make_shared<std::vector<Button*>>();
    auto show = std::make_shared<std::function<void(size_t)>>();
    *show = [content, scroll, buttons, &popup](size_t index) {
        content->clearChildren();
        auto* h = content->add<Label>(kPages[index].title);
        h->setSize(28)->setWeight(500);
        h->alignSelf = Align::Start;
        kPages[index].build(content, popup);
        scroll->scrollTo(0, false);
        for (size_t i = 0; i < buttons->size(); ++i) {
            auto* b = (*buttons)[i];
            b->setBackground(i == index ? ColorRef("secondary_container") : ColorRef::none());
            for (auto& c : b->children())
                if (auto* l = dynamic_cast<Label*>(c.get())) l->setColor(i == index ? "on_secondary_container" : "on_surface_variant");
        }
    };
    size_t first = 0;
    for (size_t i = 0; i < std::size(kPages); ++i) {
        auto* b = side->add<Button>(Flex::Direction::Row);
        pill(b)->setPadding({16, 11})->setSpacing(14);
        b->add<Icon>(kPages[i].icon, 22)->setColor("on_surface_variant", false);
        b->add<Label>(kPages[i].title)->setSize(15)->setWeight(500)->setColor("on_surface_variant", false);
        b->onClicked = [show, i](uint32_t) {
            // The page rebuild must not run inside the click that asked for it
            EventLoop::get().defer([show, i] { (*show)(i); });
        };
        buttons->push_back(b);
        if (initial == kPages[i].id) first = i;
    }
    (*show)(first);
}

} // namespace m3::shell::panels
