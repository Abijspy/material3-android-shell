#include "shell/NiriPages.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Compositor.h"
#include "services/Niri.h"
#include "services/NiriConfig.h"
#include "services/Search.h"
#include "shell/CurveEditor.h"
#include "shell/MonitorLayout.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <regex>
#include <set>

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
            const std::string action = b.value("action", "");
            auto r = row(g, "keyboard_command_key", b.value("keys", ""), action.empty() ? b.value("command", "") : niriActionLabel(action));
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
        // Side by side: drag them where they stand on your desk (niri's
        // logical layout, snapping to each other's edges)
        std::vector<MonitorLayout::Screen> screens;
        for (const auto& o : outs)
            if (o.on && o.w > 0 && o.h > 0)
                screens.push_back({o.name, o.name, o.make.empty() ? o.model : o.make + " " + o.model, o.x, o.y, o.w, o.h, false});
        if (screens.size() > 1) {
            auto* arr = group(live, "Arrangement", "Drag a monitor to where it stands; the pointer crosses where their edges meet");
            auto* box = arr->add<Flex>(Flex::Direction::Column);
            box->setPadding({20, 12})->setAlignItems(Align::Stretch);
            auto* layout = box->add<MonitorLayout>(screens);
            layout->onMoved = [](const std::string& name, int x, int y) {
                for (const auto& o : Niri::get().model().outputs) {
                    if (o.name != name) continue;
                    auto s = saved(o);
                    s.position = std::pair{x, y};
                    NiriConfig::saveOutput(s);
                }
                Niri::get().outputAction(name, {{"Position", {{"position", {{"Specific", {{"x", x}, {"y", y}}}}}}}});
            };
            auto r = row(arr, "auto_awesome_mosaic", "Arrange automatically", "niri places them left to right");
            pillButton(r.row, "Automatic", false, [] {
                for (const auto& o : Niri::get().model().outputs) {
                    auto s = saved(o);
                    if (!s.position) continue;
                    s.position.reset();
                    NiriConfig::saveOutput(s);
                    Niri::get().outputAction(o.name, {{"Position", {{"position", "Automatic"}}}});
                }
            });
        }
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

// ── Niri Settings ────────────────────────────────────────────────────────────

namespace {

const std::string kLook = "/niri/look/";

std::string px(double v) {
    return std::format("{:.0f} px", v);
}

// The option a rule for every window sets (one without a match), last
// such rule winning
std::optional<double> everyWindow(const std::vector<kdl::Node>& doc, std::string_view option) {
    std::optional<double> out;
    for (const auto& n : doc) {
        if (n.name != "window-rule") continue;
        const bool matches = std::ranges::any_of(n.children, [](const kdl::Node& c) { return c.name == "match" || c.name == "exclude"; });
        if (matches) continue;
        for (const auto& c : n.children)
            if (c.name == option && !c.args.empty() && c.args[0].type == kdl::Value::Type::Number) out = c.args[0].number;
    }
    return out;
}

// A flag niri can only switch on: while your config has it, a note in
// place of the switch (here it couldn't turn it off)
void oneWay(Node* g, const std::string& icon, const std::string& title, const std::string& sub, bool yours, const std::string& key,
            bool fallback, const std::string& lockedNote) {
    if (yours) row(g, icon, title, lockedNote);
    else switchRow(g, icon, title, sub, kLook + key, fallback);
}

// The page's options back to config.kdl's, and the page again
void resetGroup(Flex* page, const std::string& id, std::vector<std::string> keys) {
    auto* g = group(page, "Your config");
    auto r = row(g, "restart_alt", "Back to config.kdl", "Forget what's set on this page: niri takes these from your config.kdl again");
    pillButton(r.row, "Reset", false, [id, keys] {
        NiriConfig::resetLook(keys);
        EventLoop::get().defer([id] { settingsPage(id); });
    });
}

} // namespace

