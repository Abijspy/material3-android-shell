#include "shell/UmbrielPages.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "services/Umbriel.h"
#include "services/UmbrielConfig.h"
#include "services/UmbrielShaders.h"
#include "render/Images.h"
#include "core/EventLoop.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <optional>
#include <set>
#include <regex>
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
    // Number: typed in (a range too wide for a slider); List: comma-separated
    enum class Kind { Bool, Int, Float, Choice, Text, Number, List };
    std::string path;
    std::string icon;
    std::string title;
    std::string sub;
    Kind kind;
    json def = nullptr; // Umbriel's default; null: the device's own (libinput's)
    double min = 0, max = 0, step = 1;
    std::string unit = ""; // "px", "ms", "%" (a fraction shown as a percentage)
    std::vector<std::pair<std::string, std::string>> choices = {}; // value, label
    bool integer = false;   // Number: whole numbers
    std::string listOf = ""; // List: "string", "int" or "float"
    std::string defaultText = ""; // sliders: shown after the value, "8 (default 8)"
};

using K = Opt::Kind;

std::string shown(const Opt& o, double v) {
    const std::string unit = o.unit;
    if (unit == "%") return std::format("{:.0f}%", v * 100);
    if (o.kind == K::Int) return std::format("{:.0f}{}{}", v, unit.empty() ? "" : " ", unit);
    const int places = o.step >= 1 ? 0 : o.step >= 0.1 ? 1 : o.step >= 0.01 ? 2 : 3;
    return std::format("{:.{}f}{}{}", v, places, unit.empty() ? "" : " ", unit);
}

// The option as the installed Umbriel describes it (it changes as Umbriel
// is developed): its default, range and choices from `umbriel config
// schema` where it says them, else as written here. Nothing when this
// Umbriel doesn't have it: never shown, never written
std::optional<Opt> adapted(Opt o) {
    auto& cfg = UmbrielConfig::get();
    if (!cfg.hasSchema()) return o;
    const auto* s = cfg.schema(o.path);
    if (!s) return std::nullopt;
    if (s->contains("default")) o.def = (*s)["default"];
    if (o.kind == K::Int || o.kind == K::Float) {
        if (s->contains("min") && (*s)["min"].is_number()) o.min = std::max(o.min, (*s)["min"].get<double>());
        if (s->contains("max") && (*s)["max"].is_number()) o.max = std::min(o.max, (*s)["max"].get<double>());
        if (o.max <= o.min) return std::nullopt;
    }
    if (o.kind == K::Choice && s->contains("values") && (*s)["values"].is_array()) {
        const auto& values = (*s)["values"];
        std::erase_if(o.choices, [&](const auto& c) { return std::ranges::find(values, json(c.first)) == values.end(); });
        if (o.choices.empty()) return std::nullopt;
    }
    return o;
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
        const std::string dflt = o.defaultText.empty() ? "" : " (default " + o.defaultText + ")";
        r.subtitle->setText((sub.empty() ? "" : sub + " · ") + shown(o, value) + dflt);
        r.subtitle->setVisible(true);
        auto* sl = r.row->add<Slider>();
        sl->fixedWidth = 240;
        sl->setRange(static_cast<float>(o.min), static_cast<float>(o.max))->setStep(static_cast<float>(o.step / (o.max - o.min)));
        sl->setValue(static_cast<float>(value), false);
        auto* label = r.subtitle;
        const Opt copy = o;
        sl->onChanged = [label, copy, sub, dflt](float x) {
            label->setText((sub.empty() ? "" : sub + " · ") + shown(copy, std::round(x / copy.step) * copy.step) + dflt);
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
    case K::Number: {
        auto* f = r.row->add<TextField>();
        f->fixedWidth = 160;
        auto number = [&](const json& x) {
            return !x.is_number() ? std::string() : o.integer ? std::to_string(x.get<long long>()) : std::format("{}", x.get<double>());
        };
        f->setText(now.is_null() ? "" : number(now));
        f->setPlaceholder(number(o.def));
        const Opt copy = o;
        f->onSubmit = [path, copy](const std::string& t) {
            if (t.empty()) return (void)UmbrielConfig::get().unset(path);
            try {
                double x = std::stod(t);
                if (copy.max > copy.min) x = std::clamp(x, copy.min, copy.max);
                if (copy.integer) UmbrielConfig::get().set(path, static_cast<long long>(std::llround(x)));
                else UmbrielConfig::get().set(path, x);
            } catch (...) {
            }
        };
        break;
    }
    case K::List: {
        // As written, a comma between; Enter applies, empty takes it out
        auto* f = r.row->add<TextField>();
        f->fixedWidth = 240;
        auto words = [](const json& x) {
            std::string out;
            if (x.is_array())
                for (const auto& e : x) out += (out.empty() ? "" : ", ") + (e.is_string() ? e.get<std::string>() : e.dump());
            return out;
        };
        f->setText(now.is_null() ? "" : words(now));
        f->setPlaceholder(words(o.def));
        const std::string of = o.listOf;
        f->onSubmit = [path, of](const std::string& t) {
            if (t.empty()) return (void)UmbrielConfig::get().unset(path);
            json list = json::array();
            std::stringstream ss(t);
            for (std::string w; std::getline(ss, w, ',');) {
                w.erase(0, w.find_first_not_of(' '));
                while (!w.empty() && w.back() == ' ') w.pop_back();
                if (w.empty()) continue;
                try {
                    if (of == "int") list.push_back(std::stoll(w));
                    else if (of == "float") list.push_back(std::stod(w));
                    else list.push_back(w);
                } catch (...) {
                    return; // not a number where one's wanted: nothing written
                }
            }
            UmbrielConfig::get().set(path, list);
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

std::optional<Opt> fromSchema(const std::string& p, const json& s); // below
std::string humane(std::string key);
std::string lastPart(const std::string& dotted);
std::string parentOf(const std::string& dotted);

// The options these pages were written for (shell/data/umbriel-known-options.txt):
// any other one Umbriel has is newer than them. Empty without the file, and
// then nothing counts as newer
const std::set<std::string>& knownOptions() {
    static const std::set<std::string> known = [] {
        std::set<std::string> out;
        std::ifstream in(paths::dataFile("umbriel-known-options.txt"));
        for (std::string line; std::getline(in, line);)
            if (!line.empty() && line[0] != '#') out.insert(line);
        return out;
    }();
    return known;
}

// A page: its state, then its groups of options, built again as config.toml
// changes. The options of its `sections` that are newer than these pages
// (a newer Umbriel's) join the group showing their table, or a group of
// their own named after it, marked new
void page(Flex* page, std::vector<std::tuple<std::string, std::string, std::vector<Opt>>> groups, std::vector<std::string> sections = {}) {
    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->sync = [live, groups, sections] {
        live->clearChildren();
        notice(live);
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        // Only what this Umbriel has; a group with none of it, not at all
        std::vector<std::tuple<std::string, std::string, std::vector<Opt>>> shown;
        for (const auto& [title, sub, opts] : groups) {
            std::vector<Opt> here;
            for (const auto& o : opts)
                if (auto a = adapted(o)) here.push_back(std::move(*a));
            shown.push_back({title, sub, std::move(here)});
        }
        const auto& known = knownOptions();
        if (!known.empty())
            for (const auto& [path, s] : cfg.schemaAll()) {
                const std::string section = path.substr(0, path.find('.'));
                if (known.contains(path) || std::ranges::find(sections, section) == sections.end()) continue;
                auto o = fromSchema(path, s);
                if (!o) continue;
                o->sub = "New in your Umbriel · " + o->sub;
                // Beside the options of its table, else a group of its own
                const std::string parent = parentOf(path);
                auto it = std::ranges::find_if(shown, [&](const auto& g) {
                    return std::ranges::any_of(std::get<2>(g), [&](const Opt& x) { return parentOf(x.path) == parent; });
                });
                if (it == shown.end()) {
                    const std::string title = parent.find('.') == std::string::npos ? humane(parent) : humane(lastPart(parent));
                    it = std::ranges::find_if(shown, [&](const auto& g) { return std::get<1>(g) == "New in your Umbriel: " + parent; });
                    if (it == shown.end()) {
                        shown.push_back({title, "New in your Umbriel: " + parent, {}});
                        it = shown.end() - 1;
                    }
                }
                std::get<2>(*it).push_back(std::move(*o));
            }
        for (const auto& [title, sub, opts] : shown) {
            if (opts.empty()) continue;
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
    }, {"layout", "workspaces"});
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
    }, {"appearance", "colors"});
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
    }, {"animation"});
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
    }, {"input"});
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
    }, {"general", "overview", "screencast", "hot_corners", "scratchpad"});
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

