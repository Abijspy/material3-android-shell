// Settings → Hyprland: every compositor option, from what the running
// Hyprland describes (services/HyprOptions). The catalogue below only
// groups and names them; anything it doesn't place lands in "Other", so new
// Hyprland options show up without a change here.
#include "shell/HyprPages.h"

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "services/HyprOptions.h"
#include "services/Hyprland.h"
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
#include <sstream>

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

} // namespace

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
void hyprCursorPage(Flex* page, Popup&) { buildPage(page, "hypr-cursor"); }
void hyprGesturesPage(Flex* page, Popup&) { buildPage(page, "hypr-gestures"); }
void hyprLayoutsPage(Flex* page, Popup&) { buildPage(page, "hypr-layouts"); }
void hyprGroupsPage(Flex* page, Popup&) { buildPage(page, "hypr-groups"); }
void hyprMiscPage(Flex* page, Popup&) { buildPage(page, "hypr-misc"); }
void hyprRenderPage(Flex* page, Popup&) { buildPage(page, "hypr-render"); }
void hyprDebugPage(Flex* page, Popup&) { buildPage(page, "hypr-debug"); }

} // namespace m3::shell::panels