void niriLayoutPage(Flex* page, Popup&) {
    niriNotice(page);
    const auto doc = NiriConfig::get().yours();
    auto* cols = group(page, "Columns", "What's set here wins over niri's config.kdl");
    sliderRow(cols, "space_bar", "Gaps", kLook + "gaps", kdl::number(doc, {"layout", "gaps"}).value_or(16), 0, 64, 1, px);
    choiceRow<std::string>(cols, "center_focus_strong", "Centre the focused column", "Scroll so the column you're in sits in the middle",
                           kLook + "centerColumn", kdl::string(doc, {"layout", "center-focused-column"}).value_or("never"),
                           {{"never", "Never"}, {"on-overflow", "If it won't fit"}, {"always", "Always"}});
    switchRow(cols, "align_horizontal_center", "Centre a lone column", "A workspace's only column in the middle of the screen",
              kLook + "centerSingle", kdl::flag(doc, {"layout", "always-center-single-column"}).value_or(false));
    // The nearest of the choices to your config's width
    const double width = kdl::number(doc, {"layout", "default-column-width", "proportion"}).value_or(0.5);
    const std::vector<std::pair<double, std::string>> widths = {{1.0 / 3, "A third"}, {0.5, "Half"}, {2.0 / 3, "Two thirds"}, {1.0, "Full"}};
    const double nearest = std::ranges::min(widths, {}, [width](const auto& w) { return std::abs(w.first - width); }).first;
    choiceRow<double>(cols, "view_column", "New windows", "How wide a new window's column opens", kLook + "columnWidth", nearest, widths);

    auto* edges = group(page, "Space at the edges", "Kept free around the windows, besides the bar's own");
    const std::pair<const char*, const char*> sides[] = {{"top", "Top"}, {"bottom", "Bottom"}, {"left", "Left"}, {"right", "Right"}};
    for (const auto& [side, name] : sides) {
        const std::string key = std::string("strut") + static_cast<char>(std::toupper(static_cast<unsigned char>(side[0]))) + (side + 1);
        sliderRow(edges, std::string("border_") + side, name, kLook + key, kdl::number(doc, {"layout", "struts", side}).value_or(0), 0, 200, 4, px);
    }
    resetGroup(page, "niri-layout",
               {"gaps", "centerColumn", "centerSingle", "columnWidth", "strutTop", "strutBottom", "strutLeft", "strutRight"});
}

void niriDecorationPage(Flex* page, Popup&) {
    niriNotice(page);
    const auto doc = NiriConfig::get().yours();
    auto* ring = group(page, "Focus ring", "Around the focused window, outside it");
    switchRow(ring, "filter_center_focus", "Focus ring", "Show which window has the focus", kLook + "ringOn",
              kdl::onOff(doc, {"layout", "focus-ring"}).value_or(true));
    sliderRow(ring, "line_weight", "Width", kLook + "ringWidth", kdl::number(doc, {"layout", "focus-ring", "width"}).value_or(4), 1, 16, 1, px);

    auto* border = group(page, "Border", "Around every window, taking room from it");
    switchRow(border, "border_outer", "Border", "A line around each window, brighter on the focused one", kLook + "borderOn",
              kdl::onOff(doc, {"layout", "border"}).value_or(false));
    sliderRow(border, "line_weight", "Width", kLook + "borderWidth", kdl::number(doc, {"layout", "border", "width"}).value_or(4), 1, 16, 1, px);
    switchRow(border, "palette", "Wallpaper colours", "The ring and border in the palette's colours, else your config's", "/niri/focusRingColors", true);

    auto* shadow = group(page, "Shadow", "Behind each window");
    switchRow(shadow, "ev_shadow", "Shadow", "Soft shadows lift windows off the wallpaper", kLook + "shadowOn",
              kdl::onOff(doc, {"layout", "shadow"}).value_or(false));
    sliderRow(shadow, "blur_on", "Softness", kLook + "shadowSoftness", kdl::number(doc, {"layout", "shadow", "softness"}).value_or(30), 0, 100, 1, px);
    sliderRow(shadow, "open_with", "Spread", kLook + "shadowSpread", kdl::number(doc, {"layout", "shadow", "spread"}).value_or(5), 0, 40, 1, px);
    double offset = 5;
    if (const auto* o = kdl::last(doc, {"layout", "shadow", "offset"}); o && o->prop("y")) offset = o->prop("y")->number;
    sliderRow(shadow, "vertical_align_bottom", "Drop", kLook + "shadowOffset", offset, 0, 40, 1, px);

    // niri's own blur (26.04): behind the shell's panels and windows, and any
    // app that asks for it (foot, kitty…) or that a rule blurs
    auto* blur = group(page, "Blur", "Behind the shell's bar, panels and windows, and apps that ask for it");
    switchRow(blur, "blur_on", "Blur", "niri's blur, for everything blurred", kLook + "blurOn", !kdl::flag(doc, {"blur", "off"}).value_or(false));
    sliderRow(blur, "layers", "Passes", kLook + "blurPasses", kdl::number(doc, {"blur", "passes"}).value_or(3), 1, 6, 1,
              [](double v) { return std::format("{:.0f}", v); });
    sliderRow(blur, "blur_medium", "Smoothness", kLook + "blurOffset", kdl::number(doc, {"blur", "offset"}).value_or(3), 1, 10, 0.5,
              [](double v) { return std::format("{:.1f}", v); });
    sliderRow(blur, "grain", "Noise", kLook + "blurNoise", kdl::number(doc, {"blur", "noise"}).value_or(0.02), 0, 0.1, 0.005,
              [](double v) { return std::format("{:.3f}", v); });
    sliderRow(blur, "palette", "Saturation", kLook + "blurSaturation", kdl::number(doc, {"blur", "saturation"}).value_or(1.5), 0, 3, 0.05,
              [](double v) { return std::format("{:.0f}%", v * 100); });

    auto* corners = group(page, "Corners");
    sliderRow(corners, "rounded_corner", "Window corners", kLook + "cornerRadius", everyWindow(doc, "geometry-corner-radius").value_or(0), 0, 32, 1,
              [](double v) { return v < 0.5 ? std::string("Square") : px(v); });
    resetGroup(page, "niri-decoration",
               {"ringOn", "ringWidth", "borderOn", "borderWidth", "shadowOn", "shadowSoftness", "shadowSpread", "shadowOffset", "blurOn", "blurPasses",
                "blurOffset", "blurNoise", "blurSaturation", "cornerRadius"});
}