namespace {

// Umbriel's actions a key can do, in words
const std::vector<std::pair<std::string, std::string>>& umbrielActions() {
    static const std::vector<std::pair<std::string, std::string>> list = {
        {"window-close", "Close the window"},
        {"window-toggle-fullscreen", "Fullscreen"},
        {"window-toggle-maximize", "Maximise (the column's full width)"},
        {"window-toggle-maximize-to-edges", "Maximise to the edges"},
        {"window-toggle-floating", "Float or tile the window"},
        {"window-toggle-pinned", "Pin above other windows"},
        {"window-minimize", "Minimise (a floating window)"},
        {"window-restore-minimized", "Bring back the last minimised"},
        {"window-focus-left", "Focus the window to the left"},
        {"window-focus-right", "Focus the window to the right"},
        {"window-focus-up", "Focus the window above"},
        {"window-focus-down", "Focus the window below"},
        {"window-move-left", "Move the window left"},
        {"window-move-right", "Move the window right"},
        {"window-focus-last", "Focus the window before"},
        {"column-center", "Centre the column"},
        {"column-toggle-tabbed", "Tab the column"},
        {"window-cycle-primary-extent", "Cycle the width"},
        {"workspace-next", "Next workspace"},
        {"workspace-previous", "Previous workspace"},
        {"workspace-focus-last", "The workspace before"},
        {"window-move-to-workspace-next", "Window to the next workspace"},
        {"window-move-to-workspace-previous", "Window to the previous workspace"},
        {"workspace-toggle-show-desktop", "Show the desktop"},
        {"overview-toggle", "Overview"},
        {"scratchpad-toggle", "Show or hide the scratchpad"},
        {"window-toggle-scratchpad", "Window to or from the scratchpad"},
        {"keyboard-layout-next", "Next keyboard layout"},
        {"screencast-set-window", "Share this window instead"},
        {"cheatsheet-toggle", "Keys cheatsheet"},
        {"config-reload", "Reload the config"},
        {"session-quit", "Quit Umbriel"},
    };
    return list;
}

// A key's action ("window-close", "spawn foot", or a table with one) in words
std::string actionWords(const json& v) {
    const std::string a = v.is_string() ? v.get<std::string>() : v.is_object() ? v.value("action", "") : "";
    if (a.starts_with("spawn ")) return "Runs " + a.substr(6);
    const auto space = a.find(' ');
    const std::string word = a.substr(0, space);
    for (const auto& [id, label] : umbrielActions())
        if (id == word) return space == std::string::npos ? label : label + a.substr(space);
    return a;
}

bool validChord(const std::string& keys) {
    static const std::regex chord(R"(^((Mod|Shift|Ctrl|Control|Alt|Super|Logo|Win)\+)*[A-Za-z0-9_]+$)");
    return std::regex_match(keys, chord);
}

// What a rule matches, and what it does, in words
std::string ruleMatch(const json& r) {
    const auto m = r.value("match", json::object());
    std::string out;
    if (m.contains("app_id")) out += "Apps: " + (m["app_id"].is_string() ? m["app_id"].get<std::string>() : m["app_id"].dump());
    if (m.contains("title")) out += (out.empty() ? "" : " · ") + std::string("Titles: ") + (m["title"].is_string() ? m["title"].get<std::string>() : m["title"].dump());
    return out.empty() ? "Every window" : out;
}

std::string ruleEffects(const json& r) {
    std::vector<std::string> parts;
    for (const auto& [k, v] : r.items()) {
        if (k == "match") continue;
        if (k == "default_floating") parts.push_back(v == true ? "opens floating" : "opens tiled");
        else if (k == "default_fullscreen" && v == true) parts.push_back("opens fullscreen");
        else if (k == "default_maximize" && v == true) parts.push_back("opens maximised");
        else if (k == "default_pinned" && v == true) parts.push_back("opens pinned");
        else if (k == "default_workspace") parts.push_back("opens on workspace " + (v.is_string() ? v.get<std::string>() : v.dump()));
        else if (k == "opacity" && v.is_number()) parts.push_back(std::format("{:.0f}% opaque", v.get<double>() * 100));
        else if (k == "blur") parts.push_back(v == true ? "blurred behind" : "no blur");
        else parts.push_back(k + " = " + v.dump());
    }
    std::string out;
    for (const auto& p : parts) out += (out.empty() ? "" : ", ") + p;
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// A mode as config.toml writes it: 1920x1080@60, 2560x1440@143.912
std::string modeText(const json& m) {
    const double hz = m.value("refresh_mhz", 0) / 1000.0;
    std::string rate = std::format("{:.3f}", hz);
    while (rate.ends_with("0")) rate.pop_back();
    if (rate.ends_with(".")) rate.pop_back();
    return std::format("{}x{}@{}", m.value("width", 0), m.value("height", 0), rate);
}

} // namespace

void umbrielKeysPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto draft = std::make_shared<std::string>();
    auto problem = std::make_shared<std::string>();
    live->sync = [live, draft, problem] {
        live->clearChildren();
        notice(live);
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        // config.toml's, which win over the includes'
        json mine = cfg.own({"keybinds"});
        if (!mine.is_object()) mine = json::object();
        auto* g = group(live, "Your keys", "In config.toml: they win over the shell's and other includes'");
        if (mine.empty()) row(g, "info", "None yet", "Add one below");
        for (const auto& [chord, v] : mine.items()) {
            if (v.is_object() && !v.contains("action")) continue; // a submap's, not a key
            const bool locked = v.is_object() && v.value("allow_when_locked", false);
            auto r = row(g, actionWords(v).starts_with("Runs ") ? "terminal" : "keyboard_command_key", chord,
                         actionWords(v) + (locked ? " · on the lock screen too" : ""));
            const std::string k = chord;
            confirmButton(r.row, "umbkey:" + k, "delete", [k] { UmbrielConfig::get().unset({"keybinds", k}); })->tooltip = "Remove";
        }
        // A new key: its combination, then what it does
        auto keys = row(g, "keyboard", "New key", problem->empty() ? "Mod is Super; then pick what it does" : *problem);
        if (!problem->empty()) keys.subtitle->setColor("error", false);
        auto* combo = keys.row->add<TextField>();
        combo->setPlaceholder("Mod+Shift+K");
        combo->setText(*draft);
        combo->fixedWidth = 170;
        combo->onChanged = [draft](const std::string& t) { *draft = t; };
        auto add = [live, draft, problem](json action) {
            if (!validChord(*draft)) {
                *problem = "First the keys, like Mod+Shift+K";
                live->syncSoon();
                return;
            }
            const std::string chord = *draft;
            problem->clear();
            draft->clear();
            UmbrielConfig::get().set({"keybinds", chord}, action);
        };
        auto act = row(g, "bolt", "Does an Umbriel action");
        dropdown(act.row, g, "Choose", umbrielActions(), [add](const std::string& a) { add(a); });
        auto cmd = row(g, "terminal", "Or runs a command", "Enter to add");
        auto* f = cmd.row->add<TextField>();
        f->setPlaceholder("firefox, or material3ui msg …");
        f->fixedWidth = 260;
        f->onSubmit = [add](const std::string& text) {
            if (!text.empty()) add(json{{"action", "spawn " + text}, {"repeat", false}});
        };

        // The includes' (the shell's material3ui.toml): where config.toml doesn't bind the same keys
        json all = cfg.value({"keybinds"});
        std::vector<std::pair<std::string, std::string>> theirs;
        if (all.is_object())
            for (const auto& [chord, v] : all.items())
                if (!mine.contains(chord) && (v.is_string() || (v.is_object() && v.contains("action")))) theirs.push_back({chord, actionWords(v)});
        if (!theirs.empty()) {
            auto* tg = group(live, "From material3ui.toml and other includes", std::format("{} keys. A key of yours with the same keys wins over one of these", theirs.size()));
            for (const auto& [chord, what] : theirs) {
                auto r = row(tg, "", chord, what);
                r.title->setSize(13);
            }
        }
    };
    live->watch(UmbrielConfig::get().changed);
    live->sync();
}

void umbrielRulesPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto app = std::make_shared<std::string>();
    auto problem = std::make_shared<std::string>();
    live->sync = [live, app, problem] {
        live->clearChildren();
        notice(live);
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        const auto rules = cfg.tables("window_rule");
        auto* g = group(live, "Your window rules", "In config.toml: how an app's windows open and look");
        bool any = false;
        for (const auto& r : rules) {
            if (r.index < 0) continue;
            any = true;
            auto x = row(g, "rule", ruleMatch(r.data), ruleEffects(r.data));
            const int index = r.index;
            confirmButton(x.row, std::format("umbrule:{}:{}", index, r.data.dump()), "delete",
                          [index] { UmbrielConfig::get().removeTable("window_rule", index); })->tooltip = "Remove";
        }
        if (!any) row(g, "info", "None yet", "Add one below: an app, then what its windows do");
        // A new rule: the app, then what it does
        auto who = row(g, "apps", "App", problem->empty() ? "Its app id (an open window's, below), or a pattern" : *problem);
        if (!problem->empty()) who.subtitle->setColor("error", false);
        auto* field = who.row->add<TextField>();
        field->setPlaceholder("firefox");
        field->setText(*app);
        field->fixedWidth = 200;
        field->onChanged = [app](const std::string& t) { *app = t; };
        // The apps open now
        std::vector<std::pair<std::string, std::string>> open;
        for (const auto& w : Compositor::get().windows(true))
            if (!w.appId.empty() && std::ranges::none_of(open, [&](const auto& o) { return o.first == w.appId; })) open.push_back({w.appId, w.appId});
        if (!open.empty())
            dropdown(who.row, g, "Open apps", open, [app, live](const std::string& id) {
                *app = id;
                live->syncSoon();
            });
        auto add = [live, app, problem](json effect) {
            std::string id = *app;
            id.erase(0, id.find_first_not_of(' '));
            while (!id.empty() && id.back() == ' ') id.pop_back();
            if (id.empty()) {
                *problem = "First the app, like firefox";
                live->syncSoon();
                return;
            }
            // A plain id matches itself exactly; a pattern as written
            const bool pattern = id.find_first_of("^$.*+?()[]|\\") != std::string::npos;
            json rule = effect;
            rule["match"] = {{"app_id", pattern ? id : "^" + id + "$"}};
            problem->clear();
            app->clear();
            UmbrielConfig::get().addTable("window_rule", rule);
        };
        auto what = row(g, "bolt", "Its windows");
        const std::vector<std::pair<std::string, std::string>> effects = {
            {"floating", "Open floating"},     {"tiled", "Open tiled"},           {"fullscreen", "Open fullscreen"},
            {"maximize", "Open maximised"},    {"pinned", "Open pinned on top"},  {"ws1", "Open on workspace 1"},
            {"ws2", "Open on workspace 2"},    {"ws3", "Open on workspace 3"},    {"ws4", "Open on workspace 4"},
            {"o90", "90% opaque"},             {"o80", "80% opaque"},             {"blur", "Blurred behind"},
        };
        dropdown(what.row, g, "Choose", effects, [add](const std::string& e) {
            if (e == "floating") add({{"default_floating", true}});
            else if (e == "tiled") add({{"default_floating", false}});
            else if (e == "fullscreen") add({{"default_fullscreen", true}});
            else if (e == "maximize") add({{"default_maximize", true}});
            else if (e == "pinned") add({{"default_pinned", true}});
            else if (e.starts_with("ws")) add({{"default_workspace", std::stoi(e.substr(2))}});
            else if (e == "o90") add({{"opacity", 0.9}});
            else if (e == "o80") add({{"opacity", 0.8}});
            else if (e == "blur") add({{"blur", true}});
        });

        // The includes' rules
        std::vector<const UmbrielConfig::Table*> theirs;
        for (const auto& r : rules)
            if (r.index < 0) theirs.push_back(&r);
        if (!theirs.empty()) {
            auto* tg = group(live, "From includes", "Umbriel applies every matching rule, these before yours");
            for (const auto* r : theirs) {
                auto x = row(tg, "", ruleMatch(r->data), ruleEffects(r->data) + " · " + r->file);
                x.title->setSize(13);
            }
        }
    };
    live->watch(UmbrielConfig::get().changed);
    live->sync();
}

void umbrielMonitorsPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(6);
    // A change on probation: put back unless kept within 15 s (a mode the
    // screen can't show, a scale too large to reach the button)
    struct Trial {
        tomledit::Path path;
        json before; // config.toml's own, null: not set there
        EventLoop::TimerId timer = 0;
        int left = 0;
    };
    auto trial = std::make_shared<std::optional<Trial>>();
    auto settle = [trial](bool keep) {
        if (!*trial) return;
        auto t = **trial;
        trial->reset();
        if (t.timer) EventLoop::get().cancel(t.timer);
        if (keep) return;
        if (t.before.is_null()) UmbrielConfig::get().unset(t.path);
        else UmbrielConfig::get().set(t.path, t.before);
    };
    live->sync = [live, trial, settle] {
        live->clearChildren();
        notice(live);
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        if (*trial) {
            auto* c = live->add<Flex>(Flex::Direction::Row);
            c->setPadding({18, 14})->setSpacing(14)->setRadius(Radii(20))->setBackground("primary_container", false);
            c->add<Icon>("timer", 22)->setColor("on_primary_container", false);
            auto* l = c->add<Label>(std::format("Keep this? Going back in {} s", (*trial)->left));
            l->setSize(14)->setWeight(600)->setColor("on_primary_container", false);
            l->grow = true;
            pillButton(c, "Go back", false, [settle] { settle(false); });
            pillButton(c, "Keep", true, [settle] { settle(true); }, "check");
        }
        // Umbriel's outputs, with their modes
        json outs = Compositor::session() == "umbriel" ? Umbriel::get().request({{"cmd", "outputs"}}).value_or(json::array()) : json::array();
        if (!outs.is_array() || outs.empty()) {
            row(group(live, "Monitors"), "monitor", "Umbriel isn't running", "Monitors show here in an Umbriel session");
            return;
        }
        // A change that's tried first: the old value kept to go back to
        auto tryIt = [live, trial, settle](const tomledit::Path& path, const json& value) {
            settle(true); // an earlier one, kept as it is
            Trial t{path, UmbrielConfig::get().own(path), 0, 15};
            t.timer = EventLoop::get().addTimer(1000, [live, trial, settle, alive = live->alive()] {
                if (!*alive || !*trial) return;
                if (--(*trial)->left <= 0) {
                    (*trial)->timer = 0;
                    settle(false);
                }
                live->syncSoon();
            }, true);
            *trial = t;
            UmbrielConfig::get().set(path, value);
        };
        for (const auto& o : outs) {
            const std::string name = o.value("name", "");
            if (name.empty()) continue;
            const std::string desc = o.value("description", "");
            auto* g = group(live, name, desc);
            auto on = row(g, "monitor", "On", o.value("enabled", true) ? "Part of the desktop" : "Off");
            auto* sw = on.row->add<Switch>();
            sw->setChecked(o.value("enabled", true), false);
            sw->onToggled = [name](bool v) { UmbrielConfig::get().set({"output", name, "enabled"}, v); };
            if (!o.value("enabled", true)) continue;
            // Its modes, the one in use first
            std::vector<std::pair<std::string, std::string>> modes;
            std::string current;
            for (const auto& m : o.value("modes", json::array())) {
                const std::string text = modeText(m);
                const std::string label = std::format("{}×{} at {:.0f} Hz{}", m.value("width", 0), m.value("height", 0),
                                                      m.value("refresh_mhz", 0) / 1000.0, m.value("preferred", false) ? " (best)" : "");
                if (std::ranges::any_of(modes, [&](const auto& x) { return x.first == text; })) continue;
                modes.push_back({text, label});
                if (m.value("current", false)) current = label;
            }
            if (!modes.empty()) {
                auto r = row(g, "aspect_ratio", "Resolution", "");
                dropdown(r.row, g, current.empty() ? "Choose" : current, modes,
                         [name, tryIt](const std::string& mode) { tryIt({"output", name, "mode"}, mode); });
            }
            const double scale = o.value("scale", 1.0);
            auto sc = row(g, "zoom_in", "Scale", std::format("{:.0f}%", scale * 100));
            connectedGroup<double>(sc.row, std::round(scale * 4) / 4, {{1.0, "100%"}, {1.25, "125%"}, {1.5, "150%"}, {1.75, "175%"}, {2.0, "200%"}},
                                   [name, tryIt](double v) { tryIt({"output", name, "scale"}, v); });
            const std::string transform = o.value("transform", "normal");
            auto rot = row(g, "screen_rotation", "Rotation", "");
            connectedGroup<std::string>(rot.row, transform, {{"normal", "None"}, {"90", "90°"}, {"180", "180°"}, {"270", "270°"}},
                                        [name, tryIt](std::string v) { tryIt({"output", name, "transform"}, v); });
            if (!o["adaptive_sync"].is_null()) {
                const json vrr = cfg.value({"output", name, "vrr"});
                auto v = row(g, "speed", "Variable refresh rate", "Smoother games and video, where the screen can");
                connectedGroup<std::string>(v.row, vrr.is_string() ? vrr.get<std::string>() : "disabled",
                                            {{"disabled", "Off"}, {"fullscreen", "Fullscreen"}, {"always", "Always"}},
                                            [name](std::string x) { UmbrielConfig::get().set({"output", name, "vrr"}, x); });
            }
        }
        auto r = row(group(live, "Arranging them"), "view_quilt", "Their positions",
                     "Each monitor's [output.<name>] position = [x, y] in config.toml; Umbriel lines them up from left to right without one");
        pillButton(r.row, "Open config.toml", false, [] { spawn("xdg-open \"${XDG_CONFIG_HOME:-$HOME/.config}/umbriel/config.toml\""); }, "open_in_new");
    };
    live->watch(UmbrielConfig::get().changed);
    live->watch(Compositor::get().workspacesChanged); // outputs come and go
    live->sync();
}

