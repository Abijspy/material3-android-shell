#include "shell/NiriPages.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Niri.h"
#include "services/NiriConfig.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// "1920×1080 at 60 Hz"
std::string modeText(const niri::Model::Mode& m) {
    return std::format("{}×{} at {:.0f} Hz", m.width, m.height, m.refresh / 1000.0);
}

// The config's spelling of a mode ("1920x1080@60.000") and IPC's
nlohmann::json modeAction(const niri::Model::Mode& m) {
    return {{"Mode", {{"mode", {{"Specific", {{"width", m.width}, {"height", m.height}, {"refresh", m.refresh / 1000.0}}}}}}}};
}

std::string modeConfig(const niri::Model::Mode& m) {
    return std::format("{}x{}@{:.3f}", m.width, m.height, m.refresh / 1000.0);
}

// What's kept for an output now, to change one thing in it
niri::Settings::Output saved(const niri::Model::Out& o) {
    niri::Settings::Output s;
    s.name = o.name;
    const auto j = Config::get().value<nlohmann::json>("/niri/outputs/" + o.name, nlohmann::json::object());
    s.off = j.value("off", false);
    s.mode = j.value("mode", "");
    s.scale = j.value("scale", 0.0);
    s.transform = j.value("transform", "");
    if (j.contains("x") && j.contains("y")) s.position = std::pair{j.value("x", 0), j.value("y", 0)};
    s.vrr = j.value("vrr", false);
    return s;
}

} // namespace

void niriNotice(Flex* page) {
    auto& nc = NiriConfig::get();
    if (nc.included() && nc.error().empty()) return;
    auto* g = group(page, "Not reaching niri yet");
    if (!nc.included()) {
        auto r = row(g, "warning", "material3ui.kdl isn't included",
                     "These settings are written for niri, but its config doesn't read them. See Material3UI on niri (docs/NIRI.md)");
        pillButton(r.row, "Open niri's folder", false, [] { spawn("xdg-open \"${XDG_CONFIG_HOME:-$HOME/.config}/niri\""); }, "folder_open");
    } else {
        row(g, "error", "niri refused its config", nc.error());
    }
}

void niriMouse(Flex* page) {
    niriNotice(page);
    auto* tp = group(page, "Touchpad");
    switchRow(tp, "touch_app", "Tap to click", "A tap is a click; two fingers right-click", "/input/tapToClick", true);
    switchRow(tp, "swap_vert", "Natural scrolling", "Content follows your fingers", "/input/naturalScroll", true);
    switchRow(tp, "keyboard", "Ignore while typing", "No stray taps from the palm", "/input/disableWhileTyping", true);
    switchRow(tp, "back_hand", "Click with fingers", "Two fingers to right-click, three for the middle button (else the pad's corners)",
              "/input/clickFinger", true);
    switchRow(tp, "drag_pan", "Tap and drag", "Tap, then hold and move to drag", "/input/tapAndDrag", true);
    switchRow(tp, "lock", "Drag lock", "Lift your finger in a drag without dropping it", "/input/dragLock", false);
    sliderRow(tp, "speed", "Scrolling speed", "/input/scrollFactor", 0.7, 0.1, 2.0, 0.05, [](double v) { return std::format("{:.2f}×", v); });

    auto* pt = group(page, "Pointer", "The touchpad's and mice's");
    sliderRow(pt, "mouse", "Pointer speed", "/input/sensitivity", 0.0, -1.0, 1.0, 0.05,
              [](double v) { return v == 0 ? std::string("Default") : std::format("{:+.2f}", v); });
    choiceRow<std::string>(pt, "trending_up", "Acceleration", "Faster moves go further, or one speed always", "/input/accelProfile",
                           "adaptive", {{"adaptive", "Adaptive"}, {"flat", "Flat"}});
    switchRow(pt, "swap_vert", "Natural scrolling with a mouse", "The wheel scrolls the way content moves", "/input/mouseNaturalScroll", false);

    auto* more = group(page, "More");
    auto r = row(more, "swipe_vertical", "Scrolling in the shell", "Smooth scrolling, speed and touchpad glide");
    pillButton(r.row, "Look & feel", false, [] { settingsPage("style"); }, "open_in_new");
}

