#include "shell/UmbrielPages.h"

#include "core/Process.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "services/UmbrielConfig.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;
using nlohmann::json;

namespace {

tomledit::Path split(const std::string& dotted) {
    tomledit::Path out;
    std::stringstream ss(dotted);
    for (std::string part; std::getline(ss, part, '.');) out.push_back(part);
    return out;
}

// One option of Umbriel's: where it is in config.toml, its row, and what
// Umbriel does without it
struct Opt {
    enum class Kind { Bool, Int, Float, Choice, Text };
    const char* path;
    const char* icon;
    const char* title;
    const char* sub;
    Kind kind;
    json def = nullptr; // Umbriel's default; null: the device's own (libinput's)
    double min = 0, max = 0, step = 1;
    const char* unit = ""; // "px", "ms", "%" (a fraction shown as a percentage)
    std::vector<std::pair<std::string, std::string>> choices = {}; // value, label
};

using K = Opt::Kind;

std::string shown(const Opt& o, double v) {
    const std::string unit = o.unit;
    if (unit == "%") return std::format("{:.0f}%", v * 100);
    if (o.kind == K::Int) return std::format("{:.0f}{}{}", v, unit.empty() ? "" : " ", unit);
    const int places = o.step >= 1 ? 0 : o.step >= 0.1 ? 1 : o.step >= 0.01 ? 2 : 3;
    return std::format("{:.{}f}{}{}", v, places, unit.empty() ? "" : " ", unit);
}

// A row for an option: its control showing what's in effect, written into
// config.toml as it changes; where config.toml sets it, a button back to
// Umbriel's default (or what an include says)
void option(Flex* g, const Opt& o) {
    auto& cfg = UmbrielConfig::get();
    const auto path = split(o.path);
    const json now = cfg.value(path);
    const json v = now.is_null() ? o.def : now;
    const std::string from = cfg.from(path);
    std::string sub = o.sub;
    if (!from.empty() && from != "config.toml") sub += (sub.empty() ? "" : " · ") + std::string("from ") + from;
    auto r = row(g, o.icon, o.title, sub);
    if (from == "config.toml") {
        auto* back = iconButton(r.row, "restart_alt", [path] { UmbrielConfig::get().unset(path); }, 18);
        back->tooltip = "Back to Umbriel's default";
    }
    switch (o.kind) {
    case K::Bool: {
        auto* sw = r.row->add<Switch>();
        sw->setChecked(v.is_boolean() && v.get<bool>(), false);
        sw->onToggled = [path](bool on) { UmbrielConfig::get().set(path, on); };
        break;
    }
    case K::Int:
    case K::Float: {
        const double value = v.is_number() ? v.get<double>() : o.min;
        r.subtitle->setText((sub.empty() ? "" : sub + " · ") + shown(o, value));
        r.subtitle->setVisible(true);
        auto* sl = r.row->add<Slider>();
        sl->fixedWidth = 240;
        sl->setRange(static_cast<float>(o.min), static_cast<float>(o.max))->setStep(static_cast<float>(o.step / (o.max - o.min)));
        sl->setValue(static_cast<float>(value), false);
        auto* label = r.subtitle;
        const Opt copy = o;
        sl->onChanged = [label, copy, sub](float x) {
            label->setText((sub.empty() ? "" : sub + " · ") + shown(copy, std::round(x / copy.step) * copy.step));
        };
        sl->onCommitted = [path, copy](float x) {
            const double snapped = std::round(x / copy.step) * copy.step;
            if (copy.kind == K::Int) UmbrielConfig::get().set(path, static_cast<long long>(std::lround(snapped)));
            else UmbrielConfig::get().set(path, std::round(snapped * 1000) / 1000);
        };
        break;
    }
    case K::Choice: {
        const std::string current = v.is_string() ? v.get<std::string>() : "";
        if (o.choices.size() <= 4) {
            connectedGroup<std::string>(r.row, current, o.choices, [path](std::string pick) { UmbrielConfig::get().set(path, pick); });
        } else {
            std::string label = current.empty() ? "Device default" : current;
            for (const auto& [value, name] : o.choices)
                if (value == current) label = name;
            dropdown(r.row, g, label, o.choices, [path](const std::string& pick) { UmbrielConfig::get().set(path, pick); });
        }
        break;
    }
    case K::Text: {
        auto* f = r.row->add<TextField>();
        f->fixedWidth = 240;
        f->setText(v.is_string() ? v.get<std::string>() : "");
        if (o.def.is_string() && !o.def.get<std::string>().empty()) f->setPlaceholder(o.def.get<std::string>());
        // Enter applies it; empty takes it out (Umbriel's default again)
        f->onSubmit = [path](const std::string& t) {
            if (t.empty()) UmbrielConfig::get().unset(path);
            else UmbrielConfig::get().set(path, t);
        };
        break;
    }
    }
}

// The page's state above its options: not on Umbriel, no config.toml yet,
// a file Umbriel can't read, or what its check found
void notice(Flex* page) {
    auto& cfg = UmbrielConfig::get();
    auto card = [&](const char* icon, const std::string& title, const std::string& text, bool bad) {
        auto* c = page->add<Flex>(Flex::Direction::Row);
        c->setPadding({18, 14})->setSpacing(14)->setRadius(Radii(20))->setAlignItems(Align::Start);
        c->setBackground(bad ? "error_container" : "secondary_container", false);
        c->add<Icon>(icon, 22)->setColor(bad ? "on_error_container" : "on_secondary_container", false);
        auto* texts = c->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(2);
        texts->grow = true;
        texts->add<Label>(title)->setSize(14)->setWeight(600)->setColor(bad ? "on_error_container" : "on_secondary_container", false);
        texts->add<Label>(text)->setSize(13)->setMaxWidth(560)->setMaxLines(ui::kAllLines)->setColor(bad ? "on_error_container" : "on_secondary_container", false);
        pillButton(c, "Open config.toml", false, [] { spawn("xdg-open \"${XDG_CONFIG_HOME:-$HOME/.config}/umbriel/config.toml\""); }, "open_in_new");
    };
    if (!cfg.readError().empty()) {
        card("error", "config.toml can't be read", cfg.readError() + ". Nothing here changes it until it's fixed.", true);
        return;
    }
    if (!cfg.problems().empty()) {
        std::string text;
        for (const auto& p : cfg.problems()) text += (text.empty() ? "" : "\n") + p;
        card("warning", "Umbriel found problems in your config", text, true);
    }
    if (!cfg.exists())
        card("info", "No config.toml of yours yet", "Umbriel runs on its own. The first change here starts ~/.config/umbriel/config.toml from it.", false);
    if (!cfg.lastError().empty()) card("error", "That change wasn't saved", cfg.lastError(), true);
}

// A page: its state, then its groups of options, built again as config.toml changes
void page(Flex* page, std::vector<std::tuple<std::string, std::string, std::vector<Opt>>> groups) {
    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->sync = [live, groups] {
        live->clearChildren();
        notice(live);
        if (!UmbrielConfig::get().readError().empty()) return;
        for (const auto& [title, sub, opts] : groups) {
            auto* g = group(live, title, sub);
            for (const auto& o : opts) option(g, o);
        }
    };
    live->watch(UmbrielConfig::get().changed);
    live->sync();
}


} // namespace