namespace {

// A slot in words
std::string slotName(const std::string& slot) {
    static const std::pair<const char*, const char*> names[] = {
        {"effects.border", "Border of the focused window"}, {"effects.window", "Windows"},
        {"effects.screen", "Whole screen"},                 {"effects.cursor", "Pointer"},
        {"animation.windows_in", "Opening windows"},       {"animation.windows_out", "Closing windows"},
        {"animation.windows_move", "Moving windows"},      {"animation.workspaces", "Switching workspaces"},
        {"animation.overview", "The overview"},            {"animation.scratchpad", "The scratchpad"},
        {"animation.border", "Border changes"},            {"animation.dim_unfocused", "Dimming other windows"},
        {"animation.layers", "Panels"},
    };
    for (const auto& [id, name] : names)
        if (slot == id) return name;
    return slot;
}

// What an animation is for ("Opening and closing windows")
std::string usesText(const UmbrielShaders::Effect& e) {
    std::vector<std::string> parts;
    for (const auto& u : e.uses) {
        if (!u.starts_with("animation.")) continue;
        std::string n = slotName(u);
        if (!parts.empty()) n[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(n[0])));
        parts.push_back(n);
    }
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return out;
}

bool matches(const UmbrielShaders::Effect& e, const std::string& filter) {
    if (filter.empty()) return true;
    auto low = [](std::string t) {
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    const std::string f = low(filter);
    std::string all = e.name + " " + e.description + " " + e.author;
    for (const auto& t : e.tags) all += " " + t;
    return low(all).find(f) != std::string::npos;
}

} // namespace