void niriAnimationsPage(Flex* page, Popup&) {
    niriNotice(page);
    const auto doc = NiriConfig::get().yours();
    auto* g = group(page, "Animations", "All of them at once; each one's own below");
    switchRow(g, "animation", "Animations", "Windows, workspaces and the overview move into place", kLook + "animations",
              kdl::onOff(doc, {"animations"}).value_or(true));
    sliderRow(g, "slow_motion_video", "Speed", kLook + "slowdown", kdl::number(doc, {"animations", "slowdown"}).value_or(1.0), 0.25, 4.0, 0.05,
              [](double v) {
                  if (std::abs(v - 1) < 0.01) return std::string("Normal");
                  return v > 1 ? std::format("{:.2f}× slower", v) : std::format("{:.2f}× faster", 1 / v);
              });

    // Each animation: on or off, and its motion, a spring or a curve (a
    // custom one drawn in the curve editor). Rebuilt when the motion or the
    // curve changes, which decide its rows; the sliders write as they move
    auto* each = page->add<Live>();
    each->setSpacing(0);
    each->sync = [each, doc] {
        each->clearChildren();
        for (const auto& d : niri::animationDefs()) {
            const std::string base = kLook + "anim/" + d.name + "/";
            const auto mine = Config::get().value<nlohmann::json>("/niri/look/anim/" + std::string(d.name), nlohmann::json::object());
            // Yours, from config.kdl (its last section for this animation)
            bool yoursOff = false;
            std::string kind = d.spring ? "spring" : "easing", curve = d.curve;
            double damping = d.damping, stiffness = d.stiffness, duration = d.duration;
            Bezier bezier{0.05, 0.7, 0.1, 1};
            if (const kdl::Node* n = kdl::last(doc, {"animations", d.name}))
                for (const auto& c : n->children) {
                    if (c.name == "off") yoursOff = true;
                    if (c.name == "spring") {
                        kind = "spring";
                        if (const auto* v = c.prop("damping-ratio")) damping = v->number;
                        if (const auto* v = c.prop("stiffness")) stiffness = v->number;
                    }
                    if (c.name == "duration-ms" && !c.args.empty()) {
                        kind = "easing";
                        duration = c.args[0].number;
                    }
                    if (c.name == "curve" && !c.args.empty()) {
                        kind = "easing";
                        curve = c.args[0].text;
                        if (curve == "cubic-bezier" && c.args.size() == 5)
                            for (size_t i = 0; i < 4; ++i) bezier[i] = c.args[i + 1].number;
                    }
                }
            // Then what's set here
            kind = mine.value("kind", kind);
            curve = mine.value("curve", curve);
            if (mine.contains("bezier") && mine["bezier"].is_array() && mine["bezier"].size() == 4)
                for (size_t i = 0; i < 4; ++i) bezier[i] = mine["bezier"][i].get<double>();

            auto* sec = group(each, d.title);
            if (yoursOff) {
                row(sec, "motion_photos_off", "Off in your config.kdl", "niri can't switch one animation back on from here");
                continue;
            }
            switchRow(sec, "animation", "On", "", base + "on", true);
            // A slider moved means this motion: kept with it
            auto keep = [base, kind](double) { Config::get().set(base + "kind", kind); };
            choiceRow<std::string>(sec, "tune", "Motion", "A curve over a set time, or a spring that settles", base + "kind", kind,
                                   {{"easing", "Curve"}, {"spring", "Spring"}}, [each](const std::string&) { each->syncSoon(); });
            if (kind == "spring") {
                sliderRow(sec, "compress", "Bounce", base + "damping", mine.value("damping", damping), 0.1, 1.0, 0.05,
                          [](double v) { return v > 0.99 ? std::string("None") : std::format("{:.0f}%", (1 - v) * 100); }, keep);
                sliderRow(sec, "speed", "Stiffness", base + "stiffness", mine.value("stiffness", stiffness), 100, 2000, 50,
                          [](double v) { return std::format("{:.0f}", v); }, keep);
                continue;
            }
            sliderRow(sec, "timer", "Length", base + "duration", mine.value("duration", duration), 50, 1000, 10,
                      [](double v) { return std::format("{:.0f} ms", v); }, keep);
            choiceRow<std::string>(sec, "show_chart", "Curve", "How it eases into place", base + "curve", curve,
                                   {{"linear", "Linear"}, {"ease-out-quad", "Gentle"}, {"ease-out-cubic", "Smooth"}, {"ease-out-expo", "Swift"},
                                    {"cubic-bezier", "Custom"}},
                                   [each, base](const std::string&) {
                                       Config::get().set(base + "kind", "easing");
                                       each->syncSoon();
                                   });
            if (curve != "cubic-bezier") continue;
            // Your own curve: drag its two points
            auto* box = sec->add<Flex>(Flex::Direction::Column);
            box->setPadding({20, 12})->setSpacing(10)->setAlignItems(Align::Stretch);
            auto shape = std::make_shared<Bezier>(bezier);
            auto* editor = box->add<CurveEditor>(shape, 200);
            const double seconds = mine.value("duration", duration) / 1000.0;
            box->add<CurvePreview>(shape, [seconds] { return seconds; });
            editor->onCommitted = [shape, base] {
                Config::get().set(base + "bezier", nlohmann::json{(*shape)[0], (*shape)[1], (*shape)[2], (*shape)[3]});
                Config::get().set(base + "kind", "easing");
            };
        }
    };
    each->sync();
    resetGroup(page, "niri-animations", {"animations", "slowdown", "anim"});
}