void niriKeyboard(Flex* page) {
    niriNotice(page);
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->watch(Niri::get().layoutsChanged);
    live->sync = [live] {
        live->clearChildren();
        const auto& m = Niri::get().model();
        auto* g = group(live, "Layouts", "The keyboard layouts niri switches between");
        std::string now = m.layouts.empty() ? std::string("One layout") : m.layouts[static_cast<size_t>(std::clamp(m.layout, 0, static_cast<int>(m.layouts.size()) - 1))];
        std::string all;
        for (const auto& l : m.layouts) all += (all.empty() ? "" : " · ") + l;
        auto cur = row(g, "keyboard_alt", now, m.layouts.size() > 1 ? "In use now · " + all : all);
        if (m.layouts.size() > 1)
            pillButton(cur.row, "Next layout", false, [] { Niri::get().action({{"SwitchLayout", {{"layout", "Next"}}}}); }, "keyboard_tab");
        textRow(g, "language", "Layouts", "/keyboard/layouts", "");
        auto* hint = g->add<Flex>(Flex::Direction::Row);
        hint->setPadding({20, 0, 20, 10});
        caption(hint, "XKB names, comma-separated: us, de, fr, in(eng)… Empty leaves niri's own");
        choiceRow<std::string>(g, "keyboard_command_key", "Switch with", "", "/keyboard/options", "",
                               {{"", "niri's"}, {"grp:alt_shift_toggle", "Alt+Shift"}, {"grp:ctrl_shift_toggle", "Ctrl+Shift"},
                                {"grp:caps_toggle", "Caps Lock"}});
    };
    live->sync();

    auto* typing = group(page, "Typing", "How soon a held key repeats, and how fast");
    sliderRow(typing, "timer", "Repeat delay", "/keyboard/repeatDelay", 600, 150, 1000, 25, [](double v) { return std::format("{:.0f} ms", v); });
    sliderRow(typing, "speed", "Repeat rate", "/keyboard/repeatRate", 25, 5, 60, 1, [](double v) { return std::format("{:.0f} a second", v); });

    // Your own keys: a command each, written as niri binds
    auto* keys = page->add<Live>();
    keys->setSpacing(0);
    keys->conns.push_back(Config::get().changed.connect([keys] { keys->syncSoon(); }));
    auto draft = std::make_shared<std::pair<std::string, std::string>>();
    keys->sync = [keys, draft] {
        keys->clearChildren();
        auto* g = group(keys, "Shortcuts",
                        "The shell's own (Spotlight, Quick Settings, the switcher…) are in material3ui.kdl; add yours here");
        const auto list = Config::get().value<nlohmann::json>("/niri/binds", nlohmann::json::array());
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& b = list[i];
            if (!b.is_object()) continue;
            auto r = row(g, "keyboard_command_key", b.value("keys", ""), b.value("command", ""));
            confirmButton(r.row, "niribind:" + std::to_string(i), "delete", [i] {
                auto l = Config::get().value<nlohmann::json>("/niri/binds", nlohmann::json::array());
                if (i < l.size()) l.erase(l.begin() + static_cast<long>(i));
                Config::get().set("/niri/binds", l);
            })->tooltip = "Remove";
        }
        auto* add = g->add<Flex>(Flex::Direction::Column);
        add->setPadding({20, 12})->setSpacing(8)->setAlignItems(Align::Stretch);
        auto* line = add->add<Flex>(Flex::Direction::Row);
        line->setSpacing(8);
        auto* combo = line->add<TextField>();
        combo->setPlaceholder("Mod+Shift+K");
        combo->setText(draft->first);
        combo->fixedWidth = 170;
        combo->onChanged = [draft](const std::string& t) { draft->first = t; };
        auto* cmd = line->add<TextField>();
        cmd->setPlaceholder("A command: firefox, or material3ui msg …");
        cmd->setText(draft->second);
        cmd->grow = true;
        cmd->onChanged = [draft](const std::string& t) { draft->second = t; };
        auto* problem = add->add<Label>("");
        problem->setSize(12)->setColor("error", false);
        problem->setVisible(false);
        auto save = [draft, problem] {
            if (!niri::validKeys(draft->first) || draft->second.empty()) {
                problem->setText("Keys like Mod+Shift+K (Mod is Super), and a command");
                problem->setVisible(true);
                return;
            }
            auto l = Config::get().value<nlohmann::json>("/niri/binds", nlohmann::json::array());
            l.push_back({{"keys", draft->first}, {"command", draft->second}});
            Config::get().set("/niri/binds", l);
            draft->first.clear();
            draft->second.clear();
        };
        cmd->onSubmit = [save](const std::string&) { save(); };
        auto* buttons = add->add<Flex>(Flex::Direction::Row);
        buttons->setJustify(Align::End);
        pillButton(buttons, "Add shortcut", true, save, "add");
    };
    keys->sync();
}