void umbrielLayoutPage(Flex* p, Popup&) {
    page(p, {
        {"Layout", "How windows share a workspace",
         {
             {"layout.mode", "view_quilt", "Layout", "Each workspace's, unless a workspace rule says otherwise", K::Choice, "scrolling", 0, 0, 1, "",
              {{"scrolling", "Scrolling"}, {"dwindle", "Dwindle"}, {"master", "Master"}, {"stacking", "Floating"}}},
             {"layout.gap", "space_bar", "Gaps", "Between windows, and around them", K::Int, 8, 0, 64, 1, "px"},
             {"workspaces.back_and_forth", "swap_horiz", "Back and forth", "Switching to the workspace you're on goes back to the one before",
              K::Bool, false},
             {"workspaces.empty_above", "vertical_align_top", "An empty workspace first", "As well as the one kept at the end", K::Bool, false},
         }},
        {"Scrolling", "Windows in a strip of columns you scroll through",
         {
             {"layout.scrolling.center_focused", "center_focus_strong", "Centre the focused column", "", K::Choice, "never", 0, 0, 1, "",
              {{"never", "Never"}, {"on_overflow", "If it won't fit"}, {"always", "Always"}}},
             {"layout.scrolling.center_underfull_strip", "align_horizontal_center", "Centre a short strip",
              "Columns that don't fill the screen sit in the middle", K::Bool, true},
         }},
        {"Master", "One big window, the rest stacked beside it",
         {
             {"layout.master.position", "vertical_split", "The big window", "", K::Choice, "left", 0, 0, 1, "",
              {{"left", "Left"}, {"center", "Centre"}, {"right", "Right"}}},
             {"layout.master.default_width_fraction", "width", "Its width", "Of the screen", K::Float, 0.55, 0.1, 0.9, 0.05, "%"},
             {"layout.master.new_becomes_master", "open_in_new", "New windows take its place", "", K::Bool, false},
         }},
        {"Dwindle and floating", "",
         {
             {"layout.dwindle.preserve_split", "splitscreen", "Keep each split's direction", "Dwindle: as you set it, not by the window's shape",
              K::Bool, false},
             {"layout.stacking.drop_zones", "dashboard", "Snap to halves and quarters", "Floating: drag a window to an edge or corner", K::Bool, true},
         }},
    });
}