void niriBehaviourPage(Flex* page, Popup&) {
    niriNotice(page);
    const auto doc = NiriConfig::get().yours();
    auto* focus = group(page, "Focus");
    oneWay(focus, "mouse", "Focus follows the mouse", "The window under the pointer takes the focus",
           kdl::last(doc, {"input", "focus-follows-mouse"}) != nullptr, "focusFollowsMouse", false,
           "On in your config.kdl (niri can't switch it off from here)");
    oneWay(focus, "near_me", "Pointer follows the focus", "The pointer jumps to a window focused from the keyboard",
           kdl::last(doc, {"input", "warp-mouse-to-focus"}) != nullptr, "warpMouse", false,
           "On in your config.kdl (niri can't switch it off from here)");
    switchRow(focus, "swap_horiz", "Back and forth", "Going to the workspace you're on goes back to the one before", kLook + "backAndForth",
              kdl::flag(doc, {"input", "workspace-auto-back-and-forth"}).value_or(false));
    // Takes only "off", like Hyprland 0.57's input:tablet:enabled
    oneWay(focus, "stylus_note", "Drawing tablets", "Pens and tablets move the pointer and draw",
           kdl::flag(doc, {"input", "tablet", "off"}).value_or(false), "tablets", true,
           "Off in your config.kdl (niri can't switch them on from here)");

    auto* windows = group(page, "Windows");
    switchRow(windows, "web_asset", "niri's borders for every app", "Ask apps not to draw their own title bars and shadows", kLook + "preferNoCsd",
              kdl::flag(doc, {"prefer-no-csd"}).value_or(false));

    auto* overview = group(page, "Overview");
    // hot-corners takes only "off"
    bool cornerOff = false;
    for (const auto& n : doc)
        if (n.name == "gestures")
            for (const auto& c : n.children)
                if (c.name == "hot-corners")
                    for (const auto& f : c.children) cornerOff = cornerOff || f.name == "off";
    oneWay(overview, "north_west", "Hot corner", "Push the pointer into the top-left corner to open the overview", cornerOff, "hotCorner", true,
           "Off in your config.kdl (niri can't switch it on from here)");
    sliderRow(overview, "zoom_out", "Overview size", kLook + "overviewZoom", kdl::number(doc, {"overview", "zoom"}).value_or(0.5), 0.1, 0.75, 0.05,
              [](double v) { return std::format("{:.0f}%", v * 100); });
    switchRow(overview, "blur_on", "Blurred backdrop", "The wallpaper, blurred, behind the workspaces in the overview", kLook + "blurredOverview",
              true);

    auto* start = group(page, "Starting niri");
    switchRow(start, "keyboard", "Important keys at login", "niri's list of keys when it starts", kLook + "hotkeyHelp",
              !kdl::flag(doc, {"hotkey-overlay", "skip-at-startup"}).value_or(false));
    resetGroup(page, "niri-behaviour",
               {"focusFollowsMouse", "warpMouse", "backAndForth", "tablets", "preferNoCsd", "hotCorner", "overviewZoom", "blurredOverview",
                "hotkeyHelp"});
}