void umbrielEffectsPage(Flex* page, Popup&) {
    auto& sh = UmbrielShaders::get();
    sh.refresh();
    auto tab = std::make_shared<std::string>("cursor");
    auto filter = std::make_shared<std::string>();

    // What's on, the catalogue's state and the families
    auto* top = page->add<Live>();
    top->setSpacing(6);
    top->sync = [top] {
        top->clearChildren();
        notice(top);
        auto& sh = UmbrielShaders::get();
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        auto* g = group(top, "Community shaders", "Effects by Noctalia's community, downloaded when you pick one");
        if (sh.state() == UmbrielShaders::State::Ready) {
            auto r = row(g, "auto_fix_high", std::format("{} effects", std::ranges::count_if(sh.effects(), [](const auto& e) { return e.role != "companion"; })),
                         "github.com/noctalia-dev/community-umbriel-shaders · revision " + sh.revision().substr(0, 7));
            pillButton(r.row, "Check for new", false, [] { UmbrielShaders::get().refresh(true); }, "refresh");
        } else if (sh.state() == UmbrielShaders::State::Failed) {
            auto r = row(g, "cloud_off", "The catalogue isn't here", sh.error());
            r.subtitle->setColor("error", false);
            pillButton(r.row, "Try again", true, [] { UmbrielShaders::get().refresh(true); }, "refresh");
        } else {
            row(g, "cloud_download", "Getting the catalogue…", "From GitHub, once a day");
        }
        // How the last one went
        if (!sh.message().empty()) {
            auto* c = top->add<Flex>(Flex::Direction::Row);
            const bool bad = sh.messageIsError();
            c->setPadding({18, 14})->setSpacing(14)->setRadius(Radii(20))->setAlignItems(Align::Start);
            c->setBackground(bad ? "error_container" : "secondary_container", false);
            c->add<Icon>(bad ? "error" : "check_circle", 22)->setColor(bad ? "on_error_container" : "on_secondary_container", false);
            auto* l = c->add<Label>(sh.message());
            l->setSize(14)->setMaxWidth(560)->setMaxLines(ui::kAllLines)->setColor(bad ? "on_error_container" : "on_secondary_container", false);
            l->grow = true;
        }
        // What's on now, each with a way off
        auto* on = group(top, "On now", "");
        const auto used = sh.inUse();
        if (used.empty()) row(on, "info", "Nothing yet", "Pick an effect below: it's downloaded and turned on");
        for (const auto& [slot, preset] : used) {
            const auto* e = sh.byPreset(preset);
            auto r = row(on, slot.starts_with("animation") ? "animation" : "auto_fix_high", e ? e->name : preset,
                         slotName(slot) + (e ? " · by " + e->author : ""));
            const std::string s = slot;
            pillButton(r.row, "Turn off", false, [s] { UmbrielShaders::get().turnOff(s); });
        }
        // Families: matching effects for several slots at once
        if (sh.state() == UmbrielShaders::State::Ready && !sh.families().empty()) {
            auto* fg = group(top, "Families", "Matching effects for several places at once: applying one turns them all on");
            for (const auto& f : sh.families()) {
                if (!f.activation.is_object()) continue;
                const std::string by = f.authors.empty() ? "" : " · by " + f.authors.front();
                auto r = row(fg, "palette", f.name, f.description + std::format(" · {} effects", f.members.size()) + by);
                if (sh.busy() == "family:" + f.id) {
                    r.row->add<Label>("Installing…")->setSize(13)->setColor("on_surface_variant", false);
                } else {
                    const std::string id = f.id;
                    auto* b = pillButton(r.row, "Apply", false, [id] { UmbrielShaders::get().useFamily(id); }, "download");
                    if (!sh.busy().empty()) b->setOpacity(0.4f);
                }
            }
        }
    };
    top->watch(sh.changed);
    top->watch(UmbrielConfig::get().changed);
    top->sync();

    // The effects by kind, and a filter: outside what's rebuilt, so typing keeps its place
    auto* head = page->add<Flex>(Flex::Direction::Column);
    head->setSpacing(10)->setPadding({0, 12, 0, 4})->setAlignItems(Align::Stretch);
    auto* tabs = head->add<Flex>(Flex::Direction::Row);
    auto* findRow = head->add<Flex>(Flex::Direction::Row);
    pill(findRow->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    findRow->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* field = findRow->add<TextField>();
    field->setPlaceholder("Filter: fire, neon, minimal, an author…");
    field->bare = true;
    field->grow = true;

    auto* grid = page->add<Live>();
    grid->setSpacing(6);
    grid->sync = [grid, tab, filter] {
        grid->clearChildren();
        auto& sh = UmbrielShaders::get();
        if (sh.state() != UmbrielShaders::State::Ready) return;
        std::vector<const UmbrielShaders::Effect*> shown;
        for (const auto& e : sh.effects())
            if (e.kind == *tab && e.role != "companion" && matches(e, *filter)) shown.push_back(&e);
        sh.fetchPreviews(shown);
        if (shown.empty()) {
            row(group(grid, ""), "search_off", "Nothing matches", "Another word, or another kind above");
            return;
        }
        std::vector<std::string> selected;
        for (const auto& [slot, preset] : sh.inUse()) selected.push_back(preset);
        auto* flow = grid->add<kit::Flow>();
        flow->spacing = 10;
        for (const auto* e : shown) {
            const bool on = std::ranges::find(selected, e->preset) != selected.end();
            const bool busy = sh.busy() == e->id;
            auto* t = flow->add<Button>(Flex::Direction::Column);
            t->setPadding(Insets(10))->setSpacing(6)->setRadius(Radii(22))->setAlignItems(Align::Start);
            t->setBackground(on ? "secondary_container" : "surface_container_high", false);
            if (on) t->setBorder(3, "primary");
            t->pressedRadius = 14;
            t->fixedWidth = 216;
            // Its preview, once here
            auto* frame = t->add<Flex>(Flex::Direction::Row);
            frame->setRadius(Radii(14))->setBackground("surface_container_highest", false)->setJustify(Align::Center);
            frame->fixedWidth = 196;
            frame->fixedHeight = 110;
            const std::string file = sh.preview(*e);
            if (file.empty()) {
                frame->add<Icon>(e->kind == "animation" ? "animation" : "auto_fix_high", 32)->setColor("on_surface_variant", false);
            } else {
                auto* img = frame->add<ImageView>(196, 110, [file](float scale, int& w, int& h) -> GLuint {
                    auto* tex = Images::get().picture(file, static_cast<int>(196 * scale), static_cast<int>(110 * scale), true);
                    if (!tex) return 0;
                    w = tex->width;
                    h = tex->height;
                    return tex->id;
                });
                img->cover = true;
                img->radius = 14;
            }
            t->add<Label>(e->name)->setSize(14)->setWeight(600)->setMaxWidth(196)->setMaxLines(ui::kAllLines);
            const std::string what = e->kind == "animation" ? usesText(*e) : "";
            t->add<Label>((what.empty() ? "" : what + " · ") + "by " + e->author)->setSize(12)->setMaxWidth(196)->setMaxLines(ui::kAllLines)
                ->setColor("on_surface_variant", false);
            t->add<Label>(e->description)->setSize(12)->setMaxWidth(196)->setMaxLines(ui::kAllLines)->setColor("on_surface_variant", false);
            // Use, or what it is now
            auto* actions = t->add<Flex>(Flex::Direction::Row);
            actions->setSpacing(6)->setAlignItems(Align::Center);
            const std::string id = e->id;
            if (busy) {
                actions->add<Label>("Installing…")->setSize(13)->setColor("on_surface_variant", false);
            } else if (on) {
                actions->add<Icon>("check_circle", 18)->setColor("primary", false);
                actions->add<Label>("On")->setSize(13)->setWeight(600)->setColor("primary", false);
            } else {
                auto* b = pillButton(actions, sh.installed(id) ? "Use" : "Get and use", !sh.installed(id) ? false : true,
                                     [id] { UmbrielShaders::get().use(id); }, sh.installed(id) ? "check" : "download");
                if (!sh.busy().empty()) b->setOpacity(0.4f);
            }
            if (sh.installed(id) && !busy) {
                actions->add<Node>()->grow = true;
                confirmButton(actions, "umbshader:" + id, "delete", [id] { UmbrielShaders::get().remove(id); })->tooltip = "Remove its files";
            }
            t->onClicked = [id, on](uint32_t button) {
                if (button == BTN_LEFT && !on && UmbrielShaders::get().busy().empty()) UmbrielShaders::get().use(id);
            };
        }
    };
    grid->watch(sh.changed);
    grid->watch(UmbrielConfig::get().changed);
    connectedGroup<std::string>(tabs, *tab,
                                {{"cursor", "Pointer"}, {"border", "Borders"}, {"window", "Windows"}, {"screen", "Screen"}, {"animation", "Animations"}},
                                [tab, grid](std::string v) {
                                    *tab = v;
                                    grid->syncSoon();
                                });
    field->onChanged = [filter, grid](const std::string& t) {
        *filter = t;
        grid->syncSoon();
    };
    grid->sync();

    auto* foot = group(page, "About these", "");
    row(foot, "gavel", "Their licenses", "Each keeps its author's license, installed beside it in ~/.config/umbriel/shaders/community");
    row(foot, "memory", "On the graphics card", "Effects run on every frame they're seen: a busy one costs battery. One your Umbriel can't compile is turned off again");
}

namespace {

// "repeat_delay" → "Repeat delay", "hot_corners" → "Hot corners"
std::string humane(std::string key) {
    for (const char* unit : {"_ms", "_px"})
        if (key.ends_with(unit)) key.resize(key.size() - 3);
    for (auto& c : key)
        if (c == '_') c = ' ';
    if (!key.empty()) key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    return key;
}

std::string lastPart(const std::string& dotted) {
    const auto dot = dotted.rfind('.');
    return dot == std::string::npos ? dotted : dotted.substr(dot + 1);
}

std::string parentOf(const std::string& dotted) {
    const auto dot = dotted.rfind('.');
    return dot == std::string::npos ? std::string() : dotted.substr(0, dot);
}

// Lists of tables, names you choose, and whole tables: their own pages (keys,
// rules, monitors) or config.toml, not one control each
bool generated(const std::string& p, const json& s) {
    if (p.find('<') != std::string::npos || p.find("[]") != std::string::npos) return false;
    const std::string section = p.substr(0, p.find('.'));
    static const std::set<std::string> elsewhere = {"include", "keybinds", "output", "window_rule", "layer_rule", "workspace", "security_context_rule", "environment"};
    if (elsewhere.contains(section)) return false;
    static const std::set<std::string> kinds = {"bool", "int", "float", "enum", "string", "color", "string_array", "int_array", "float_array"};
    return kinds.contains(s.value("type", ""));
}

// An option's control, from what Umbriel says of it
std::optional<Opt> fromSchema(const std::string& p, const json& s) {
    if (!generated(p, s)) return std::nullopt;
    const std::string type = s.value("type", "");
    Opt o;
    o.path = p;
    o.title = humane(lastPart(p));
    o.def = s.contains("default") ? s["default"] : json();
    std::string sub = p, dflt;
    if (!o.def.is_null()) dflt = o.def.is_string() ? (o.def.get<std::string>().empty() ? "none" : o.def.get<std::string>()) : o.def.dump();
    if (p.ends_with("_ms")) o.unit = "ms";
    else if (p.ends_with("_px")) o.unit = "px";
    const bool bounded = s.contains("min") && s.contains("max") && s["min"].is_number() && s["max"].is_number();
    if (bounded) {
        o.min = s["min"].get<double>();
        o.max = s["max"].get<double>();
    }
    const json values = s.contains("values") && s["values"].is_array() ? s["values"] : json::array();
    if (type == "bool") {
        o.kind = K::Bool;
        o.icon = "toggle_on";
    } else if ((type == "int" || type == "float") && bounded && o.max > o.min) {
        const double range = o.max - o.min;
        if (type == "int" && range <= 10000) {
            o.kind = K::Int;
            o.step = range > 2000 ? 10 : 1;
        } else if (type == "float" && range <= 10000) {
            o.kind = K::Float;
            // About two hundred steps, on a round number
            const double raw = range / 200;
            double step = 0.001;
            for (double nice : {0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0, 10.0, 50.0})
                if (nice <= raw) step = nice;
            o.step = step;
        } else {
            o.kind = K::Number;
            o.integer = type == "int";
        }
        o.icon = "tune";
        if (!o.def.is_number()) o.def = o.min;
    } else if (type == "int" || type == "float") {
        o.kind = K::Number;
        o.integer = type == "int";
        o.icon = "tune";
    } else if ((type == "enum" || type == "string") && !values.empty()) {
        o.kind = K::Choice;
        o.icon = "list";
        for (const auto& v : values)
            if (v.is_string()) o.choices.push_back({v.get<std::string>(), humane(v.get<std::string>())});
        if (o.choices.empty()) return std::nullopt;
    } else if (type == "string" || type == "color") {
        o.kind = K::Text;
        o.icon = type == "color" ? "palette" : "text_fields";
        if (type == "color") sub += " · #RRGGBBAA";
    } else {
        o.kind = K::List;
        o.icon = "format_list_bulleted";
        o.listOf = type == "int_array" ? "int" : type == "float_array" ? "float" : "string";
    }
    // A slider says its value, and the default after it; the rest, the default here
    if (o.kind == K::Int || o.kind == K::Float) o.defaultText = dflt;
    else if (!dflt.empty()) sub += " · default " + dflt;
    o.sub = sub;
    return o;
}

} // namespace

void umbrielAllPage(Flex* page, Popup&) {
    auto section = std::make_shared<std::string>("appearance");
    auto filter = std::make_shared<std::string>();

    // What it's from, and what's new in this Umbriel
    auto* top = page->add<Live>();
    top->setSpacing(6);
    top->sync = [top] {
        top->clearChildren();
        notice(top);
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty()) return;
        auto* g = group(top, "From your Umbriel", "");
        if (!cfg.hasSchema()) {
            row(g, "info", "Umbriel isn't installed", "These options come from Umbriel itself (umbriel config schema)");
            return;
        }
        const auto count = std::ranges::count_if(cfg.schemaAll(), [](const auto& kv) { return generated(kv.first, kv.second); });
        row(g, "auto_awesome", std::format("Umbriel {}: {} options", cfg.umbrielVersion(), count),
            "Made from its own description of them, so a newer Umbriel's options show up here as it adds them");
        // New since the Umbriel you had before
        std::vector<Opt> fresh;
        for (const auto& p : cfg.newOptions())
            if (const auto* s = cfg.schema(p))
                if (auto o = fromSchema(p, *s)) fresh.push_back(std::move(*o));
        if (!fresh.empty()) {
            auto* ng = group(top, std::format("New in this Umbriel ({})", fresh.size()),
                             cfg.newSince().empty() ? std::string() : "Since " + cfg.newSince());
            for (const auto& o : fresh) option(ng, o);
        }
    };
    top->watch(UmbrielConfig::get().changed);
    top->sync();

    // The section, and a filter over all of them: outside what's rebuilt
    auto* head = page->add<Flex>(Flex::Direction::Column);
    head->setSpacing(10)->setPadding({0, 12, 0, 4})->setAlignItems(Align::Stretch);
    auto* pick = head->add<Flex>(Flex::Direction::Column);
    pick->setAlignItems(Align::Stretch);
    auto* findRow = head->add<Flex>(Flex::Direction::Row);
    pill(findRow->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    findRow->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* field = findRow->add<TextField>();
    field->setPlaceholder("Find an option in every section: blur, repeat, cursor…");
    field->bare = true;
    field->grow = true;

    auto* list = page->add<Live>();
    list->setSpacing(6);
    list->sync = [list, section, filter, pick] {
        list->clearChildren();
        auto& cfg = UmbrielConfig::get();
        if (!cfg.readError().empty() || !cfg.hasSchema()) return;
        // The sections, as this Umbriel has them
        std::map<std::string, int> sections;
        for (const auto& [p, s] : cfg.schemaAll())
            if (generated(p, s)) ++sections[p.substr(0, p.find('.'))];
        if (!sections.contains(*section) && !sections.empty()) *section = sections.begin()->first;
        pick->clearChildren();
        std::vector<std::pair<std::string, std::string>> items;
        for (const auto& [name, n] : sections) items.push_back({name, std::format("{} ({})", humane(name), n)});
        auto r = row(pick, "category", "Section", filter->empty() ? "" : "Finding in every section");
        dropdown(r.row, pick, std::format("{} ({})", humane(*section), sections[*section]), items, [section, list](const std::string& v) {
            *section = v;
            list->syncSoon();
        });
        // Its options (or every section's that match), by the table they're in
        auto low = [](std::string t) {
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        const std::string f = low(*filter);
        std::map<std::string, std::vector<Opt>> tables;
        int shown = 0;
        for (const auto& [p, s] : cfg.schemaAll()) {
            const bool hit = f.empty() ? p.substr(0, p.find('.')) == *section : low(p + " " + humane(lastPart(p))).find(f) != std::string::npos;
            if (!hit || shown >= 120) continue;
            if (auto o = fromSchema(p, s)) {
                tables[parentOf(p)].push_back(std::move(*o));
                ++shown;
            }
        }
        if (tables.empty()) {
            row(group(list, ""), "search_off", "Nothing matches", "Another word, or a section above");
            return;
        }
        for (const auto& [parent, opts] : tables) {
            auto* g = group(list, parent.find('.') == std::string::npos ? humane(parent) : humane(lastPart(parent)), parent);
            for (const auto& o : opts) option(g, o);
        }
        if (shown >= 120) caption(list, "The first 120: a longer word finds fewer");
    };
    list->watch(UmbrielConfig::get().changed);
    field->onChanged = [filter, list](const std::string& t) {
        *filter = t;
        list->syncSoon();
    };
    list->sync();

    auto* foot = group(page, "Not here", "");
    row(foot, "rule", "Keys, window rules, monitors and effects", "On their own pages; anything else that's a list, in config.toml");
}

} // namespace m3::shell::panels

