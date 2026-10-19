// Settings → Hyprland: every compositor option, from what the running
// Hyprland describes (services/HyprOptions). The catalogue below only
// groups and names them; anything it doesn't place lands in "Other", so new
// Hyprland options show up without a change here.
#include "shell/HyprPages.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Signal.h"
#include "services/HyprOptions.h"
#include "services/Hyprland.h"
#include "services/Packages.h"
#include "services/Search.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "shell/CurveEditor.h"
#include "shell/MonitorLayout.h"
#include "shell/SettingsKit.h"

#include <cmath>
#include <format>
#include <functional>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <set>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <unistd.h>

namespace m3::shell::panels {

using namespace m3::shell::kit;

namespace {

using Option = HyprOptions::Option;
using Kind = HyprOptions::Kind;

// A section of a page: the options whose names start with one of `prefixes`
// and have no deeper section of their own (blur stays out of "Decoration")
struct Section {
    const char* title;
    std::vector<std::string> prefixes;
    std::vector<std::string> names = {}; // exact names, placed before prefixes
};
struct Page {
    const char* id;
    std::vector<Section> sections;
};

// The catalogue: pages, then their sections in order
const std::vector<Page>& catalogue() {
    static const std::vector<Page> pages = {
        {"hypr-windows",
         {{"Gaps", {}, {"general:gaps_in", "general:gaps_out", "general:float_gaps", "general:gaps_workspaces"}},
          {"Borders",
           {},
           {"general:border_size", "general:resize_on_border", "general:extend_border_grab_area",
            "general:hover_icon_on_border", "general:resize_corner"}},
          {"Border colours", {"general:col."}},
          {"Snapping", {"general:snap:"}},
          {"Behaviour", {"general:"}}}},
        {"hypr-decoration",
         {{"Corners, opacity and dimming", {"decoration:"}},
          {"Blur", {"decoration:blur:"}},
          {"Shadow", {"decoration:shadow:"}},
          {"Glow", {"decoration:glow:"}},
          {"Motion blur", {"decoration:motion_blur:"}}}},
        {"hypr-input",
         {{"Keyboard",
           {"input:kb_"},
           {"input:repeat_rate", "input:repeat_delay", "input:numlock_by_default", "input:resolve_binds_by_sym"}},
          {"Pointer and focus", {"input:"}},
          {"Touchpad", {"input:touchpad:"}},
          {"Touchscreen", {"input:touchdevice:"}},
          {"Tablet", {"input:tablet:", "input:tablettool:"}},
          {"Virtual keyboards", {"input:virtualkeyboard:"}},
          {"Input capture", {"input-capture:"}}}},
        {"hypr-cursor", {{"Cursor", {"cursor:"}}}},
        {"hypr-gestures", {{"Workspace swipe", {"gestures:"}}, {"Scrolling layout", {"gestures:scrolling:"}}}},
        {"hypr-layouts",
         {{"All layouts", {"layout:"}, {"general:layout"}},
          {"Dwindle", {"dwindle:"}},
          {"Master", {"master:"}},
          {"Scrolling", {"scrolling:"}}}},
        {"hypr-groups", {{"Groups", {"group:"}}, {"Group bar", {"group:groupbar:"}}}},
        {"hypr-misc",
         {{"Behaviour", {"misc:"}},
          {"Keybinds", {"binds:"}},
          {"XWayland", {"xwayland:"}},
          {"Ecosystem", {"ecosystem:"}},
          {"Quirks", {"quirks:"}}}},
        {"hypr-render", {{"Rendering", {"render:"}}, {"OpenGL", {"opengl:"}}, {"Experimental", {"experimental:"}}}},
        {"hypr-debug", {{"Debug", {"debug:"}}}},
        {"hypr-animations", {{"Animations", {"animations:"}}}},
    };
    return pages;
}

// Friendlier names for options people look for; the rest come from the key
const std::map<std::string, std::string> kNames = {
    {"general:gaps_in", "Gaps between windows"},
    {"general:gaps_out", "Gaps at the screen edges"},
    {"general:float_gaps", "Gaps for floating windows"},
    {"general:gaps_workspaces", "Gap between workspaces"},
    {"general:border_size", "Border width"},
    {"general:resize_on_border", "Resize by dragging borders"},
    {"general:col.active_border", "Active window border"},
    {"general:col.inactive_border", "Inactive window border"},
    {"general:col.nogroup_border", "Ungroupable window border"},
    {"general:col.nogroup_border_active", "Ungroupable window border (active)"},
    {"general:layout", "Window layout"},
    {"general:allow_tearing", "Allow tearing (games)"},
    {"decoration:rounding", "Corner radius"},
    {"decoration:rounding_power", "Corner shape (squircle power)"},
    {"decoration:active_opacity", "Active window opacity"},
    {"decoration:inactive_opacity", "Inactive window opacity"},
    {"decoration:fullscreen_opacity", "Fullscreen window opacity"},
    {"decoration:dim_inactive", "Dim inactive windows"},
    {"decoration:dim_strength", "Dimming strength"},
    {"decoration:blur:enabled", "Blur"},
    {"decoration:blur:size", "Blur size"},
    {"decoration:blur:passes", "Blur passes"},
    {"decoration:shadow:enabled", "Shadows"},
    {"decoration:shadow:range", "Shadow size"},
    {"decoration:shadow:color", "Shadow colour"},
    {"input:kb_layout", "Keyboard layouts"},
    {"input:kb_model", "Keyboard model"},
    {"input:kb_rules", "Keyboard rules"},
    {"input:kb_file", "Keymap file"},
    {"input:kb_variant", "Layout variants"},
    {"input:kb_options", "Keyboard options (XKB)"},
    {"input:repeat_rate", "Key repeat rate"},
    {"input:repeat_delay", "Key repeat delay"},
    {"input:sensitivity", "Pointer speed"},
    {"input:accel_profile", "Pointer acceleration"},
    {"input:follow_mouse", "Focus follows the mouse"},
    {"input:natural_scroll", "Natural scrolling (mouse)"},
    {"input:touchpad:natural_scroll", "Natural scrolling"},
    {"input:touchpad:tap-to-click", "Tap to click"},
    {"input:touchpad:disable_while_typing", "Ignore while typing"},
    {"cursor:inactive_timeout", "Hide the cursor after (seconds)"},
    {"cursor:no_hardware_cursors", "Software cursor"},
    {"cursor:zoom_factor", "Zoom"},
    {"misc:disable_hyprland_logo", "Hide the Hyprland logo"},
    {"misc:disable_splash_rendering", "Hide the splash text"},
    {"misc:vrr", "Variable refresh rate"},
    {"misc:animate_manual_resizes", "Animate manual resizes"},
    {"misc:focus_on_activate", "Focus windows that ask for it"},
    {"animations:enabled", "Animations"},
};

std::string nameOf(const Option& o) {
    if (auto it = kNames.find(o.name); it != kNames.end()) return it->second;
    std::string key = o.name.substr(o.name.find_last_of(":.") + 1);
    for (auto& c : key)
        if (c == '_' || c == '-') c = ' ';
    if (!key.empty()) key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    return key;
}

// The section an option belongs to on a page: an exact name wins, then
// the longest matching prefix (so "decoration:blur:" beats "decoration:")
std::pair<const Page*, size_t> placeOf(const std::string& name) {
    const Page* bestPage = nullptr;
    size_t bestSection = 0, bestLen = 0;
    for (const auto& p : catalogue())
        for (size_t i = 0; i < p.sections.size(); ++i) {
            for (const auto& n : p.sections[i].names)
                if (n == name) return {&p, i};
            for (const auto& pre : p.sections[i].prefixes) {
                if (name.starts_with(pre) && pre.size() > bestLen) {
                    bestPage = &p;
                    bestSection = i;
                    bestLen = pre.size();
                }
            }
        }
    return {bestPage, bestSection};
}

// ── Values as text, for the fields ──

std::string numberText(double v, bool integer) {
    if (integer) return std::to_string(std::llround(v));
    std::string s = std::format("{:.3f}", v);
    while (s.ends_with('0')) s.pop_back();
    if (s.ends_with('.')) s.pop_back();
    return s;
}

std::string textOf(const Option& o, const nlohmann::json& v) {
    switch (o.kind) {
    case Kind::Text:
    case Kind::Color: return v.is_string() ? v.get<std::string>() : v.dump();
    case Kind::Int: return v.is_number() ? numberText(v.get<double>(), true) : v.dump();
    case Kind::Float: return v.is_number() ? numberText(v.get<double>(), false) : v.dump();
    case Kind::Gradient: {
        std::string out;
        for (const auto& c : v.value("colors", nlohmann::json::array())) out += c.get<std::string>() + " ";
        return out + std::to_string(v.value("angle", 0)) + "deg";
    }
    case Kind::Vec2: return numberText(v[0].get<double>(), false) + " " + numberText(v[1].get<double>(), false);
    case Kind::Gaps: {
        const int t = v[0], r = v[1], b = v[2], l = v[3];
        return t == r && r == b && b == l ? std::to_string(t) : std::format("{} {} {} {}", t, r, b, l);
    }
    default: return v.dump();
    }
}

// Text from a field back to a value; nullopt when it doesn't read
std::optional<nlohmann::json> parse(const Option& o, const std::string& text) {
    std::istringstream in(text);
    std::vector<std::string> words;
    for (std::string w; in >> w;) words.push_back(w);
    try {
        switch (o.kind) {
        case Kind::Text: return nlohmann::json(text);
        case Kind::Int: return nlohmann::json(std::stoll(text));
        case Kind::Float: return nlohmann::json(std::stod(text));
        case Kind::Color: {
            if (words.size() != 1) return std::nullopt;
            std::string w = words[0];
            // Also take #RRGGBB / #RRGGBBAA, as colour pickers write them
            if (w.starts_with('#')) w = "rgba(" + w.substr(1) + (w.size() == 7 ? "ff" : "") + ")";
            return nlohmann::json(w);
        }
        case Kind::Gradient: {
            nlohmann::json g{{"colors", nlohmann::json::array()}, {"angle", 0}};
            for (const auto& w : words) {
                if (w.ends_with("deg")) g["angle"] = std::stoi(w);
                else g["colors"].push_back(w.starts_with('#') ? "rgba(" + w.substr(1) + (w.size() == 7 ? "ff" : "") + ")" : w);
            }
            if (g["colors"].empty()) return std::nullopt;
            return g;
        }
        case Kind::Vec2:
            if (words.size() != 2) return std::nullopt;
            return nlohmann::json{std::stod(words[0]), std::stod(words[1])};
        case Kind::Gaps: {
            if (words.empty() || words.size() > 4) return std::nullopt;
            std::vector<int> n;
            for (const auto& w : words) n.push_back(std::stoi(w));
            // CSS order, as Hyprland reads it: 1, 2 (vertical horizontal) or 4 values
            if (n.size() == 1) n = {n[0], n[0], n[0], n[0]};
            else if (n.size() == 2) n = {n[0], n[1], n[0], n[1]};
            else if (n.size() == 3) n = {n[0], n[1], n[2], n[1]};
            return nlohmann::json{n[0], n[1], n[2], n[3]};
        }
        default: return std::nullopt;
        }
    } catch (...) {
        return std::nullopt;
    }
}

// "rgba(RRGGBBAA)" → a colour for a swatch
std::optional<Color> colorOf(const std::string& s) {
    const auto open = s.find('('), close = s.find(')');
    std::string hex = open != std::string::npos && close != std::string::npos ? s.substr(open + 1, close - open - 1) : s;
    if (hex.starts_with('#')) hex = hex.substr(1);
    if (hex.size() != 6 && hex.size() != 8) return std::nullopt;
    try {
        const auto byte = [&](size_t i) { return static_cast<float>(std::stoi(hex.substr(i, 2), nullptr, 16)) / 255.0f; };
        return Color{byte(0), byte(2), byte(4), hex.size() == 8 ? byte(6) : 1.0f};
    } catch (...) {
        return std::nullopt;
    }
}

void swatch(Node* parent, const std::string& color) {
    auto* s = parent->add<Flex>(Flex::Direction::Row);
    s->fixedWidth = s->fixedHeight = 28;
    pill(s)->setBorder(1, ColorRef("outline_variant"));
    if (auto c = colorOf(color)) s->setBackground(ColorRef(*c), false);
}

// One option's row: its name and Hyprland's description, the control for
// its kind, and a reset while Settings keeps a value for it
void optionRow(Node* g, const Option& opt) {
    const std::string name = opt.name;
    // A description that only repeats the name says nothing
    auto same = [](std::string a, std::string b) {
        for (auto* t : {&a, &b}) {
            for (auto& c : *t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            while (!t->empty() && (t->back() == '.' || t->back() == ' ')) t->pop_back();
        }
        return a == b;
    };
    const std::string description = same(opt.description, nameOf(opt)) ? std::string() : opt.description;
    auto r = row(g, "", nameOf(opt), description);
    // Narrower than other rows: the controls here are wider
    r.title->setMaxWidth(320);
    r.subtitle->setMaxWidth(320)->setMaxLines(3);
    auto* tail = r.row->add<Flex>(Flex::Direction::Row);
    tail->setSpacing(8);
    auto* error = r.subtitle;

    // After a change: the reset shows, or Hyprland's complaint replaces the description
    auto apply = [name, error, description](const nlohmann::json& v) {
        const std::string err = HyprOptions::get().set(name, v);
        error->setText(err.empty() ? description : err);
        error->setColor(err.empty() ? "on_surface_variant" : "error");
        error->setVisible(!error->text().empty());
    };

    switch (opt.kind) {
    case Kind::Bool: {
        auto* sw = tail->add<Switch>();
        sw->setChecked(opt.value.get<bool>(), false);
        sw->onToggled = [apply](bool on) { apply(on); };
        break;
    }
    case Kind::Choice: {
        if (opt.choices.size() <= 4) {
            connectedGroup<std::string>(tail, opt.value.dump(),
                                        [&] {
                                            std::vector<std::pair<std::string, std::string>> out;
                                            for (const auto& [v, l] : opt.choices) out.push_back({v.dump(), l});
                                            return out;
                                        }(),
                                        [apply](std::string v) { apply(nlohmann::json::parse(v)); });
            break;
        }
        // Many choices: a dropdown under the row
        std::string current = opt.value.dump();
        for (const auto& [v, l] : opt.choices)
            if (v == opt.value) current = l;
        auto* drop = tail->add<Button>(Flex::Direction::Row);
        pill(drop->setBackground("surface_container_highest", false))->setPadding({14, 8})->setSpacing(6);
        auto* shown = drop->add<Label>(current);
        shown->setSize(14)->setWeight(500);
        auto* chevron = drop->add<Icon>("expand_more", 18);
        auto* menu = g->add<Flex>(Flex::Direction::Column);
        menu->setPadding({20, 4, 20, 12})->setSpacing(2)->setAlignItems(Align::Stretch);
        menu->setVisible(false);
        for (const auto& [v, l] : opt.choices) {
            auto* item = menu->add<Button>(Flex::Direction::Row);
            pill(item)->setPadding({14, 8});
            item->add<Label>(l)->setSize(14);
            item->onClicked = [apply, v = v, l = l, shown, menu, chevron](uint32_t button) {
                if (button != BTN_LEFT) return;
                apply(v);
                shown->setText(l);
                menu->setVisible(false);
                chevron->setText("expand_more");
            };
        }
        drop->onClicked = [menu, chevron](uint32_t button) {
            if (button != BTN_LEFT) return;
            menu->setVisible(!menu->visible());
            chevron->setText(menu->visible() ? "expand_less" : "expand_more");
        };
        break;
    }
    case Kind::Int:
    case Kind::Float: {
        const bool integer = opt.kind == Kind::Int;
        if (opt.ranged && opt.max > opt.min) {
            // A slider, with the value beside it
            auto* value = tail->add<Label>(numberText(opt.value.get<double>(), integer));
            value->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
            value->minWidth = 44;
            auto* sl = tail->add<Slider>();
            sl->fixedWidth = 200;
            const double step = integer ? 1 : (opt.max - opt.min) / 100;
            sl->setRange(static_cast<float>(opt.min), static_cast<float>(opt.max))
                ->setStep(static_cast<float>(step / (opt.max - opt.min)));
            sl->setValue(static_cast<float>(opt.value.get<double>()), false);
            sl->onChanged = [value, integer, step](float v) { value->setText(numberText(std::round(v / step) * step, integer)); };
            sl->onCommitted = [apply, integer, step](float v) {
                const double snapped = std::round(v / step) * step;
                apply(integer ? nlohmann::json(std::llround(snapped)) : nlohmann::json(snapped));
            };
            break;
        }
        [[fallthrough]];
    }
    case Kind::Gaps:
        // (unranged numbers fall through here too: only gaps take a slider)
        if (opt.kind == Kind::Gaps && opt.value[0] == opt.value[1] && opt.value[1] == opt.value[2] && opt.value[2] == opt.value[3]) {
            // Even on every side: a slider, and a pencil for per-side values
            auto* value = tail->add<Label>(std::to_string(opt.value[0].get<int>()));
            value->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
            value->minWidth = 28;
            auto* sl = tail->add<Slider>();
            sl->fixedWidth = 180;
            sl->setRange(0, 100)->setStep(0.01f);
            sl->setValue(static_cast<float>(opt.value[0].get<int>()), false);
            sl->onChanged = [value](float v) { value->setText(std::to_string(std::lround(v))); };
            sl->onCommitted = [apply](float v) {
                const int n = static_cast<int>(std::lround(v));
                apply(nlohmann::json{n, n, n, n});
            };
            auto* sides = g->add<Flex>(Flex::Direction::Row);
            sides->setPadding({20, 4, 20, 12})->setSpacing(12);
            sides->setVisible(false);
            sides->add<Label>("Per side (top right bottom left)")->setSize(13)->setColor("on_surface_variant", false)->grow = true;
            auto* f = sides->add<TextField>();
            f->setText(textOf(opt, opt.value));
            f->fixedWidth = 140;
            const Option copy = opt;
            f->onSubmit = [apply, copy](const std::string& text) {
                if (auto v = parse(copy, text)) apply(*v);
            };
            iconButton(tail, "edit", [sides] { sides->setVisible(!sides->visible()); }, 18);
            break;
        }
        [[fallthrough]];
    default: {
        // Typed: Enter applies (colours with a swatch beside them)
        if (opt.kind == Kind::Color) swatch(tail, opt.value.get<std::string>());
        if (opt.kind == Kind::Gradient)
            for (const auto& c : opt.value.value("colors", nlohmann::json::array())) swatch(tail, c.get<std::string>());
        auto* f = tail->add<TextField>();
        f->setText(textOf(opt, opt.value));
        f->fixedWidth = opt.kind == Kind::Gradient ? 300 : opt.kind == Kind::Text || opt.kind == Kind::Color ? 220
                      : opt.kind == Kind::Gaps                                                       ? 96
                                                                                                     : 120;
        f->setPlaceholder(opt.kind == Kind::Gaps ? "e.g. 5 or 5 10" : opt.kind == Kind::Color ? "rgba(RRGGBBAA)" : "");
        const Option copy = opt;
        f->onSubmit = [apply, copy, error](const std::string& text) {
            if (auto v = parse(copy, text)) return apply(*v);
            error->setText("That doesn't read as a value for this option");
            error->setColor("error");
        };
        break;
    }
    }

    if (opt.custom) {
        // Kept by Settings: one tap goes back to what the config files had
        iconButton(r.row, "restart_alt", [name] { HyprOptions::get().reset(name); });
    }
}

// The page's sections, rebuilt when the options change (a reset, an undo,
// a config reload)
void buildPage(Flex* page, const std::string& id) {
    auto& h = HyprOptions::get();
    if (!h.loaded()) h.load();
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live, id] {
        live->clearChildren();
        auto& h = HyprOptions::get();
        if (!Hyprland::get().available() || !h.loaded()) {
            auto* g = group(live, "Hyprland");
            row(g, "info", "Hyprland isn't running", "These settings need a running Hyprland (0.55 or newer)");
            return;
        }
        // Hyprland's own complaints about the config, first
        std::istringstream errors(Hyprland::get().request("configerrors"));
        std::vector<std::string> lines;
        for (std::string l; std::getline(errors, l);)
            if (l.find_first_not_of(" \t") != std::string::npos) lines.push_back(l);
        if (!lines.empty()) {
            auto* g = group(live, "Config errors", "Hyprland couldn't apply these lines of the config");
            for (const auto& l : lines) {
                auto r = row(g, "error", l);
                static_cast<Icon*>(r.row->children()[0].get())->setColor("error", false);
                r.title->setSize(13)->setMaxLines(4)->setMaxWidth(640);
            }
            pillButton(g->add<Flex>(Flex::Direction::Row)->setPadding({20, 8}), "Reload the config", false,
                       [] { Hyprland::get().request("reload"); }, "refresh");
        }

        const Page* page = nullptr;
        for (const auto& p : catalogue())
            if (id == p.id) page = &p;
        if (!page) return;
        std::vector<std::vector<const Option*>> bySection(page->sections.size());
        std::vector<const Option*> other;
        for (const auto& o : h.options()) {
            auto [p, i] = placeOf(o.name);
            if (p == page) bySection[i].push_back(&o);
            else if (!p && id == "hypr-misc") other.push_back(&o);
        }
        // Exact names in the catalogue's order first, the rest as Hyprland lists them
        for (size_t i = 0; i < page->sections.size(); ++i) {
            auto& list = bySection[i];
            const auto& names = page->sections[i].names;
            std::ranges::stable_sort(list, [&](const Option* a, const Option* b) {
                const auto ia = std::ranges::find(names, a->name), ib = std::ranges::find(names, b->name);
                return ia < ib;
            });
            if (list.empty()) continue;
            auto* g = group(live, page->sections[i].title);
            for (const auto* o : list) optionRow(g, *o);
        }
        if (!other.empty()) {
            auto* g = group(live, "Other", "Options Hyprland added that Settings has no place for yet");
            for (const auto* o : other) optionRow(g, *o);
        }
    };
    live->conns.push_back(h.changed.connect([live] { live->syncSoon(); }));
    // A config reload may change what the files set
    live->conns.push_back(Hyprland::get().event.connect([live](const std::string& name, const std::string&) {
        if (name == "configreloaded")
            EventLoop::get().defer([live] {
                HyprOptions::get().load();
                live->syncSoon();
            });
    }));
    live->sync();
}


// ── Animations ──

// Animation leaves by family, with names people know them by
struct Family {
    const char* title;
    std::vector<std::string> leaves;
};
const std::vector<Family> kFamilies = {
    {"Windows", {"windows", "windowsIn", "windowsOut", "windowsMove"}},
    {"Panels, menus and notifications", {"layers", "layersIn", "layersOut"}},
    {"Workspaces", {"workspaces", "workspacesIn", "workspacesOut", "specialWorkspace", "specialWorkspaceIn", "specialWorkspaceOut"}},
    {"Fades",
     {"fade", "fadeIn", "fadeOut", "fadeSwitch", "fadeShadow", "fadeDim", "fadeLayers", "fadeLayersIn", "fadeLayersOut",
      "fadePopups", "fadePopupsIn", "fadePopupsOut", "fadeDpms", "fadeGlow"}},
    {"Borders and the rest", {"global", "border", "borderangle", "glowangle", "shadowangle", "zoomFactor", "monitorAdded"}},
};
const std::map<std::string, std::string> kLeafNames = {
    {"global", "Everything (the default)"},
    {"windows", "Windows"}, {"windowsIn", "Windows opening"}, {"windowsOut", "Windows closing"},
    {"windowsMove", "Windows moving"},
    {"layers", "Panels and menus"}, {"layersIn", "Panels appearing"}, {"layersOut", "Panels leaving"},
    {"workspaces", "Workspaces"}, {"workspacesIn", "Workspace arriving"}, {"workspacesOut", "Workspace leaving"},
    {"specialWorkspace", "Special workspace"}, {"specialWorkspaceIn", "Special workspace opening"},
    {"specialWorkspaceOut", "Special workspace closing"},
    {"fade", "Fades"}, {"fadeIn", "Fade in"}, {"fadeOut", "Fade out"}, {"fadeSwitch", "Focus change"},
    {"fadeShadow", "Shadows"}, {"fadeDim", "Dimming"}, {"fadeLayers", "Panel fades"}, {"fadeLayersIn", "Panel fade in"},
    {"fadeLayersOut", "Panel fade out"}, {"fadePopups", "Popup fades"}, {"fadePopupsIn", "Popup fade in"},
    {"fadePopupsOut", "Popup fade out"}, {"fadeDpms", "Screen on and off"}, {"fadeGlow", "Glow"},
    {"border", "Border colour"}, {"borderangle", "Gradient border turning"}, {"glowangle", "Glow turning"},
    {"shadowangle", "Shadow turning"}, {"zoomFactor", "Zoom"}, {"monitorAdded", "Monitor plugged in"},
};

std::string leafName(const std::string& leaf) {
    auto it = kLeafNames.find(leaf);
    return it != kLeafNames.end() ? it->second : leaf;
}

// The styles a leaf's family takes ("" = Hyprland's default for it)
std::vector<std::string> stylesFor(const std::string& leaf) {
    if (leaf.starts_with("windows"))
        return {"", "slide", "slide top", "slide bottom", "slide left", "slide right", "popin 80%", "popin 90%", "gnomed"};
    if (leaf.starts_with("layers")) return {"", "slide", "popin", "fade"};
    if (leaf.starts_with("workspaces") || leaf.starts_with("specialWorkspace"))
        return {"", "slide", "slidevert", "fade", "slidefade 20%", "slidefadevert 20%"};
    if (leaf == "borderangle" || leaf == "glowangle" || leaf == "shadowangle") return {"", "once", "loop"};
    return {};
}

Bezier curveOf(const std::string& name) {
    for (const auto& c : HyprOptions::get().curves())
        if (c.name == name) return {c.x0, c.y0, c.x1, c.y1};
    return {0, 0.75, 0.15, 1}; // Hyprland's "default"
}

// Hyprland counts speed in tenths of a second
std::string seconds(double speed) {
    return numberText(speed / 10, false) + " s";
}

// A dropdown below `anchor`'s row: a pill showing the choice, opening a list
// inside `g`
Button* dropdown(Node* tail, Node* g, const std::string& shown, const std::vector<std::pair<std::string, std::string>>& items,
                 std::function<void(const std::string&)> pick, std::function<Node*(Node*, const std::string&)> lead = {}) {
    auto* drop = tail->add<Button>(Flex::Direction::Row);
    pill(drop->setBackground("surface_container_highest", false))->setPadding({14, 8})->setSpacing(6);
    drop->add<Label>(shown)->setSize(14)->setWeight(500);
    auto* chevron = drop->add<Icon>("expand_more", 18);
    auto* menu = g->add<Flex>(Flex::Direction::Column);
    menu->setPadding({20, 4, 20, 12})->setSpacing(2)->setAlignItems(Align::Stretch);
    menu->setVisible(false);
    for (const auto& [value, label] : items) {
        auto* item = menu->add<Button>(Flex::Direction::Row);
        pill(item)->setPadding({14, 8})->setSpacing(12);
        if (lead) lead(item, value);
        item->add<Label>(label)->setSize(14);
        item->onClicked = [pick, value = value](uint32_t button) {
            if (button == BTN_LEFT) pick(value);
        };
    }
    drop->onClicked = [menu, chevron](uint32_t button) {
        if (button != BTN_LEFT) return;
        menu->setVisible(!menu->visible());
        chevron->setText(menu->visible() ? "expand_less" : "expand_more");
    };
    return drop;
}

// One leaf's editor: on/off, speed, curve, style and a preview
void leafEditor(Node* g, const HyprOptions::Animation& a) {
    const std::string leaf = a.name;
    // Every change sends the whole leaf, starting from what it is now
    auto change = [leaf](const std::function<void(HyprOptions::Animation&)>& edit) {
        const auto* now = HyprOptions::get().animation(leaf);
        if (!now) return;
        HyprOptions::Animation next = *now;
        edit(next);
        HyprOptions::get().setAnimation(leaf, next.enabled, next.speed, next.curve, next.style);
    };

    auto on = row(g, "", "On");
    auto* sw = on.row->add<Switch>();
    sw->setChecked(a.enabled, false);
    sw->onToggled = [change](bool v) { change([v](auto& x) { x.enabled = v; }); };
    if (!a.enabled) return;

    auto speed = row(g, "", "Length", seconds(a.speed));
    speed.subtitle->setVisible(true);
    auto* sl = speed.row->add<Slider>();
    sl->fixedWidth = 220;
    sl->setRange(0.5f, 20)->setStep(0.5f / 19.5f);
    sl->setValue(static_cast<float>(a.speed), false);
    auto* length = speed.subtitle;
    sl->onChanged = [length](float v) { length->setText(seconds(std::round(v * 2) / 2)); };
    sl->onCommitted = [change](float v) { change([v](auto& x) { x.speed = std::round(v * 2) / 2; }); };

    auto curve = row(g, "", "Curve");
    std::vector<std::pair<std::string, std::string>> curves;
    for (const auto& c : HyprOptions::get().curves()) curves.push_back({c.name, c.name});
    dropdown(curve.row, g, a.curve.empty() ? "default" : a.curve, curves,
             [change](const std::string& v) { change([v](auto& x) { x.curve = v; }); },
             [](Node* item, const std::string& v) { return item->add<CurveThumb>(curveOf(v), 28); });

    if (auto styles = stylesFor(leaf); !styles.empty()) {
        auto style = row(g, "", "Style");
        std::vector<std::pair<std::string, std::string>> items;
        for (const auto& st : styles) items.push_back({st, st.empty() ? "Hyprland's default" : st});
        dropdown(style.row, g, a.style.empty() ? "Hyprland's default" : a.style, items,
                 [change](const std::string& v) { change([v](auto& x) { x.style = v; }); });
    }

    auto* preview = g->add<Flex>(Flex::Direction::Column);
    preview->setPadding({20, 8, 20, 16})->setAlignItems(Align::Stretch);
    const double speedNow = a.speed;
    preview->add<CurvePreview>(std::make_shared<Bezier>(curveOf(a.curve)), [speedNow] { return speedNow / 10; });
}

// A curve's editor: the graph, presets, and the curve in motion
void curveEditor(Node* g, const HyprOptions::Curve& c) {
    auto* box = g->add<Flex>(Flex::Direction::Column);
    box->setPadding({20, 8, 20, 16})->setSpacing(12)->setAlignItems(Align::Stretch);
    auto points = std::make_shared<Bezier>(Bezier{c.x0, c.y0, c.x1, c.y1});
    auto* values = box->add<Label>();
    values->setSize(13)->setColor("on_surface_variant", false);
    auto show = [values, points] {
        const auto& p = *points;
        values->setText(std::format("cubic-bezier({}, {}, {}, {})", numberText(p[0], false), numberText(p[1], false),
                                    numberText(p[2], false), numberText(p[3], false)));
    };
    show();
    auto* editor = box->add<CurveEditor>(points);
    const std::string name = c.name;
    editor->onChanged = show;
    editor->onCommitted = [name, points] {
        const auto& p = *points;
        HyprOptions::get().setCurve(name, p[0], p[1], p[2], p[3]);
    };
    box->add<CurvePreview>(points, [] { return 0.6; });
    // Starting points: Material's motion curves and the classics
    static const std::vector<std::pair<const char*, Bezier>> presets = {
        {"Standard", {0.2, 0, 0, 1}},          {"Emphasized in", {0.05, 0.7, 0.1, 1}},
        {"Emphasized out", {0.3, 0, 0.8, 0.15}}, {"Overshoot", {0.34, 1.56, 0.64, 1}},
        {"Ease", {0.25, 0.1, 0.25, 1}},        {"Ease out", {0, 0, 0.58, 1}},
        {"Ease in and out", {0.42, 0, 0.58, 1}}, {"Linear", {0, 0, 1, 1}},
    };
    for (size_t start = 0; start < presets.size(); start += 4) {
        auto* line = box->add<Flex>(Flex::Direction::Row);
        line->setSpacing(8);
        for (size_t i = start; i < std::min(presets.size(), start + 4); ++i) {
            const auto [label, p] = presets[i];
            auto* b = line->add<Button>(Flex::Direction::Row);
            pill(b->setBackground("secondary_container", false))->setPadding({12, 8})->setSpacing(8);
            b->add<CurveThumb>(p, 22);
            b->add<Label>(label)->setSize(13)->setWeight(500)->setColor("on_secondary_container", false);
            b->onClicked = [name, p = p](uint32_t button) {
                if (button == BTN_LEFT) HyprOptions::get().setCurve(name, p[0], p[1], p[2], p[3]);
            };
        }
    }
}

void animationSections(Flex* page) {
    if (!HyprOptions::get().loaded()) HyprOptions::get().load();
    auto openLeaf = std::make_shared<std::string>();
    auto openCurve = std::make_shared<std::string>();
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live, openLeaf, openCurve] {
        live->clearChildren();
        auto& h = HyprOptions::get();
        if (h.animations().empty()) return;
        for (const auto& family : kFamilies) {
            auto* g = group(live, family.title);
            for (const auto& leaf : family.leaves) {
                const auto* a = h.animation(leaf);
                if (!a) continue;
                auto* r = g->add<Button>(Flex::Direction::Row);
                r->setPadding({20, 12})->setSpacing(16);
                r->minHeight = 60;
                auto* texts = r->add<Flex>(Flex::Direction::Column);
                texts->setAlignItems(Align::Start)->setSpacing(1);
                texts->grow = true;
                texts->add<Label>(leafName(leaf))->setSize(15)->setWeight(500);
                std::string summary = !a->enabled ? "Off" : seconds(a->speed) + " · " + (a->curve.empty() ? "default" : a->curve);
                if (a->enabled && !a->style.empty()) summary += " · " + a->style;
                if (!a->overridden && !a->parent.empty()) summary = "Same as " + leafName(a->parent) + " · " + summary;
                texts->add<Label>(summary)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(380);
                if (a->enabled) r->add<CurveThumb>(curveOf(a->curve), 36);
                if (a->custom) iconButton(r, "restart_alt", [leaf] { HyprOptions::get().resetAnimation(leaf); });
                r->add<Icon>(*openLeaf == leaf ? "expand_less" : "expand_more", 22)->setColor("on_surface_variant", false);
                r->onClicked = [live, openLeaf, leaf](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    *openLeaf = *openLeaf == leaf ? "" : leaf;
                    live->syncSoon();
                };
                if (*openLeaf == leaf) leafEditor(g, *a);
            }
        }

        auto* g = group(live, "Curves", "How each animation speeds up and slows down. Drag the two points; overshoot bounces");
        for (const auto& c : h.curves()) {
            auto* r = g->add<Button>(Flex::Direction::Row);
            r->setPadding({20, 12})->setSpacing(16);
            r->minHeight = 60;
            r->add<CurveThumb>(Bezier{c.x0, c.y0, c.x1, c.y1}, 36);
            auto* texts = r->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(c.name)->setSize(15)->setWeight(500);
            texts->add<Label>(std::format("{}, {}, {}, {}", numberText(c.x0, false), numberText(c.y0, false),
                                          numberText(c.x1, false), numberText(c.y1, false)))
                ->setSize(13)
                ->setColor("on_surface_variant", false);
            const std::string name = c.name;
            if (c.custom) confirmButton(r, "curve:" + name, "delete", [name] { HyprOptions::get().removeCurve(name); });
            r->add<Icon>(*openCurve == name ? "expand_less" : "expand_more", 22)->setColor("on_surface_variant", false);
            r->onClicked = [live, openCurve, name](uint32_t button) {
                if (button != BTN_LEFT) return;
                *openCurve = *openCurve == name ? "" : name;
                live->syncSoon();
            };
            if (*openCurve == name) curveEditor(g, c);
        }
        // A new curve, by name
        auto add = row(g, "add", "New curve", "Name it, then shape it");
        auto* f = add.row->add<TextField>();
        f->setPlaceholder("myCurve");
        f->fixedWidth = 180;
        f->onSubmit = [live, openCurve](const std::string& text) {
            std::string name;
            for (char ch : text)
                if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') name += ch;
            if (name.empty()) return;
            if (HyprOptions::get().setCurve(name, 0.2, 0, 0, 1).empty()) {
                *openCurve = name;
                live->syncSoon();
            }
        };
    };
    live->conns.push_back(HyprOptions::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}


// ── Displays ──

// A change that could leave the screen unusable waits to be kept: it
// reverts by itself unless confirmed within the countdown
struct Pending {
    std::string key;
    nlohmann::json before, after; // the fields, as they were and as asked
    EventLoop::TimerId timer = 0;
    int left = 0;
    Signal<> changed;
};
Pending& pending() {
    static Pending p;
    return p;
}

void settle(bool keep) {
    auto& p = pending();
    if (p.key.empty()) return;
    if (p.timer) EventLoop::get().cancel(p.timer);
    p.timer = 0;
    if (keep) HyprOptions::get().keepMonitor(p.key, p.after);
    else HyprOptions::get().applyMonitor(p.key, p.before);
    p.key.clear();
    p.changed.emit();
}

void tick() {
    auto& p = pending();
    if (p.key.empty()) return;
    if (--p.left <= 0) return settle(false);
    p.timer = EventLoop::get().addTimer(1000, tick);
    p.changed.emit();
}

// The fields a monitor has now, for the ones a change touches
nlohmann::json current(const HyprOptions::Monitor& m, const nlohmann::json& fields) {
    nlohmann::json out = nlohmann::json::object();
    for (auto& [k, v] : fields.items()) {
        if (k == "mode") out[k] = std::format("{}x{}@{:.2f}", m.width, m.height, m.refresh);
        else if (k == "position") out[k] = std::format("{}x{}", m.x, m.y);
        else if (k == "scale") out[k] = m.scale;
        else if (k == "transform") out[k] = m.transform;
        else if (k == "bitdepth") out[k] = m.bitdepth;
        else if (k == "cm") out[k] = m.cm;
        else if (k == "mirror") out[k] = m.mirrorOf;
        else if (k == "disabled") out[k] = m.disabled;
        else out[k] = m.kept.contains(k) ? m.kept[k] : v;
    }
    return out;
}

// Applies; risky changes start the countdown, the rest are kept at once
void changeMonitor(const HyprOptions::Monitor& m, const nlohmann::json& fields, bool risky) {
    auto& p = pending();
    if (!p.key.empty() && p.key != m.key) settle(false);
    const nlohmann::json before = current(m, fields);
    if (!HyprOptions::get().applyMonitor(m.key, fields).empty()) return;
    if (!risky) return HyprOptions::get().keepMonitor(m.key, fields);
    if (p.key.empty()) {
        p.key = m.key;
        p.before = before;
        p.after = fields;
    } else {
        // Another change in the same countdown: revert to the first state
        for (auto& [k, v] : before.items())
            if (!p.before.contains(k)) p.before[k] = v;
        for (auto& [k, v] : fields.items()) p.after[k] = v;
    }
    if (p.timer) EventLoop::get().cancel(p.timer);
    p.left = 15;
    p.timer = EventLoop::get().addTimer(1000, tick);
    p.changed.emit();
}

std::string modeLabel(const std::string& mode) {
    // "1920x1080@60.01Hz" → "1920 × 1080 · 60 Hz"
    const auto x = mode.find('x'), at = mode.find('@');
    if (x == std::string::npos || at == std::string::npos) return mode;
    const double hz = std::atof(mode.substr(at + 1).c_str());
    return mode.substr(0, x) + " × " + mode.substr(x + 1, at - x - 1) + " · " + std::format("{:.0f} Hz", hz);
}

void monitorSections(Flex* page) {
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        const auto monitors = HyprOptions::get().monitors();
        if (monitors.empty()) {
            row(group(live, "Displays"), "info", "No displays found", "These settings need a running Hyprland");
            return;
        }

        // Waiting to be kept: the countdown, first
        if (auto& p = pending(); !p.key.empty()) {
            auto* g = group(live, "Keep these display settings?");
            auto r = row(g, "timer", std::format("Going back in {} s", p.left), "If the screen looks wrong, wait or press Revert");
            pillButton(r.row, "Revert", false, [] { settle(false); }, "undo");
            pillButton(r.row, "Keep", true, [] { settle(true); }, "check");
        }

        // The arrangement, to scale
        std::vector<MonitorLayout::Screen> screens;
        for (const auto& m : monitors) {
            if (m.disabled) continue;
            const bool turned = m.transform % 2 == 1;
            const float w = static_cast<float>(turned ? m.height : m.width) / static_cast<float>(m.scale);
            const float h = static_cast<float>(turned ? m.width : m.height) / static_cast<float>(m.scale);
            screens.push_back({m.key, m.name, std::format("{} × {}", m.width, m.height), static_cast<float>(m.x),
                               static_cast<float>(m.y), w, h, m.focused});
        }
        auto* arrangement = group(live, "Arrangement", monitors.size() > 1 ? "Drag a display to place it beside the others" : "");
        auto* holder = arrangement->add<Flex>(Flex::Direction::Column);
        holder->setPadding(Insets(12))->setAlignItems(Align::Stretch);
        auto* layout = holder->add<MonitorLayout>(screens);
        layout->onMoved = [monitors](const std::string& key, int x, int y) {
            for (const auto& m : monitors)
                if (m.key == key) changeMonitor(m, {{"position", std::format("{}x{}", x, y)}}, true);
        };

        for (const auto& m : monitors) {
            const bool builtIn = m.name.starts_with("eDP") || m.name.starts_with("LVDS") || m.name.starts_with("DSI");
            auto* g = group(live, builtIn ? "Built-in display · " + m.name : m.description.empty() ? m.name : m.description,
                            builtIn ? m.description : m.name);

            auto on = row(g, "monitor", "Use this display");
            auto* sw = on.row->add<Switch>();
            sw->setChecked(!m.disabled, false);
            const size_t enabled = static_cast<size_t>(std::ranges::count_if(monitors, [](const auto& x) { return !x.disabled; }));
            sw->onToggled = [m, enabled, sw](bool v) {
                // The last display stays on: nothing could turn it back
                if (!v && enabled <= 1) return sw->setChecked(true);
                changeMonitor(m, {{"disabled", !v}}, true);
            };
            if (m.disabled) continue;

            // Resolution and refresh rate
            auto res = row(g, "aspect_ratio", "Resolution and refresh rate");
            std::vector<std::pair<std::string, std::string>> modes = {
                {"preferred", "Preferred"}, {"highrr", "Highest refresh rate"}, {"highres", "Highest resolution"}};
            for (const auto& mode : m.modes) modes.push_back({mode.substr(0, mode.size() - 2), modeLabel(mode)});
            dropdown(res.row, g, std::format("{} × {} · {:.0f} Hz", m.width, m.height, m.refresh), modes,
                     [m](const std::string& v) { changeMonitor(m, {{"mode", v}}, true); });

            // Scale
            auto sc = row(g, "zoom_out_map", "Scale", "Bigger text and controls at higher values");
            std::vector<std::pair<std::string, std::string>> scales;
            for (double v : {1.0, 1.25, 4.0 / 3, 1.5, 1.6, 5.0 / 3, 1.75, 2.0, 2.5, 3.0})
                scales.push_back({std::format("{:.6f}", v), std::format("{:.0f}%", v * 100)});
            dropdown(sc.row, g, std::format("{:.0f}%", m.scale * 100), scales,
                     [m](const std::string& v) { changeMonitor(m, {{"scale", std::atof(v.c_str())}}, true); });

            // Rotation, and mirrored
            auto rot = row(g, "screen_rotation", "Rotation");
            connectedGroup<int>(rot.row, m.transform % 4, {{0, "Normal"}, {1, "90°"}, {2, "180°"}, {3, "270°"}},
                                [m](int v) { changeMonitor(m, {{"transform", v + (m.transform >= 4 ? 4 : 0)}}, true); });
            auto flip = row(g, "flip", "Flipped", "Mirror the picture left to right");
            auto* flipSw = flip.row->add<Switch>();
            flipSw->setChecked(m.transform >= 4, false);
            flipSw->onToggled = [m](bool v) { changeMonitor(m, {{"transform", m.transform % 4 + (v ? 4 : 0)}}, true); };

            // Variable refresh rate
            auto vrr = row(g, "speed", "Variable refresh rate", m.vrr ? "Active now" : "Not active now");
            const int vrrNow = m.kept.contains("vrr") ? m.kept["vrr"].get<int>() : -1;
            static const std::vector<std::pair<std::string, std::string>> vrrModes = {
                {"-1", "Follow the default"}, {"0", "Off"}, {"1", "On"}, {"2", "Fullscreen only"}, {"3", "Fullscreen games"}};
            std::string vrrLabel = "Follow the default";
            for (const auto& [v, l] : vrrModes)
                if (std::atoi(v.c_str()) == vrrNow) vrrLabel = l;
            dropdown(vrr.row, g, vrrLabel, vrrModes, [m](const std::string& v) { changeMonitor(m, {{"vrr", std::atoi(v.c_str())}}, false); });

            // Colour: 10-bit, the colour mode, and SDR in HDR
            auto bits = row(g, "palette", "10-bit colour", "Smoother gradients, where the display and cable support it");
            auto* bitsSw = bits.row->add<Switch>();
            bitsSw->setChecked(m.bitdepth == 10, false);
            bitsSw->onToggled = [m](bool v) { changeMonitor(m, {{"bitdepth", v ? 10 : 8}}, true); };

            static const std::vector<std::pair<std::string, std::string>> cms = {
                {"auto", "Automatic"}, {"srgb", "sRGB (standard)"}, {"wide", "Wide gamut (BT.2020)"},
                {"edid", "As the display reports"}, {"hdr", "HDR"}, {"hdredid", "HDR, as the display reports"},
                {"dcip3", "DCI-P3"}, {"dp3", "Display P3"}, {"adobe", "Adobe RGB"}};
            std::string cmLabel = m.cm;
            for (const auto& [v, l] : cms)
                if (v == m.cm) cmLabel = l;
            auto cm = row(g, "hdr_strong", "Colour mode", "HDR needs a display that supports it");
            dropdown(cm.row, g, cmLabel, cms, [m](const std::string& v) { changeMonitor(m, {{"cm", v}}, true); });
            if (m.cm.starts_with("hdr")) {
                // Ordinary (SDR) apps on an HDR screen
                auto bright = row(g, "brightness_6", "Brightness of SDR apps", std::format("{:.0f}%", m.sdrBrightness * 100));
                auto* bs = bright.row->add<Slider>();
                bs->fixedWidth = 200;
                bs->setRange(0.5f, 2.0f)->setStep(0.05f / 1.5f);
                bs->setValue(static_cast<float>(m.sdrBrightness), false);
                auto* bsub = bright.subtitle;
                bs->onChanged = [bsub](float v) { bsub->setText(std::format("{:.0f}%", v * 100)); };
                bs->onCommitted = [m](float v) { changeMonitor(m, {{"sdrbrightness", std::round(v * 20) / 20}}, false); };
                auto sat = row(g, "invert_colors", "Saturation of SDR apps", std::format("{:.0f}%", m.sdrSaturation * 100));
                auto* ss = sat.row->add<Slider>();
                ss->fixedWidth = 200;
                ss->setRange(0.5f, 1.5f)->setStep(0.05f);
                ss->setValue(static_cast<float>(m.sdrSaturation), false);
                auto* ssub = sat.subtitle;
                ss->onChanged = [ssub](float v) { ssub->setText(std::format("{:.0f}%", v * 100)); };
                ss->onCommitted = [m](float v) { changeMonitor(m, {{"sdrsaturation", std::round(v * 20) / 20}}, false); };
            }

            // Mirroring another display
            if (monitors.size() > 1) {
                auto mir = row(g, "screen_share", "Mirror", "Show another display's picture here");
                std::vector<std::pair<std::string, std::string>> others = {{"", "Don't mirror"}};
                for (const auto& o : monitors)
                    if (o.key != m.key) others.push_back({o.name, o.name});
                dropdown(mir.row, g, m.mirrorOf.empty() ? "Don't mirror" : m.mirrorOf, others,
                         [m](const std::string& v) { changeMonitor(m, {{"mirror", v}}, true); });
            }

            if (!m.kept.empty()) {
                const std::string key = m.key;
                auto reset = row(g, "restart_alt", "Settings' changes for this display", "Back to what the config files set");
                pillButton(reset.row, "Forget", false, [key] { HyprOptions::get().forgetMonitor(key); }, "restart_alt");
            }
        }
    };
    live->conns.push_back(pending().changed.connect([live] { live->syncSoon(); }));
    live->conns.push_back(HyprOptions::get().changed.connect([live] { live->syncSoon(); }));
    // Plugged in or out, or rearranged: read them again (after Hyprland settles)
    live->conns.push_back(Hyprland::get().event.connect([live](const std::string& name, const std::string&) {
        if (name.starts_with("monitor") || name == "configreloaded")
            EventLoop::get().addTimer(300, [live] { live->syncSoon(); });
    }));
    live->sync();
}


// ── Startup apps and environment ──

// What a shipped module sets up, read from its file: lines like
// hl.exec_cmd("…") or hl.env("NAME", "value")
std::vector<std::vector<std::string>> shipped(const std::string& module, const std::regex& line) {
    std::vector<std::vector<std::string>> out;
    std::ifstream in(paths::home() + "/.config/hypr/material3ui/" + module);
    for (std::string l; std::getline(in, l);) {
        std::smatch m;
        if (l.find("--") != std::string::npos && l.find("--") < l.find("hl.")) continue; // commented out
        if (std::regex_search(l, m, line)) {
            std::vector<std::string> parts;
            for (size_t i = 1; i < m.size(); ++i) parts.push_back(m[i].str());
            out.push_back(parts);
        }
    }
    return out;
}

// An app's command for running it: the Exec line without %U and the like
std::string runnable(const std::string& exec) {
    return std::regex_replace(std::regex_replace(exec, std::regex(" ?%[a-zA-Z]"), ""), std::regex("\\s+$"), "");
}

void startupSections(Flex* page) {
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        auto& h = HyprOptions::get();
        auto list = h.startups();

        auto* mine = group(live, "Your startup apps", "Run once Hyprland has started, in this order");
        if (list.empty()) row(mine, "info", "None yet", "Add an app or a command below");
        for (size_t i = 0; i < list.size(); ++i) {
            const auto& st = list[i];
            auto r = row(mine, "rocket_launch", st.label.empty() ? st.command : st.label, st.label.empty() ? "" : st.command);
            iconButton(r.row, "play_arrow", [cmd = st.command] { spawn(cmd); });
            confirmButton(r.row, "startup:" + st.command, "delete", [i] {
                auto l = HyprOptions::get().startups();
                if (i < l.size()) l.erase(l.begin() + static_cast<long>(i));
                HyprOptions::get().setStartups(l);
            });
            auto* sw = r.row->add<Switch>();
            sw->setChecked(st.enabled, false);
            sw->onToggled = [i](bool on) {
                auto l = HyprOptions::get().startups();
                if (i < l.size()) l[i].enabled = on;
                HyprOptions::get().setStartups(l);
            };
        }

        // Add: an installed app, or any command
        auto add = row(mine, "add", "Add an app");
        std::vector<std::pair<std::string, std::string>> apps;
        for (const auto& a : Apps::get().all())
            if (!a.exec.empty()) apps.push_back({a.name + "\n" + runnable(a.exec), a.name});
        std::ranges::sort(apps, [](const auto& a, const auto& b) { return a.second < b.second; });
        dropdown(add.row, mine, "Choose", apps, [](const std::string& v) {
            const auto nl = v.find('\n');
            auto l = HyprOptions::get().startups();
            l.push_back({v.substr(nl + 1), v.substr(0, nl), true});
            HyprOptions::get().setStartups(l);
        });
        auto cmd = row(mine, "terminal", "Add a command", "Enter to add");
        auto* f = cmd.row->add<TextField>();
        f->setPlaceholder("e.g. nm-applet --indicator");
        f->fixedWidth = 280;
        f->onSubmit = [](const std::string& text) {
            if (text.empty()) return;
            auto l = HyprOptions::get().startups();
            l.push_back({text, "", true});
            HyprOptions::get().setStartups(l);
        };

        const auto builtIn = shipped("autostart.lua", std::regex("hl\\.exec_cmd\\(\"([^\"]+)\"\\)"));
        if (!builtIn.empty()) {
            auto* g = group(live, "Started by Material3UI", "From autostart.lua and shell.lua: the session's plumbing");
            for (const auto& b : builtIn) {
                auto r = row(g, "", b[0]);
                r.title->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(600);
            }
        }
    };
    live->conns.push_back(HyprOptions::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

void environmentSections(Flex* page) {
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        auto vars = HyprOptions::get().environment();
        auto* mine = group(live, "Your variables", "Apps started from now on see them; restart an app, or log in again, for the rest");
        if (vars.empty()) row(mine, "info", "None yet");
        for (size_t i = 0; i < vars.size(); ++i) {
            const auto& [k, v] = vars[i];
            auto r = row(mine, "data_object", k);
            auto* f = r.row->add<TextField>();
            f->setText(v);
            f->fixedWidth = 280;
            f->onSubmit = [i](const std::string& text) {
                auto l = HyprOptions::get().environment();
                if (i < l.size()) l[i].second = text;
                HyprOptions::get().setEnvironment(l);
            };
            confirmButton(r.row, "env:" + k, "delete", [i] {
                auto l = HyprOptions::get().environment();
                if (i < l.size()) l.erase(l.begin() + static_cast<long>(i));
                HyprOptions::get().setEnvironment(l);
            });
        }
        auto add = row(mine, "add", "Add a variable");
        auto* name = add.row->add<TextField>();
        name->setPlaceholder("NAME");
        name->fixedWidth = 150;
        auto* value = add.row->add<TextField>();
        value->setPlaceholder("value, then Enter");
        value->fixedWidth = 190;
        auto submit = [name, value](const std::string&) {
            std::string k;
            for (char c : name->text())
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') k += c;
            if (k.empty()) return;
            auto l = HyprOptions::get().environment();
            std::erase_if(l, [&](const auto& p) { return p.first == k; });
            l.push_back({k, value->text()});
            HyprOptions::get().setEnvironment(l);
        };
        name->onSubmit = submit;
        value->onSubmit = submit;

        const auto builtIn = shipped("env.lua", std::regex("hl\\.env\\(\"([^\"]+)\",\\s*\"([^\"]*)\"\\)"));
        if (!builtIn.empty()) {
            auto* g = group(live, "Set by Material3UI", "From env.lua: Wayland backends, portals and theming");
            for (const auto& b : builtIn) {
                auto r = row(g, "", b[0]);
                r.title->setSize(13)->setColor("on_surface_variant", false);
                r.row->add<Label>(b[1])->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(320);
            }
        }
    };
    live->conns.push_back(HyprOptions::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}


// ── Rules ──

// One setting a rule can carry, for the editor
struct Field {
    const char* key;
    const char* label;
    const char* group;
    enum Type { Bool, Int, Float, Text, Vec2 } type;
    const char* hint = "";
};

const std::vector<Field>& windowEffects() {
    static const std::vector<Field> f = {
        {"float", "Float", "Placement", Field::Bool},
        {"tile", "Tile", "Placement", Field::Bool},
        {"fullscreen", "Open fullscreen", "Placement", Field::Bool},
        {"maximize", "Open maximized", "Placement", Field::Bool},
        {"center", "Centre", "Placement", Field::Bool},
        {"pin", "Pin on every workspace", "Placement", Field::Bool},
        {"pseudo", "Pseudo-tile", "Placement", Field::Bool},
        {"workspace", "Open on workspace", "Placement", Field::Text, "3, name:web, special:magic; add \"silent\" to stay"},
        {"monitor", "Open on monitor", "Placement", Field::Text, "a name (DP-1) or number"},
        {"size", "Size", "Placement", Field::Vec2, "width height: pixels, % or monitor_w*0.5"},
        {"move", "Position", "Placement", Field::Vec2, "x y: pixels, % or expressions"},
        {"min_size", "Smallest size", "Placement", Field::Vec2},
        {"max_size", "Largest size", "Placement", Field::Vec2},
        {"persistent_size", "Remember its size", "Placement", Field::Bool},
        {"group", "Grouping", "Placement", Field::Text, "set, new, lock, barred, deny, invade, override, unset"},
        {"fullscreen_state", "Fullscreen state", "Placement", Field::Text, "internal client, e.g. 0 2"},
        {"opacity", "Opacity", "Look", Field::Text, "active [inactive [fullscreen]], e.g. 0.9 0.8"},
        {"rounding", "Corner radius", "Look", Field::Int},
        {"rounding_power", "Corner shape", "Look", Field::Float},
        {"border_size", "Border width", "Look", Field::Int},
        {"border_color", "Border colour", "Look", Field::Text, "rgba(RRGGBBAA)"},
        {"decorate", "Decorations (borders, shadow)", "Look", Field::Bool},
        {"no_blur", "No blur", "Look", Field::Bool},
        {"no_shadow", "No shadow", "Look", Field::Bool},
        {"no_dim", "Never dim", "Look", Field::Bool},
        {"dim_around", "Dim everything around it", "Look", Field::Bool},
        {"opaque", "Opaque", "Look", Field::Bool},
        {"xray", "X-ray blur", "Look", Field::Bool},
        {"animation", "Animation style", "Look", Field::Text, "e.g. popin 80%, slide"},
        {"no_anim", "No animations", "Look", Field::Bool},
        {"nearest_neighbor", "Sharp scaling (nearest neighbour)", "Look", Field::Bool},
        {"tonemap", "Tone mapping", "Look", Field::Text},
        {"no_initial_focus", "Don't focus when it opens", "Focus", Field::Bool},
        {"no_focus", "Never focus", "Focus", Field::Bool},
        {"stay_focused", "Keep the focus", "Focus", Field::Bool},
        {"focus_on_activate", "Focus when it asks", "Focus", Field::Bool},
        {"no_follow_mouse", "Ignore focus-follows-mouse", "Focus", Field::Bool},
        {"suppress_event", "Ignore its requests", "Focus", Field::Text, "fullscreen maximize activate activatefocus fullscreenoutput"},
        {"confine_pointer", "Keep the pointer inside", "Focus", Field::Bool},
        {"allows_input", "Accept input (XWayland)", "Focus", Field::Bool},
        {"no_shortcuts_inhibit", "Don't let it take shortcuts", "Focus", Field::Bool},
        {"idle_inhibit", "Keep the screen on", "Behaviour", Field::Text, "none, always, focus, fullscreen"},
        {"immediate", "Allow tearing (games)", "Behaviour", Field::Bool},
        {"no_vrr", "No variable refresh rate", "Behaviour", Field::Bool},
        {"render_unfocused", "Keep rendering when unfocused", "Behaviour", Field::Bool},
        {"no_screen_share", "Hide from screen sharing", "Behaviour", Field::Bool},
        {"content", "Content type", "Behaviour", Field::Text, "none, photo, video, game"},
        {"keep_aspect_ratio", "Keep its aspect ratio", "Behaviour", Field::Bool},
        {"no_max_size", "Ignore its largest size", "Behaviour", Field::Bool},
        {"force_rgbx", "Ignore its transparency", "Behaviour", Field::Bool},
        {"sync_fullscreen", "Fullscreen in step with the app", "Behaviour", Field::Bool},
        {"no_close_for", "Can't be closed for (ms)", "Behaviour", Field::Int},
        {"no_auto_hdr", "No automatic HDR", "Behaviour", Field::Bool},
        {"scroll_mouse", "Mouse scroll speed", "Behaviour", Field::Float},
        {"scroll_touchpad", "Touchpad scroll speed", "Behaviour", Field::Float},
        {"scrolling_width", "Column width (scrolling layout)", "Behaviour", Field::Float},
        {"tag", "Tag", "Behaviour", Field::Text, "+name adds, -name removes"},
    };
    return f;
}

const std::vector<Field>& layerEffects() {
    static const std::vector<Field> f = {
        {"blur", "Blur behind it", "Look", Field::Bool},
        {"blur_popups", "Blur its popups", "Look", Field::Bool},
        {"ignore_alpha", "Don't blur below opacity", "Look", Field::Float, "0–1"},
        {"xray", "X-ray blur", "Look", Field::Bool},
        {"dim_around", "Dim everything around it", "Look", Field::Bool},
        {"no_anim", "No animations", "Look", Field::Bool},
        {"animation", "Animation style", "Look", Field::Text, "slide, popin, fade"},
        {"order", "Stacking order", "Behaviour", Field::Int},
        {"above_lock", "Above the lock screen", "Behaviour", Field::Int, "0 no, 1 yes, 2 and interactive"},
        {"no_screen_share", "Hide from screen sharing", "Behaviour", Field::Bool},
    };
    return f;
}

const std::vector<Field>& workspaceFields() {
    static const std::vector<Field> f = {
        {"monitor", "On monitor", "Placement", Field::Text, "DP-1, desc:…"},
        {"default", "The monitor's first workspace", "Placement", Field::Bool},
        {"persistent", "Always exists", "Placement", Field::Bool},
        {"default_name", "Name", "Placement", Field::Text},
        {"on_created_empty", "When opened empty, run", "Placement", Field::Text, "a command"},
        {"layout", "Layout", "Look", Field::Text, "dwindle, master, scrolling, monocle"},
        {"gaps_in", "Gaps between windows", "Look", Field::Int},
        {"gaps_out", "Gaps at the edges", "Look", Field::Int},
        {"float_gaps", "Gaps for floating windows", "Look", Field::Int},
        {"border_size", "Border width", "Look", Field::Int},
        {"no_border", "No borders", "Look", Field::Bool},
        {"no_rounding", "No rounded corners", "Look", Field::Bool},
        {"decorate", "Decorations", "Look", Field::Bool},
        {"no_shadow", "No shadows", "Look", Field::Bool},
        {"animation", "Animation style", "Look", Field::Text},
    };
    return f;
}

// What a window or layer rule can test
const std::vector<std::pair<std::string, std::string>>& windowMatches() {
    static const std::vector<std::pair<std::string, std::string>> m = {
        {"class", "App (class)"}, {"title", "Title"}, {"initial_class", "First class"}, {"initial_title", "First title"},
        {"tag", "Tag"}, {"xwayland", "XWayland"}, {"float", "Floating"}, {"fullscreen", "Fullscreen"},
        {"pin", "Pinned"}, {"focus", "Focused"}, {"group", "In a group"}, {"modal", "A dialog (modal)"},
        {"workspace", "On workspace"}, {"content", "Content type"}, {"xdg_tag", "XDG tag"}};
    return m;
}

std::string newId() {
    static std::mt19937 rng{std::random_device{}()};
    return std::format("{:08x}", rng());
}

const Field* fieldOf(const std::vector<Field>& fields, const std::string& key) {
    for (const auto& f : fields)
        if (key == f.key) return &f;
    return nullptr;
}

std::string valueText(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_array() && v.size() == 2) {
        auto one = [](const nlohmann::json& x) { return x.is_string() ? x.get<std::string>() : x.dump(); };
        return one(v[0]) + " " + one(v[1]);
    }
    return v.dump();
}

// A rule's settings: each with its control and a remove, then "Add"
void specEditor(Node* g, const std::vector<Field>& fields, nlohmann::json values,
                std::function<void(const nlohmann::json&)> commit) {
    auto set = [values, commit](const std::string& key, const nlohmann::json& v) {
        auto next = values;
        if (v.is_null()) next.erase(key);
        else next[key] = v;
        commit(next);
    };
    for (auto& [key, value] : values.items()) {
        const Field* f = fieldOf(fields, key);
        auto r = row(g, "", f ? f->label : key, f && *f->hint ? f->hint : "");
        r.title->setMaxWidth(260);
        r.subtitle->setMaxWidth(260);
        const std::string k = key;
        const auto type = f ? f->type : Field::Text;
        if (type == Field::Bool) {
            auto* sw = r.row->add<Switch>();
            sw->setChecked(value.is_boolean() ? value.get<bool>() : true, false);
            sw->onToggled = [set, k](bool on) { set(k, on); };
        } else {
            auto* t = r.row->add<TextField>();
            t->setText(valueText(value));
            t->fixedWidth = type == Field::Vec2 ? 220 : 200;
            t->onSubmit = [set, k, type](const std::string& text) {
                try {
                    if (type == Field::Int) return set(k, std::stoll(text));
                    if (type == Field::Float) return set(k, std::stod(text));
                    if (type == Field::Vec2) {
                        std::istringstream in(text);
                        std::string a, b;
                        in >> a >> b;
                        if (b.empty()) return;
                        // Plain numbers as numbers; expressions (50%, monitor_w*0.5) as text
                        auto num = [](const std::string& x) -> nlohmann::json {
                            char* end = nullptr;
                            const double d = std::strtod(x.c_str(), &end);
                            return end && *end == 0 ? nlohmann::json(d) : nlohmann::json(x);
                        };
                        return set(k, nlohmann::json{num(a), num(b)});
                    }
                    set(k, text);
                } catch (...) {
                }
            };
        }
        iconButton(r.row, "close", [set, k] { set(k, nullptr); }, 18);
    }
    // Add a setting: the ones not set yet, by group
    auto add = row(g, "add", "Add a setting");
    std::vector<std::pair<std::string, std::string>> items;
    for (const auto& f : fields)
        if (!values.contains(f.key)) items.push_back({f.key, std::string(f.group) + " · " + f.label});
    dropdown(add.row, g, "Choose", items, [set, &fields](const std::string& key) {
        const Field* f = fieldOf(fields, key);
        const auto type = f ? f->type : Field::Text;
        set(key, type == Field::Bool    ? nlohmann::json(true)
                 : type == Field::Int   ? nlohmann::json(0)
                 : type == Field::Float ? nlohmann::json(1.0)
                 : type == Field::Vec2  ? nlohmann::json{800, 600}
                                        : nlohmann::json(""));
    });
}

// What a rule matches: property and pattern pairs (patterns are regexes)
void matchEditor(Node* g, bool layer, nlohmann::json match, std::function<void(const nlohmann::json&)> commit) {
    auto set = [match, commit](const std::string& key, const nlohmann::json& v) {
        auto next = match;
        if (v.is_null()) next.erase(key);
        else next[key] = v;
        commit(next);
    };
    for (auto& [key, value] : match.items()) {
        std::string label = key;
        for (const auto& [k, l] : windowMatches())
            if (k == key) label = l;
        if (key == "namespace") label = "Layer (namespace)";
        auto r = row(g, "filter_alt", label);
        auto* t = r.row->add<TextField>();
        t->setText(valueText(value));
        t->fixedWidth = 240;
        const std::string k = key;
        t->onSubmit = [set, k](const std::string& text) {
            // Booleans read as booleans: xwayland, float, fullscreen…
            if (text == "true" || text == "false") return set(k, text == "true");
            set(k, text);
        };
        iconButton(r.row, "close", [set, k] { set(k, nullptr); }, 18);
    }
    if (layer) {
        // The layers on screen now (bars, launchers, notifications)
        auto pick = row(g, "layers", "A layer on screen");
        std::vector<std::pair<std::string, std::string>> spaces;
        if (auto j = Hyprland::get().requestJson("layers"); j && j->is_object())
            for (auto& [mon, m] : j->items())
                for (const auto obj = m.value("levels", nlohmann::json::object()); auto& [lvl, list] : obj.items())
                    for (const auto& l : list) {
                        const std::string ns = l.value("namespace", "");
                        if (!ns.empty() && std::ranges::none_of(spaces, [&](const auto& p) { return p.first == ns; }))
                            spaces.push_back({ns, ns});
                    }
        dropdown(pick.row, g, "Choose", spaces, [set](const std::string& ns) { set("namespace", "^(" + ns + ")$"); });
        return;
    }
    // An open window: its app, exactly
    auto pick = row(g, "select_window", "From an open window");
    std::vector<std::pair<std::string, std::string>> wins;
    for (const auto& c : Hyprland::get().clients(true))
        if (!c.cls.empty() && std::ranges::none_of(wins, [&](const auto& p) { return p.first == c.cls; }))
            wins.push_back({c.cls, c.cls + " — " + c.title.substr(0, 40)});
    dropdown(pick.row, g, "Choose", wins, [set](const std::string& cls) {
        std::string escaped;
        for (char ch : cls) {
            if (std::string_view(".^$|()[]{}*+?\\").find(ch) != std::string_view::npos) escaped += '\\';
            escaped += ch;
        }
        set("class", "^(" + escaped + ")$");
    });
    auto add = row(g, "add", "Add a condition");
    std::vector<std::pair<std::string, std::string>> props;
    for (const auto& [k, l] : windowMatches())
        if (!match.contains(k)) props.push_back({k, l});
    dropdown(add.row, g, "Choose", props, [set](const std::string& k) {
        set(k, k == "xwayland" || k == "float" || k == "fullscreen" || k == "pin" || k == "focus" || k == "group" || k == "modal"
                   ? nlohmann::json(true)
                   : nlohmann::json(".*"));
    });
}

// One line saying what a rule does
std::string summary(const nlohmann::json& r, bool workspace) {
    std::string what;
    if (workspace) {
        what = "Workspace " + r.value("workspace", "?");
    } else {
        for (const auto obj = r.value("match", nlohmann::json::object()); auto& [k, v] : obj.items())
            what += (what.empty() ? "" : ", ") + k + " " + valueText(v);
        if (what.empty()) what = "Matches nothing yet";
    }
    std::string does;
    for (const auto obj = r.value(workspace ? "fields" : "effects", nlohmann::json::object()); auto& [k, v] : obj.items())
        does += (does.empty() ? "" : ", ") + (v.is_boolean() ? k : k + " " + valueText(v));
    return what + (does.empty() ? "" : "  →  " + does);
}

void ruleSections(Flex* page, HyprOptions::RuleKind kind) {
    using K = HyprOptions::RuleKind;
    auto open = std::make_shared<std::string>();
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live, open, kind] {
        live->clearChildren();
        auto& h = HyprOptions::get();
        const auto list = h.rules(kind);
        const bool ws = kind == K::Workspace;
        auto* g = group(live, ws ? "Your workspace rules" : kind == K::Window ? "Your window rules" : "Your layer rules",
                        ws ? "Where workspaces live and how they look"
                        : kind == K::Window ? "Apply to windows as they open; apps are matched by regular expressions"
                                            : "For bars, launchers and notifications (layers)");
        if (list.empty()) row(g, "info", "None yet", "Add one below");
        for (size_t i = 0; i < list.size(); ++i) {
            const auto r = list[i];
            const std::string id = r.value("id", "");
            auto* b = g->add<Button>(Flex::Direction::Row);
            b->setPadding({20, 12})->setSpacing(16);
            b->minHeight = 60;
            auto* texts = b->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(ws ? "Workspace " + r.value("workspace", "") : r.value("name", "Rule"))->setSize(15)->setWeight(500);
            texts->add<Label>(summary(r, ws))->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(420)->setMaxLines(2);
            auto* sw = b->add<Switch>();
            sw->setChecked(r.value("enabled", true), false);
            sw->onToggled = [kind, i](bool on) {
                auto l = HyprOptions::get().rules(kind);
                if (i < l.size()) l[i]["enabled"] = on;
                HyprOptions::get().setRules(kind, l);
            };
            confirmButton(b, "rule:" + id, "delete", [kind, i] {
                auto l = HyprOptions::get().rules(kind);
                if (i < l.size()) l.erase(l.begin() + static_cast<long>(i));
                HyprOptions::get().setRules(kind, l);
            });
            b->add<Icon>(*open == id ? "expand_less" : "expand_more", 22)->setColor("on_surface_variant", false);
            b->onClicked = [live, open, id](uint32_t button) {
                if (button != BTN_LEFT) return;
                *open = *open == id ? "" : id;
                live->syncSoon();
            };
            if (*open != id) continue;

            // The editor: its name or workspace, what it matches, what it does
            auto replace = [kind, i](const std::function<void(nlohmann::json&)>& edit) {
                auto l = HyprOptions::get().rules(kind);
                if (i >= l.size()) return;
                edit(l[i]);
                HyprOptions::get().setRules(kind, l);
            };
            auto name = row(g, "", ws ? "Workspace" : "Name", ws ? "1, name:web, special:magic" : "");
            auto* nf = name.row->add<TextField>();
            nf->setText(ws ? r.value("workspace", "") : r.value("name", ""));
            nf->fixedWidth = 240;
            nf->onSubmit = [replace, ws](const std::string& t) { replace([&](auto& x) { x[ws ? "workspace" : "name"] = t; }); };
            if (!ws) {
                auto* head = g->add<Flex>(Flex::Direction::Row);
                head->setPadding({20, 10, 20, 2});
                head->add<Label>("Applies to")->setSize(13)->setWeight(600)->setColor("primary", false);
                matchEditor(g, kind == K::Layer, r.value("match", nlohmann::json::object()),
                            [replace](const nlohmann::json& m) { replace([&](auto& x) { x["match"] = m; }); });
            }
            auto* head = g->add<Flex>(Flex::Direction::Row);
            head->setPadding({20, 10, 20, 2});
            head->add<Label>(ws ? "Settings" : "Does")->setSize(13)->setWeight(600)->setColor("primary", false);
            specEditor(g, ws ? workspaceFields() : kind == K::Window ? windowEffects() : layerEffects(),
                       r.value(ws ? "fields" : "effects", nlohmann::json::object()),
                       [replace, ws](const nlohmann::json& v) { replace([&](auto& x) { x[ws ? "fields" : "effects"] = v; }); });
        }
        auto add = row(g, "add", ws ? "Add a workspace rule" : "Add a rule");
        pillButton(add.row, "Add", true, [live, open, kind, ws] {
            auto l = HyprOptions::get().rules(kind);
            const std::string id = newId();
            nlohmann::json r = {{"id", id}, {"enabled", true}};
            if (ws) {
                r["workspace"] = "1";
                r["fields"] = nlohmann::json::object();
            } else {
                r["name"] = "New rule";
                r["match"] = nlohmann::json::object();
                r["effects"] = nlohmann::json::object();
            }
            l.push_back(r);
            HyprOptions::get().setRules(kind, l);
            *open = id;
            live->syncSoon();
        }, "add");
    };
    live->conns.push_back(HyprOptions::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}


// ── Cursor theme, profiles and plugins ──

// Installed cursor themes: Xcursor ("cursors") or hyprcursor ("manifest.hl")
std::vector<std::pair<std::string, std::string>> cursorThemes() {
    std::vector<std::pair<std::string, std::string>> out;
    const std::string home = paths::home();
    for (const std::string dir : {home + "/.local/share/icons", home + "/.icons", std::string("/usr/share/icons")}) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            const auto p = e.path();
            const bool x = std::filesystem::is_directory(p / "cursors", ec), hc = std::filesystem::exists(p / "manifest.hl", ec);
            const std::string id = p.filename().string();
            if ((!x && !hc) || id == "default" || std::ranges::any_of(out, [&](const auto& t) { return t.first == id; })) continue;
            // The theme's own name, from index.theme or the manifest
            std::string name = id;
            for (const char* f : {"index.theme", "manifest.hl"}) {
                std::ifstream in(p / f);
                for (std::string l; std::getline(in, l);)
                    if (l.starts_with("Name") && l.find('=') != std::string::npos) {
                        name = l.substr(l.find('=') + 1);
                        while (!name.empty() && name.front() == ' ') name.erase(0, 1);
                        break;
                    }
            }
            out.push_back({id, name + (hc ? " (hyprcursor)" : "")});
        }
    }
    std::ranges::sort(out, [](const auto& a, const auto& b) { return a.second < b.second; });
    return out;
}