namespace {

// The config's list at `pointer`, with item `i` taken out
void removeAt(const std::string& pointer, size_t i) {
    auto l = Config::get().value<nlohmann::json>(pointer, nlohmann::json::array());
    if (l.is_array() && i < l.size()) l.erase(l.begin() + static_cast<long>(i));
    Config::get().set(pointer, l);
}

void append(const std::string& pointer, nlohmann::json item) {
    auto l = Config::get().value<nlohmann::json>(pointer, nlohmann::json::array());
    if (!l.is_array()) l = nlohmann::json::array();
    l.push_back(std::move(item));
    Config::get().set(pointer, l);
}

// A KDL node's arguments as one line ("alacritty -e btop")
std::string argsText(const kdl::Node& n) {
    std::string out;
    for (const auto& a : n.args) {
        if (!out.empty()) out += ' ';
        out += a.type == kdl::Value::Type::Null ? "null" : a.text;
    }
    return out;
}

// An app's command for running it: the Exec line without %U and the like
std::string runnable(const std::string& exec) {
    return std::regex_replace(std::regex_replace(exec, std::regex(" ?%[a-zA-Z]"), ""), std::regex("\\s+$"), "");
}

} // namespace

const std::vector<std::pair<std::string, std::string>>& niriActions() {
    static const std::vector<std::pair<std::string, std::string>> actions = {
        {"close-window", "Close the window"},
        {"maximize-column", "Maximise the column"},
        {"maximize-window-to-edges", "Maximise the window to the screen's edges"},
        {"fullscreen-window", "Full screen"},
        {"toggle-windowed-fullscreen", "Full screen, inside the window"},
        {"toggle-window-floating", "Float or tile the window"},
        {"switch-focus-between-floating-and-tiling", "Focus the floating or the tiled windows"},
        {"center-column", "Centre the column"},
        {"center-visible-columns", "Centre the visible columns"},
        {"switch-preset-column-width", "Next column width"},
        {"switch-preset-window-height", "Next window height"},
        {"reset-window-height", "Reset the window's height"},
        {"expand-column-to-available-width", "Widen the column into the free space"},
        {"toggle-column-tabbed-display", "Tabs for the column's windows"},
        {"consume-or-expel-window-left", "Window into or out of the column on the left"},
        {"consume-or-expel-window-right", "Window into or out of the column on the right"},
        {"focus-column-left", "Focus the column on the left"},
        {"focus-column-right", "Focus the column on the right"},
        {"focus-column-first", "Focus the first column"},
        {"focus-column-last", "Focus the last column"},
        {"focus-window-up", "Focus the window above"},
        {"focus-window-down", "Focus the window below"},
        {"move-column-left", "Move the column left"},
        {"move-column-right", "Move the column right"},
        {"move-window-up", "Move the window up"},
        {"move-window-down", "Move the window down"},
        {"focus-workspace-up", "The workspace above"},
        {"focus-workspace-down", "The workspace below"},
        {"focus-workspace-previous", "The workspace before this one"},
        {"move-column-to-workspace-up", "Move the column to the workspace above"},
        {"move-column-to-workspace-down", "Move the column to the workspace below"},
        {"move-workspace-up", "Move the workspace up"},
        {"move-workspace-down", "Move the workspace down"},
        {"focus-monitor-left", "Focus the monitor on the left"},
        {"focus-monitor-right", "Focus the monitor on the right"},
        {"move-column-to-monitor-left", "Move the column to the monitor on the left"},
        {"move-column-to-monitor-right", "Move the column to the monitor on the right"},
        {"toggle-overview", "Open or close the overview"},
        {"screenshot", "niri's screenshot tool"},
        {"screenshot-screen", "Screenshot the screen (niri's)"},
        {"screenshot-window", "Screenshot the window (niri's)"},
        {"show-hotkey-overlay", "niri's list of keys"},
        {"power-off-monitors", "Turn the screens off"},
        {"toggle-keyboard-shortcuts-inhibit", "Let the app have every key"},
        {"quit", "Quit niri"},
    };
    return actions;
}

std::string niriActionLabel(const std::string& action) {
    for (const auto& [a, label] : niriActions())
        if (a == action) return label;
    return action;
}