void umbrielDecorationPage(Flex* p, Popup&) {
    page(p, {
        {"Windows", "Their borders, corners and title bars",
         {
             {"appearance.border_width", "border_outer", "Border", "In the focused and unfocused colours", K::Int, 2, 0, 16, 1, "px"},
             {"appearance.outer_border_width", "border_style", "Outer border", "A second line outside it", K::Int, 0, 0, 16, 1, "px"},
             {"appearance.corner_radius", "rounded_corner", "Corners", "", K::Int, 10, 0, 40, 1, "px"},
             {"appearance.title_bar.mode", "web_asset", "Title bars", "Umbriel's own, with its buttons", K::Choice, "off", 0, 0, 1, "",
              {{"off", "Off"}, {"negotiated", "If asked"}, {"on", "On"}, {"forced", "Always"}}},
             {"appearance.prefer_no_csd", "tab_close", "Ask apps not to draw their own", "Their title bars and shadows", K::Bool, true},
             {"appearance.drag_opacity", "opacity", "While dragged", "How solid a window stays as you move it", K::Float, 0.75, 0.2, 1, 0.05, "%"},
         }},
        {"Blur", "Behind see-through windows and the shell's bar, panels and dock",
         {
             {"appearance.blur.enabled", "blur_on", "Blur", "", K::Bool, true},
             {"appearance.blur.passes", "layers", "Passes", "More is smoother, and costs more", K::Int, 2, 0, 8, 1},
             {"appearance.blur.radius", "blur_medium", "Radius", "", K::Int, 5, 0, 30, 1, "px"},
             {"appearance.blur.noise", "grain", "Noise", "A little grain, against banding", K::Float, 0.02, 0, 0.2, 0.01},
             {"appearance.blur.saturation", "palette", "Saturation", "", K::Float, 1.1, 0, 2, 0.05, "%"},
             {"appearance.blur.brightness", "brightness_6", "Brightness", "", K::Float, 0.9, 0, 2, 0.05, "%"},
             {"appearance.blur.contrast", "contrast", "Contrast", "", K::Float, 0.9, 0, 2, 0.05, "%"},
             {"appearance.blur.optimized", "speed", "Cheaper blur", "The wallpaper's blur, cached; off blurs what's really behind", K::Bool, true},
         }},
        {"Shadows", "Behind each window",
         {
             {"appearance.shadow.enabled", "ev_shadow", "Shadows", "", K::Bool, true},
             {"appearance.shadow.softness", "blur_on", "Softness", "", K::Int, 10, 0, 100, 1, "px"},
             {"appearance.shadow.offset_x", "east", "Across", "", K::Int, 2, -40, 40, 1, "px"},
             {"appearance.shadow.offset_y", "south", "Down", "", K::Int, 2, -40, 40, 1, "px"},
         }},
    });
}