void cursorThemeSection(Flex* page) {
    // Rebuilt with the config: the size is also set in Accessibility
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        auto* g = group(live, "Cursor theme", "Applies at once: point at things to see it");
        const std::string now = Config::get().value<std::string>("/accessibility/cursorTheme", "");
        auto theme = row(g, "arrow_selector_tool", "Theme");
        const auto themes = cursorThemes();
        std::string shown = now.empty() ? "System default" : now;
        for (const auto& [id, name] : themes)
            if (id == now) shown = name;
        std::vector<std::pair<std::string, std::string>> items = {{"", "System default"}};
        items.insert(items.end(), themes.begin(), themes.end());
        dropdown(theme.row, g, shown, items, [](const std::string& id) {
            Config::get().set("/accessibility/cursorTheme", id.empty() ? nlohmann::json(nullptr) : nlohmann::json(id));
        });
        auto size = row(g, "open_in_full", "Size", "Also in Accessibility");
        connectedGroup<int>(size.row, Config::get().value<int>("/accessibility/cursorSize", 24),
                            {{24, "Default"}, {32, "Large"}, {48, "Larger"}, {64, "Largest"}},
                            [](int v) { Config::get().set("/accessibility/cursorSize", v); });
    };
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

void profileSections(Flex* page) {
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    live->sync = [live] {
        live->clearChildren();
        auto& h = HyprOptions::get();
        const auto list = h.profiles();
        auto* g = group(live, "Profiles", "Everything Hyprland Settings keeps (options, animations, displays, rules, startup apps and variables), under a name");
        if (list.empty()) row(g, "info", "No profiles yet", "Save the current settings below; a keybind can switch them: material3ui msg hypr profile next");
        for (const auto& p : list) {
            const bool active = p == h.activeProfile();
            auto r = row(g, active ? "radio_button_checked" : "radio_button_unchecked", p, active ? "In use" : "");
            if (active) static_cast<Icon*>(r.row->children()[0].get())->setColor("primary", false);
            if (!active) pillButton(r.row, "Apply", true, [p] { HyprOptions::get().applyProfile(p); }, "check");
            else pillButton(r.row, "Save over", false, [p] { HyprOptions::get().saveProfile(p); }, "save");
            confirmButton(r.row, "profile:" + p, "delete", [p] { HyprOptions::get().deleteProfile(p); });
        }
        auto save = row(g, "add", "Save the current settings", "Enter a name, then Enter");
        auto* f = save.row->add<TextField>();
        f->setPlaceholder("e.g. Gaming");
        f->fixedWidth = 220;
        f->onSubmit = [](const std::string& text) {
            std::string name;
            for (char c : text)
                if (c != '/' && c != '\\') name += c;
            if (!name.empty()) HyprOptions::get().saveProfile(name);
        };
        auto folder = row(g, "folder_open", "Profiles folder", HyprOptions::profilesDir());
        pillButton(folder.row, "Open", false, [] {
            std::error_code ec;
            std::filesystem::create_directories(HyprOptions::profilesDir(), ec);
            spawn("xdg-open '" + HyprOptions::profilesDir() + "'");
        }, "open_in_new");
    };
    live->conns.push_back(HyprOptions::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

// A command in a terminal, kept open to read its output
void inTerminal(const std::string& command) {
    spawn("${TERMINAL:-kitty} -e sh -c '" + command + "; echo; read -p \"Press Enter to close\" _'");
}

void pluginSections(Flex* page) {
    auto* live = page->add<kit::Live>();
    live->setSpacing(6);
    auto hyprpm = std::make_shared<std::string>("…"); // `hyprpm list`, once read
    live->sync = [live, hyprpm] {
        live->clearChildren();
        // Loaded now
        auto* loaded = group(live, "Loaded plugins", "Their options appear under Behaviour → Other once loaded");
        const std::string list = Hyprland::get().request("plugin list");
        std::istringstream in(list);
        bool any = false;
        for (std::string l; std::getline(in, l);)
            if (l.starts_with("Plugin ")) {
                any = true;
                row(loaded, "extension", l.substr(7, l.find(" by ") == std::string::npos ? std::string::npos : l.find(" by ") - 7));
            }
        if (!any) row(loaded, "extension_off", "No plugins loaded");

        // Plugins to get: well-known ones, built and enabled by hyprpm
        const bool haveHyprpm = access("/usr/bin/hyprpm", X_OK) == 0 || access("/usr/local/bin/hyprpm", X_OK) == 0;
        struct Known {
            const char *name, *about, *repo;
        };
        static const std::vector<Known> known = {
            {"hyprexpo", "An overview of every workspace, in a grid", "https://github.com/hyprwm/hyprland-plugins"},
            {"hyprbars", "Title bars with buttons on windows", "https://github.com/hyprwm/hyprland-plugins"},
            {"hyprtrails", "Smooth trails behind moving windows", "https://github.com/hyprwm/hyprland-plugins"},
            {"hyprwinwrap", "Any app as the wallpaper", "https://github.com/hyprwm/hyprland-plugins"},
            {"borders-plus-plus", "Extra borders around windows", "https://github.com/hyprwm/hyprland-plugins"},
            {"csgo-vulkan-fix", "Stretched resolutions for Source games", "https://github.com/hyprwm/hyprland-plugins"},
            {"xtra-dispatchers", "More actions for keybinds", "https://github.com/hyprwm/hyprland-plugins"},
            {"dynamic-cursors", "A cursor that tilts, stretches and shakes to grow", "https://github.com/VirtCode/hypr-dynamic-cursors"},
            {"hyprgrass", "Touchscreen gestures", "https://github.com/horriblename/hyprgrass"},
            {"hyprsplit", "Separate workspaces on each monitor", "https://github.com/shezdy/hyprsplit"},
        };
        auto* get = group(live, "Get plugins",
                          "Downloaded and built for your Hyprland in a terminal (the first one fetches Hyprland's headers and asks for your password)");
        if (!haveHyprpm) {
            auto r = row(get, "download", "Install the plugin manager first", "hyprpm, and the tools it builds plugins with");
            pillButton(r.row, "Install", true, [] {
                Packages::get().runAsRoot("Installing hyprpm…", "pacman -S --needed --noconfirm hyprpm cmake meson cpio pkgconf git gcc",
                                          "hyprpm installed: plugins can be added now");
            }, "download");
        }
        for (const auto& k : known) {
            const bool installed = hyprpm->find(std::string("Plugin ") + k.name) != std::string::npos;
            auto r = row(get, "extension", k.name, std::string(k.about) + "  ·  " + std::string(k.repo).substr(19));
            if (installed) {
                r.row->template add<Label>("Installed")->setSize(13)->setWeight(500)->setColor("primary", false);
            } else if (haveHyprpm) {
                const std::string repo = k.repo, name = k.name;
                pillButton(r.row, "Install", false, [repo, name] {
                    // Headers for this Hyprland, the repository (once), then the plugin on
                    inTerminal("hyprpm update && { hyprpm list | grep -q " + repo + " || hyprpm add " + repo +
                               "; } && hyprpm enable " + name + " && hyprpm reload -n");
                }, "download");
            }
        }
        if (haveHyprpm) {
            auto refresh = row(get, "refresh", "Installed something?", "Read the plugins again");
            pillButton(refresh.row, "Refresh", false, [hyprpm, live] {
                run({"hyprpm", "list"}, [hyprpm, live](int, const std::string& out) {
                    *hyprpm = out;
                    live->syncSoon();
                });
            }, "refresh");
        }

        // hyprpm: repositories and their plugins
        auto* pm = group(live, "Plugin manager (hyprpm)", "Builds plugins for your Hyprland version");
        if (!haveHyprpm) {
            row(pm, "info", "hyprpm isn't installed", "Install it above to add plugins from their repositories");
        } else if (*hyprpm == "…") {
            row(pm, "hourglass_empty", "Reading plugins…");
        } else {
            // "Repository name (url):" then "│ Plugin name" "└─ enabled: true"
            std::istringstream lines(*hyprpm);
            std::string plugin;
            bool listed = false;
            for (std::string l; std::getline(lines, l);) {
                const std::string clean = std::regex_replace(l, std::regex("\x1b\\[[0-9;]*m"), "");
                std::smatch m;
                if (std::regex_search(clean, m, std::regex("Repository (\\S+)"))) {
                    auto* head = pm->add<Flex>(Flex::Direction::Row);
                    head->setPadding({20, 10, 20, 2});
                    head->add<Label>(m[1].str())->setSize(13)->setWeight(600)->setColor("primary", false);
                } else if (std::regex_search(clean, m, std::regex("Plugin (\\S+)"))) {
                    plugin = m[1].str();
                } else if (std::regex_search(clean, m, std::regex("enabled: (true|false)")) && !plugin.empty()) {
                    listed = true;
                    auto r = row(pm, "extension", plugin);
                    auto* sw = r.row->add<Switch>();
                    sw->setChecked(m[1].str() == "true", false);
                    const std::string name = plugin;
                    sw->onToggled = [name, hyprpm, live](bool on) {
                        run({"sh", "-c", std::format("hyprpm {} '{}' && hyprpm reload -n", on ? "enable" : "disable", name)},
                            [hyprpm, live](int, const std::string&) {
                                *hyprpm = "…";
                                live->syncSoon();
                            });
                    };
                    plugin.clear();
                }
            }
            if (!listed) row(pm, "info", "No plugins added yet");
            auto add = row(pm, "add", "Add a repository", "A plugin repository's git URL; builds in a terminal");
            auto* f = add.row->add<TextField>();
            f->setPlaceholder("https://github.com/hyprwm/hyprland-plugins");
            f->fixedWidth = 300;
            f->onSubmit = [](const std::string& url) {
                if (!url.empty()) inTerminal("hyprpm add " + url);
            };
            auto up = row(pm, "update", "Update plugins", "Rebuilds them for the running Hyprland");
            pillButton(up.row, "Update", false, [] { inTerminal("hyprpm update"); }, "refresh");
        }

        // Any plugin file, now and at every start
        auto* file = group(live, "Load a plugin file", "A .so you built yourself");
        auto r = row(file, "upload_file", "Path to the .so", "Enter loads it now; the switch keeps it for every start");
        auto* f = r.row->add<TextField>();
        f->setPlaceholder("/path/to/plugin.so");
        f->fixedWidth = 280;
        f->onSubmit = [live](const std::string& path) {
            if (path.empty()) return;
            Hyprland::get().request("plugin load " + path);
            // And at every start, through the startup apps
            auto l = HyprOptions::get().startups();
            const std::string cmd = "hyprctl plugin load '" + path + "'";
            if (std::ranges::none_of(l, [&](const auto& s) { return s.command == cmd; })) l.push_back({cmd, "Plugin: " + path, true});
            HyprOptions::get().setStartups(l);
            live->syncSoon();
        };
    };
    if (access("/usr/bin/hyprpm", X_OK) == 0 || access("/usr/local/bin/hyprpm", X_OK) == 0)
        run({"hyprpm", "list"}, [hyprpm, live](int, const std::string& out) {
            *hyprpm = out;
            live->syncSoon();
        });
    live->sync();
}

} // namespace

void hyprProfilesPage(Flex* page, Popup&) {
    profileSections(page);
}

void hyprPluginsPage(Flex* page, Popup&) {
    pluginSections(page);
}

void hyprWindowRulesPage(Flex* page, Popup&) {
    ruleSections(page, HyprOptions::RuleKind::Window);
}

void hyprLayerRulesPage(Flex* page, Popup&) {
    ruleSections(page, HyprOptions::RuleKind::Layer);
}

void hyprWorkspaceRulesPage(Flex* page, Popup&) {
    ruleSections(page, HyprOptions::RuleKind::Workspace);
}

void hyprStartupPage(Flex* page, Popup&) {
    startupSections(page);
}

void hyprEnvironmentPage(Flex* page, Popup&) {
    environmentSections(page);
}

void hyprMonitorsPage(Flex* page, Popup&) {
    monitorSections(page);
}

void hyprAnimationsPage(Flex* page, Popup&) {
    buildPage(page, "hypr-animations");
    animationSections(page);
}

void hyprWindowsPage(Flex* page, Popup&) { buildPage(page, "hypr-windows"); }
void hyprDecorationPage(Flex* page, Popup&) { buildPage(page, "hypr-decoration"); }
void hyprInputPage(Flex* page, Popup&) { buildPage(page, "hypr-input"); }
void hyprCursorPage(Flex* page, Popup&) {
    cursorThemeSection(page);
    buildPage(page, "hypr-cursor");
}
void hyprGesturesPage(Flex* page, Popup&) { buildPage(page, "hypr-gestures"); }
void hyprLayoutsPage(Flex* page, Popup&) { buildPage(page, "hypr-layouts"); }
void hyprGroupsPage(Flex* page, Popup&) { buildPage(page, "hypr-groups"); }
void hyprMiscPage(Flex* page, Popup&) { buildPage(page, "hypr-misc"); }
void hyprRenderPage(Flex* page, Popup&) { buildPage(page, "hypr-render"); }
void hyprDebugPage(Flex* page, Popup&) { buildPage(page, "hypr-debug"); }

} // namespace m3::shell::panels