void niriRulesPage(Flex* page, Popup&) {
    niriNotice(page);
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto problem = std::make_shared<std::string>();
    // Rebuilt when a rule comes or goes; the controls write their own values
    live->sync = [live, problem] {
        live->clearChildren();
        const auto rules = Config::get().value<nlohmann::json>("/niri/rules", nlohmann::json::array());
        const std::vector<std::pair<double, std::string>> widths = {
            {0, "As it is"}, {1.0 / 3, "A third"}, {0.5, "Half"}, {2.0 / 3, "Two thirds"}, {1.0, "Full"}};
        for (size_t i = 0; i < rules.size(); ++i) {
            const auto& r = rules[i];
            if (!r.is_object()) continue;
            const std::string base = "/niri/rules/" + std::to_string(i) + "/", app = r.value("appId", ""), title = r.value("title", "");
            const std::string label = r.value("label", app.empty() ? title : app);
            auto* g = group(live, label, app.empty() ? "Windows whose title matches " + title : "Windows whose app id matches " + app);
            switchRow(g, "picture_in_picture", "Open floating", "Over the columns rather than in them", base + "floating", r.value("floating", false));
            switchRow(g, "width_full", "Open maximised", "Its column as wide as the screen", base + "maximized", r.value("maximized", false));
            switchRow(g, "fullscreen", "Open full screen", "", base + "fullscreen", r.value("fullscreen", false));
            const double w = r.value("width", 0.0);
            const double nearest = std::ranges::min(widths, {}, [w](const auto& o) { return std::abs(o.first - w); }).first;
            choiceRow<double>(g, "view_column", "Width", "Its column's, when it opens", base + "width", nearest, widths);
            sliderRow(g, "opacity", "Opacity", base + "opacity", r.value("opacity", 1.0), 0.2, 1.0, 0.05,
                      [](double v) { return v > 0.995 ? std::string("Opaque") : std::format("{:.0f}%", v * 100); });
            switchRow(g, "visibility_off", "Hide from screen sharing", "Blacked out when apps record or share the screen", base + "hide",
                      r.value("hide", false));
            // From the wallpaper's palette, so it changes with it
            choiceRow<std::string>(g, "border_color", "Border colour", "Its border and focus ring, dimmer when it isn't focused", base + "color",
                                   r.value("color", ""),
                                   {{"", "As it is"}, {"primary", "Primary"}, {"secondary", "Secondary"}, {"tertiary", "Tertiary"}, {"error", "Red"}});
            auto rm = row(g, "delete", "Remove this rule");
            confirmButton(rm.row, "nirirule:" + std::to_string(i) + ":" + label, "delete", [live, i] {
                removeAt("/niri/rules", i);
                live->syncSoon();
            })->tooltip = "Remove";
        }

        auto* add = group(live, rules.empty() ? "Your rules" : "Another rule",
                          "How an app's windows open; a rule here wins over config.kdl's for what it sets");
        // The apps with a window open
        std::vector<std::pair<std::string, std::string>> open;
        std::set<std::string> seen;
        for (const auto& w : Compositor::get().windows(true))
            if (!w.appId.empty() && !w.appId.starts_with("material3ui") && seen.insert(w.appId).second) open.push_back({w.appId, w.appId});
        std::ranges::sort(open);
        auto byApp = row(add, "apps", "For an open app", open.empty() ? "No apps have a window open" : "");
        if (!open.empty())
            dropdown(byApp.row, add, "Choose", open, [live](const std::string& id) {
                append("/niri/rules", {{"appId", niri::exactly(id)}, {"label", id}});
                live->syncSoon();
            });
        auto byId = row(add, "regular_expression", "For app ids matching", problem->empty() ? "A regex, as niri matches it; Enter to add" : *problem);
        if (!problem->empty()) byId.subtitle->setColor("error", false);
        auto* f = byId.row->add<TextField>();
        f->setPlaceholder("^org\\.gnome\\..*");
        f->fixedWidth = 240;
        f->onSubmit = [live, problem](const std::string& text) {
            if (!niri::validRegex(text)) {
                *problem = "niri couldn't read that regex";
            } else {
                problem->clear();
                append("/niri/rules", {{"appId", text}, {"label", text}});
            }
            live->syncSoon();
        };

        // config.kdl's own, for reference
        std::vector<std::pair<std::string, std::string>> theirs;
        for (const auto& n : NiriConfig::get().yours(false)) {
            if (n.name != "window-rule") continue;
            std::string match, what;
            for (const auto& c : n.children) {
                if (c.name == "match" || c.name == "exclude") {
                    for (const auto& [k, v] : c.props)
                        match += std::string(match.empty() ? "" : ", ") + (c.name == "exclude" ? "not " : "") + k + " " + v.text;
                } else {
                    what += (what.empty() ? "" : ", ") + c.name;
                }
            }
            theirs.push_back({match.empty() ? "Every window" : match, what});
        }
        if (!theirs.empty()) {
            auto* g = group(live, "In your config.kdl", "Its rules come first: one here wins where both set something");
            for (const auto& [match, what] : theirs) {
                auto r = row(g, "", match, what);
                r.title->setSize(13)->setMaxWidth(600);
            }
        }
    };
    live->sync();
}