void umbrielAnimationsPage(Flex* p, Popup&) {
    page(p, {
        {"Animations", "Umbriel's, for windows and workspaces (the shell's own are in Settings → Look & feel)",
         {
             {"animation.enabled", "animation", "Animations", "All of them", K::Bool, true},
             {"animation.duration_ms", "timer", "How long", "Each one's, unless it has its own", K::Int, 250, 50, 1000, 10, "ms"},
         }},
        {"Windows", "",
         {
             {"animation.windows_in.enabled", "open_in_new", "Opening", "", K::Bool, true},
             {"animation.windows_in.style", "auto_awesome", "How they open", "", K::Choice, "popin", 0, 0, 1, "",
              {{"popin", "Pop in"}, {"zoom", "Zoom"}, {"slide", "Slide"}, {"fade", "Fade"}, {"none", "None"}}},
             {"animation.windows_out.enabled", "close", "Closing", "", K::Bool, true},
             {"animation.windows_out.style", "auto_awesome", "How they close", "", K::Choice, "popin", 0, 0, 1, "",
              {{"popin", "Pop out"}, {"zoom", "Zoom"}, {"slide", "Slide"}, {"fade", "Fade"}}},
             {"animation.windows_move.enabled", "open_with", "Moving", "", K::Bool, true},
             {"animation.windows_resize.enabled", "aspect_ratio", "Resizing", "", K::Bool, true},
             {"animation.dialogs.enabled", "chat_bubble", "Dialogs", "Opening over their window, which dims", K::Bool, true},
             {"animation.border.enabled", "border_outer", "Borders", "Their colour, as the focus moves", K::Bool, true},
         }},
        {"Workspaces and the overview", "",
         {
             {"animation.workspaces.enabled", "view_carousel", "Switching workspaces", "", K::Bool, true},
             {"animation.workspaces.style", "swipe", "How they switch", "", K::Choice, "slide", 0, 0, 1, "",
              {{"slide", "Slide"}, {"reveal", "Reveal"}}},
             {"animation.overview.enabled", "grid_view", "The overview", "", K::Bool, true},
             {"animation.layers.enabled", "layers", "Panels", "The shell's bar and panels animate themselves: best left off", K::Bool, false},
         }},
    });
}

void umbrielInputPage(Flex* p, Popup&) {
    page(p, {
        {"Keyboard", "Layouts and typing (Settings → Keyboard has the shell's keys)",
         {
             {"input.keyboard.layout", "keyboard_alt", "Layouts", "One or more, a comma between: us,de · Enter to apply", K::Text, ""},
             {"input.keyboard.variant", "keyboard", "Variants", "Each layout's, in order: ,nodeadkeys", K::Text, ""},
             {"input.keyboard.options", "tune", "Options", "XKB options: caps:escape, compose:ralt", K::Text, ""},
             {"input.keyboard.repeat_delay", "hourglass_top", "Repeat after", "Holding a key", K::Int, 600, 100, 1500, 10, "ms"},
             {"input.keyboard.repeat_rate", "speed", "Repeat speed", "Characters a second", K::Int, 25, 1, 100, 1},
             {"input.keyboard.track_layout", "switch_access", "Layout", "One for everything, or each window its own", K::Choice, "global", 0, 0, 1,
              "", {{"global", "Everywhere"}, {"window", "Per window"}}},
         }},
        {"Touchpad", "",
         {
             {"input.touchpad.tap", "touch_app", "Tap to click", "", K::Bool, true},
             {"input.touchpad.natural_scroll", "swipe_vertical", "Natural scrolling", "Content moves with your fingers", K::Bool, false},
             {"input.touchpad.disable_while_typing", "keyboard_hide", "Off while typing", "", K::Bool, true},
             {"input.touchpad.tap_and_drag", "drag_pan", "Tap and drag", "Tap, then move to drag", K::Bool, true},
             {"input.touchpad.sensitivity", "speed", "Speed", "", K::Float, 0.0, -1, 1, 0.05},
             {"input.touchpad.click_method", "ads_click", "Right click", "", K::Choice, nullptr, 0, 0, 1, "",
              {{"clickfinger", "Two fingers"}, {"button_areas", "Bottom right corner"}}},
             {"input.touchpad.scroll_method", "unfold_more", "Scrolling", "", K::Choice, nullptr, 0, 0, 1, "",
              {{"two_finger", "Two fingers"}, {"edge", "Right edge"}, {"none", "Off"}}},
         }},
        {"Mouse", "",
         {
             {"input.mouse.natural_scroll", "swipe_vertical", "Natural scrolling", "", K::Bool, false},
             {"input.mouse.sensitivity", "speed", "Speed", "", K::Float, 0.0, -1, 1, 0.05},
             {"input.mouse.accel_profile", "trending_up", "Acceleration", "", K::Choice, nullptr, 0, 0, 1, "",
              {{"adaptive", "On"}, {"flat", "Off"}}},
             {"input.mouse.left_handed", "back_hand", "Left-handed", "The buttons swapped", K::Bool, false},
         }},
        {"Pointer", "",
         {
             {"input.cursor.size", "arrow_selector_tool", "Size", "", K::Int, 24, 16, 96, 4, "px"},
             {"input.cursor.theme", "style", "Theme", "A cursor theme's name · Enter to apply", K::Text, ""},
             {"input.cursor.hide_when_typing", "keyboard", "Hide while typing", "", K::Bool, false},
         }},
    });
}

