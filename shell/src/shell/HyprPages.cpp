// Settings → Hyprland: every compositor option, from what the running
// Hyprland describes (services/HyprOptions). The catalogue below only
// groups and names them; anything it doesn't place lands in "Other", so new
// Hyprland options show up without a change here.
#include "shell/HyprPages.h"

#include "core/EventLoop.h"
#include "services/HyprOptions.h"
#include "services/Hyprland.h"
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
          {"Animations", {"animations:"}},
          {"XWayland", {"xwayland:"}},
          {"Ecosystem", {"ecosystem:"}},
          {"Quirks", {"quirks:"}}}},
        {"hypr-render", {{"Rendering", {"render:"}}, {"OpenGL", {"opengl:"}}, {"Experimental", {"experimental:"}}}},
        {"hypr-debug", {{"Debug", {"debug:"}}}},
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

} // namespace

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