void niriKeysPage(Flex* page, Popup&) {
    niriNotice(page);
    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    auto draft = std::make_shared<std::string>();
    auto problem = std::make_shared<std::string>();
    live->sync = [live, draft, problem] {
        live->clearChildren();
        auto* g = group(live, "Your keys", "They win over config.kdl's and the shell's own. Settings → Keyboard shows them too");
        const auto list = Config::get().value<nlohmann::json>("/niri/binds", nlohmann::json::array());
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& b = list[i];
            if (!b.is_object()) continue;
            const std::string action = b.value("action", "");
            auto r = row(g, action.empty() ? "terminal" : "keyboard_command_key", b.value("keys", ""),
                         action.empty() ? b.value("command", "") : niriActionLabel(action));
            confirmButton(r.row, "niribind:" + std::to_string(i), "delete", [i] { removeAt("/niri/binds", i); })->tooltip = "Remove";
        }
        // A new key: its combination, then what it does
        auto keys = row(g, "keyboard", "New key", problem->empty() ? "Mod is Super; then pick what it does" : *problem);
        if (!problem->empty()) keys.subtitle->setColor("error", false);
        auto* combo = keys.row->add<TextField>();
        combo->setPlaceholder("Mod+Shift+K");
        combo->setText(*draft);
        combo->fixedWidth = 170;
        combo->onChanged = [draft](const std::string& t) { *draft = t; };
        auto add = [live, draft, problem](nlohmann::json b) {
            if (!niri::validKeys(*draft)) {
                *problem = "First the keys, like Mod+Shift+K";
                live->syncSoon();
                return;
            }
            // One meaning per combination: a new one replaces the old
            auto l = Config::get().value<nlohmann::json>("/niri/binds", nlohmann::json::array());
            if (!l.is_array()) l = nlohmann::json::array();
            const std::string keys = *draft;
            l.erase(std::remove_if(l.begin(), l.end(), [&](const nlohmann::json& x) { return x.is_object() && x.value("keys", "") == keys; }), l.end());
            b["keys"] = keys;
            l.push_back(b);
            problem->clear();
            draft->clear();
            Config::get().set("/niri/binds", l);
        };
        auto act = row(g, "bolt", "Does a niri action");
        dropdown(act.row, g, "Choose", niriActions(), [add](const std::string& a) { add({{"action", a}}); });
        auto cmd = row(g, "terminal", "Or runs a command", "Enter to add");
        auto* f = cmd.row->add<TextField>();
        f->setPlaceholder("firefox, or material3ui msg …");
        f->fixedWidth = 260;
        f->onSubmit = [add](const std::string& text) {
            if (!text.empty()) add({{"command", text}});
        };

        // config.kdl's, for reference
        std::vector<std::pair<std::string, std::string>> theirs;
        for (const auto& n : NiriConfig::get().yours(false)) {
            if (n.name != "binds") continue;
            for (const auto& k : n.children) {
                if (k.children.empty()) continue;
                const auto& a = k.children[0];
                std::string what = niriActionLabel(a.name);
                if (a.name == "spawn" || a.name == "spawn-sh") what = "Runs " + argsText(a);
                else if (!a.args.empty()) what += " " + argsText(a);
                theirs.push_back({k.name, what});
            }
        }
        if (!theirs.empty()) {
            auto* yg = group(live, "In your config.kdl", std::format("{} keys. The shell's (material3ui.kdl) and yours above win over these", theirs.size()));
            for (const auto& [k, what] : theirs) {
                auto r = row(yg, "", k, what);
                r.title->setSize(13);
            }
        }
    };
    live->sync();
}