void niriMonitors(Flex* page) {
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->watch(Niri::get().outputsChanged);
    live->sync = [live] {
        live->clearChildren();
        const auto& outs = Niri::get().model().outputs;
        auto* g = group(live, "Monitors", "Applied at once, and kept for the next login");
        if (outs.empty()) row(g, "monitor", "No monitors found");
        for (const auto& o : outs) {
            const std::string name = o.name;
            std::string about = o.make.empty() ? o.model : o.make + " " + o.model;
            if (o.on && o.current >= 0 && o.current < static_cast<int>(o.modes.size()))
                about += (about.empty() ? "" : " · ") + modeText(o.modes[static_cast<size_t>(o.current)]) + std::format(" · scale {:g}", o.scale);
            if (!o.on) about += (about.empty() ? "" : " · ") + std::string("Off");
            auto head = row(g, "monitor", name, about);
            if (outs.size() > 1) {
                auto* sw = head.row->add<Switch>();
                sw->setChecked(o.on, false);
                sw->onToggled = [o](bool on) {
                    auto s = saved(o);
                    s.off = !on;
                    NiriConfig::saveOutput(s);
                    Niri::get().outputAction(o.name, on ? nlohmann::json("On") : nlohmann::json("Off"));
                };
            }
            if (!o.on) continue;
            auto* box = g->add<Flex>(Flex::Direction::Column);
            box->setAlignItems(Align::Stretch);
            // Resolution and refresh: the modes it offers, best first as niri lists them
            std::vector<std::pair<std::string, std::string>> modes;
            for (size_t i = 0; i < o.modes.size() && i < 24; ++i)
                modes.emplace_back(std::to_string(i), modeText(o.modes[i]) + (o.modes[i].preferred ? " (preferred)" : ""));
            auto res = row(box, "aspect_ratio", "Resolution");
            const std::string shown = o.current >= 0 && o.current < static_cast<int>(o.modes.size()) ? modeText(o.modes[static_cast<size_t>(o.current)]) : "Automatic";
            dropdown(res.row, box, shown, modes, [o](const std::string& v) {
                const auto& m = o.modes[static_cast<size_t>(std::stoi(v))];
                auto s = saved(o);
                s.mode = modeConfig(m);
                NiriConfig::saveOutput(s);
                Niri::get().outputAction(o.name, modeAction(m));
            });
            auto sc = row(box, "zoom_in", "Scale");
            connectedGroup<double>(sc.row, std::round(o.scale * 4) / 4, {{1.0, "100%"}, {1.25, "125%"}, {1.5, "150%"}, {1.75, "175%"}, {2.0, "200%"}},
                                   [o](double v) {
                                       auto s = saved(o);
                                       s.scale = v;
                                       NiriConfig::saveOutput(s);
                                       Niri::get().outputAction(o.name, {{"Scale", {{"scale", {{"Specific", v}}}}}});
                                   });
            auto rot = row(box, "screen_rotation", "Rotation");
            const std::string turned = o.transform == "90" || o.transform == "180" || o.transform == "270" ? o.transform : "Normal";
            connectedGroup<std::string>(rot.row, turned, {{"Normal", "None"}, {"90", "90°"}, {"180", "180°"}, {"270", "270°"}},
                                        [o](std::string v) {
                                            auto s = saved(o);
                                            s.transform = v == "Normal" ? "normal" : v;
                                            NiriConfig::saveOutput(s);
                                            Niri::get().outputAction(o.name, {{"Transform", {{"transform", v}}}});
                                        });
            if (o.vrrSupported) {
                auto v = row(box, "speed", "Variable refresh rate", "Smoother games and video where the screen can follow");
                auto* sw = v.row->add<Switch>();
                sw->setChecked(o.vrr, false);
                sw->onToggled = [o](bool on) {
                    auto s = saved(o);
                    s.vrr = on;
                    NiriConfig::saveOutput(s);
                    Niri::get().outputAction(o.name, {{"Vrr", {{"vrr", {{"vrr", on}, {"on_demand", false}}}}}});
                };
            }
            if (NiriConfig::get().userSetsOutput(o.name)) {
                auto* note = box->add<Flex>(Flex::Direction::Row);
                note->setPadding({20, 4, 20, 12});
                caption(note, "niri's config.kdl sets this monitor too: its settings come back when niri reloads; remove its output block to keep these");
            }
        }
    };
    live->sync();
}

} // namespace m3::shell::panels