void umbrielBehaviourPage(Flex* p, Popup&) {
    page(p, {
        {"Windows", "",
         {
             {"general.focus_on_activate", "front_hand", "Apps may bring themselves forward", "When one asks (a link opening a browser)",
              K::Bool, false},
             {"general.attach_modal_dialogs", "chat_bubble", "Dialogs stay with their window", "Moving with it, and holding its input", K::Bool, true},
             {"layout.client_maximize", "fullscreen", "Maximise", "When an app maximises itself", K::Choice, "column", 0, 0, 1, "",
              {{"column", "Its column"}, {"edges", "Edge to edge"}}},
         }},
        {"The overview", "",
         {
             {"overview.zoom", "zoom_out", "Size", "How large workspaces show in it", K::Float, 0.5, 0.1, 0.75, 0.05, "%"},
             {"overview.background_blur", "blur_on", "Blurred background", "", K::Bool, true},
             {"overview.workspace_wallpaper", "wallpaper", "Wallpaper in each workspace", "", K::Bool, true},
             {"overview.shortcuts", "pin", "Number keys", "Jump to a workspace with 1 to 9", K::Bool, true},
         }},
        {"Session", "",
         {
             {"general.show_cheatsheet", "keyboard_command_key", "Keys cheatsheet at login", "Umbriel's list of its keys", K::Bool, true},
             {"screencast.disable_dynamic_confirmation", "cast", "Change what's shared without asking",
              "Switching a screen share to another window or screen", K::Bool, false},
             {"general.xwayland", "apps", "X11 apps", "Through Xwayland, started when one opens · at your next login", K::Bool, true},
         }},
    });
}

void umbrielStartupPage(Flex* p, Popup&) {
    auto* live = p->add<Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        notice(live);
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        const tomledit::Path path = {"general", "autostart"};
        json list = cfg.value(path);
        if (!list.is_array()) list = json::array();
        const std::string from = cfg.from(path);
        auto* g = group(live, "Startup apps",
                        from.empty() || from == "config.toml" ? "Run when Umbriel starts, at your next login. ▶ runs one now"
                                                              : "Set in " + from + "; a change here saves the list in config.toml");
        if (list.empty()) row(g, "info", "None yet", "Add an app or a command below. The shell itself starts as material3ui");
        for (size_t i = 0; i < list.size(); ++i) {
            if (!list[i].is_string()) continue;
            const std::string command = list[i].get<std::string>();
            auto r = row(g, command == "material3ui" ? "widgets" : "rocket_launch", command, command == "material3ui" ? "This shell" : "");
            iconButton(r.row, "play_arrow", [command] { spawn(command); });
            confirmButton(r.row, "umbst:" + std::to_string(i) + ":" + command, "delete", [list, i] {
                json next = list;
                next.erase(i);
                UmbrielConfig::get().set({"general", "autostart"}, next);
            });
        }
        auto add = [list](const std::string& command) {
            if (command.empty()) return;
            json next = list;
            next.push_back(command);
            UmbrielConfig::get().set({"general", "autostart"}, next);
        };
        auto addApp = row(g, "add", "Add an app");
        std::vector<std::pair<std::string, std::string>> apps;
        for (const auto& a : Apps::get().all()) {
            if (a.exec.empty()) continue;
            // Its command without the field codes (%U, %f…)
            std::string exec;
            std::istringstream words(a.exec);
            for (std::string w; words >> w;)
                if (!(w.size() == 2 && w[0] == '%')) exec += (exec.empty() ? "" : " ") + w;
            apps.push_back({exec, a.name});
        }
        std::ranges::sort(apps, [](const auto& a, const auto& b) { return a.second < b.second; });
        dropdown(addApp.row, g, "Choose", apps, add);
        auto addCmd = row(g, "terminal", "Add a command", "Enter to add");
        auto* f = addCmd.row->add<TextField>();
        f->setPlaceholder("e.g. nm-applet --indicator");
        f->fixedWidth = 280;
        f->onSubmit = add;
        // The shell starts with the session only from this list
        if (std::ranges::find(list, json("material3ui")) == list.end()) {
            auto r = row(g, "widgets", "Start the shell with Umbriel", "Material3UI isn't in the list");
            pillButton(r.row, "Add it", true, [add] { add("material3ui"); }, "add");
        }
    };
    live->watch(UmbrielConfig::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