void niriStartupPage(Flex* page, Popup&) {
    niriNotice(page);
    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        const auto list = Config::get().value<nlohmann::json>("/niri/startup", nlohmann::json::array());
        auto* mine = group(live, "Your startup apps", "Run when niri starts, at your next login. ▶ runs one now");
        if (list.empty()) row(mine, "info", "None yet", "Add an app or a command below");
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& st = list[i];
            if (!st.is_object()) continue;
            const std::string command = st.value("command", ""), label = st.value("label", "");
            auto r = row(mine, "rocket_launch", label.empty() ? command : label, label.empty() ? "" : command);
            iconButton(r.row, "play_arrow", [command] { spawn(command); });
            confirmButton(r.row, "nirist:" + std::to_string(i) + ":" + command, "delete", [live, i] {
                removeAt("/niri/startup", i);
                live->syncSoon();
            });
            auto* sw = r.row->add<Switch>();
            sw->setChecked(st.value("enabled", true), false);
            sw->onToggled = [i](bool on) { Config::get().set("/niri/startup/" + std::to_string(i) + "/enabled", on); };
        }
        auto addApp = row(mine, "add", "Add an app");
        std::vector<std::pair<std::string, std::string>> apps;
        for (const auto& a : Apps::get().all())
            if (!a.exec.empty()) apps.push_back({a.name + "\n" + runnable(a.exec), a.name});
        std::ranges::sort(apps, [](const auto& a, const auto& b) { return a.second < b.second; });
        dropdown(addApp.row, mine, "Choose", apps, [live](const std::string& v) {
            const auto nl = v.find('\n');
            append("/niri/startup", {{"command", v.substr(nl + 1)}, {"label", v.substr(0, nl)}, {"enabled", true}});
            live->syncSoon();
        });
        auto addCmd = row(mine, "terminal", "Add a command", "Enter to add");
        auto* f = addCmd.row->add<TextField>();
        f->setPlaceholder("e.g. nm-applet --indicator");
        f->fixedWidth = 280;
        f->onSubmit = [live](const std::string& text) {
            if (text.empty()) return;
            append("/niri/startup", {{"command", text}, {"enabled", true}});
            live->syncSoon();
        };

        // What your config (and the shell's material3ui.kdl) starts
        std::vector<std::string> theirs;
        for (const auto& n : NiriConfig::get().yours(true))
            if (n.name == "spawn-at-startup" || n.name == "spawn-sh-at-startup") theirs.push_back(argsText(n));
        if (!theirs.empty()) {
            auto* g = group(live, "Started by your config", "config.kdl, and material3ui.kdl for the shell itself");
            for (const auto& t : theirs) {
                auto r = row(g, "", t);
                r.title->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(600);
            }
        }
    };
    live->sync();
}

void niriEnvironmentPage(Flex* page, Popup&) {
    niriNotice(page);
    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        const auto vars = Config::get().value<nlohmann::json>("/niri/environment", nlohmann::json::array());
        auto* mine = group(live, "Your variables", "For the apps niri starts from now on; log in again for the ones already running");
        if (vars.empty()) row(mine, "info", "None yet");
        for (size_t i = 0; i < vars.size(); ++i) {
            const auto& v = vars[i];
            if (!v.is_object()) continue;
            const std::string name = v.value("name", "");
            auto r = row(mine, "data_object", name);
            auto* f = r.row->add<TextField>();
            f->setText(v.value("value", ""));
            f->fixedWidth = 260;
            f->onSubmit = [i](const std::string& text) { Config::get().set("/niri/environment/" + std::to_string(i) + "/value", text); };
            confirmButton(r.row, "nirienv:" + name, "delete", [live, i] {
                removeAt("/niri/environment", i);
                live->syncSoon();
            });
        }
        auto add = row(mine, "add", "Add a variable");
        auto* name = add.row->add<TextField>();
        name->setPlaceholder("NAME");
        name->fixedWidth = 150;
        auto* value = add.row->add<TextField>();
        value->setPlaceholder("value, then Enter");
        value->fixedWidth = 190;
        auto submit = [live, name, value](const std::string&) {
            const std::string k = name->text();
            if (!niri::validVariable(k)) return;
            auto l = Config::get().value<nlohmann::json>("/niri/environment", nlohmann::json::array());
            if (!l.is_array()) l = nlohmann::json::array();
            l.erase(std::remove_if(l.begin(), l.end(), [&](const nlohmann::json& x) { return x.is_object() && x.value("name", "") == k; }), l.end());
            l.push_back({{"name", k}, {"value", value->text()}});
            Config::get().set("/niri/environment", l);
            live->syncSoon();
        };
        name->onSubmit = submit;
        value->onSubmit = submit;

        std::vector<std::pair<std::string, std::string>> theirs;
        for (const auto& n : NiriConfig::get().yours(true))
            if (n.name == "environment")
                for (const auto& c : n.children) theirs.push_back({c.name, c.args.empty() ? "" : argsText(c)});
        if (!theirs.empty()) {
            auto* g = group(live, "Set by your config", "config.kdl, and material3ui.kdl for your terminal and browser; yours above win");
            for (const auto& [k, v] : theirs) {
                auto r = row(g, "", k);
                r.title->setSize(13)->setColor("on_surface_variant", false);
                r.row->add<Label>(v)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(320);
            }
        }
    };
    live->sync();
}

} // namespace m3::shell::panels
