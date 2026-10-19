#include "services/HyprOptions.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "services/Hyprland.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace m3 {

namespace {

constexpr const char* kStore = "/hyprland/options";
// The value an option had from the config files before Settings first
// changed it: what a reset goes back to
constexpr const char* kBaseline = "/hyprland/baseline";
constexpr const char* kAnimations = "/hyprland/animations"; // leaf → {enabled, speed, curve, style}
constexpr const char* kCurves = "/hyprland/curves";         // name → [x0, y0, x1, y1]
constexpr const char* kMonitors = "/hyprland/monitors";     // key → {mode, position, scale, …}
constexpr const char* kStartup = "/hyprland/autostart";     // [{command, label, enabled}]
constexpr const char* kEnv = "/hyprland/env";               // [[name, value]]

std::string quoted(const std::string& s); // below

// A monitor rule's fields as Lua
std::string monitorLua(const std::string& key, const nlohmann::json& fields) {
    std::string out = "{ output = " + quoted(key);
    for (auto& [k, v] : fields.items()) {
        out += ", " + k + " = ";
        if (v.is_string()) out += quoted(v.get<std::string>());
        else if (v.is_boolean()) out += v.get<bool>() ? "true" : "false";
        else out += v.dump();
    }
    return out + " }";
}

// Hyprland's animation tree: each leaf and the one it inherits from
const std::map<std::string, std::string> kParents = {
    {"global", ""},
    {"windows", "global"}, {"windowsIn", "windows"}, {"windowsOut", "windows"}, {"windowsMove", "windows"},
    {"layers", "global"}, {"layersIn", "layers"}, {"layersOut", "layers"},
    {"fade", "global"}, {"fadeIn", "fade"}, {"fadeOut", "fade"}, {"fadeSwitch", "fade"}, {"fadeShadow", "fade"},
    {"fadeDim", "fade"}, {"fadeDpms", "fade"}, {"fadeGlow", "fade"}, {"fadeLayers", "fade"},
    {"fadeLayersIn", "fadeLayers"}, {"fadeLayersOut", "fadeLayers"}, {"fadePopups", "fade"},
    {"fadePopupsIn", "fadePopups"}, {"fadePopupsOut", "fadePopups"},
    {"border", "global"}, {"borderangle", "global"}, {"glowangle", "global"}, {"shadowangle", "global"},
    {"workspaces", "global"}, {"workspacesIn", "workspaces"}, {"workspacesOut", "workspaces"},
    {"specialWorkspace", "workspaces"}, {"specialWorkspaceIn", "specialWorkspace"},
    {"specialWorkspaceOut", "specialWorkspace"},
    {"zoomFactor", "global"}, {"monitorAdded", "global"},
};

// Strings with a fixed set of values Hyprland doesn't list itself
const std::map<std::string, std::vector<std::string>> kStringChoices = {
    {"general:layout", {"dwindle", "master", "scrolling", "monocle"}},
    {"input:accel_profile", {"adaptive", "flat", "custom"}},
    {"input:scroll_method", {"2fg", "edge", "on_button_down", "no_scroll"}},
    {"master:orientation", {"left", "right", "top", "bottom", "center"}},
    {"master:new_status", {"master", "slave", "inherit"}},
    {"scrolling:direction", {"right", "left", "down", "up"}},
    {"input:touchpad:tap_button_map", {"lrm", "lmr"}},
};

std::string label(std::string key) {
    std::replace(key.begin(), key.end(), '_', ' ');
    if (!key.empty()) key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    return key;
}

// "AARRGGBB" (Hyprland's readback) → "rgba(RRGGBBAA)"
std::string rgba(const std::string& argb) {
    if (argb.size() != 8) return "rgba(" + argb + ")";
    return "rgba(" + argb.substr(2) + argb.substr(0, 2) + ")";
}

std::string rgbaOf(uint32_t argb) {
    return rgba(std::format("{:08x}", argb));
}

bool looksLikeColor(const std::string& name) {
    return name.find("col.") != std::string::npos || name.ends_with("color") || name.ends_with("_color") ||
           name.find("color_") != std::string::npos;
}

// "ff444444 ffaa00ff 45deg" → {"colors": [...], "angle": 45}
nlohmann::json gradientOf(const std::string& text) {
    nlohmann::json out{{"colors", nlohmann::json::array()}, {"angle", 0}};
    std::istringstream in(text);
    for (std::string w; in >> w;) {
        if (w.ends_with("deg")) out["angle"] = std::atoi(w.c_str());
        else out["colors"].push_back(rgba(w));
    }
    return out;
}

nlohmann::json gapsOf(const nlohmann::json& v) {
    if (v.is_number()) return {v, v, v, v};
    std::istringstream in(v.is_string() ? v.get<std::string>() : std::string());
    std::vector<int> n;
    for (int x; in >> x;) n.push_back(x);
    while (n.size() < 4) n.push_back(n.empty() ? 0 : n.back());
    return {n[0], n[1], n[2], n[3]};
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// A value in getoption's or descriptions' form, read for `kind`
nlohmann::json normalise(HyprOptions::Kind kind, const nlohmann::json& raw) {
    using K = HyprOptions::Kind;
    switch (kind) {
    case K::Bool: return raw.is_boolean() ? raw : nlohmann::json(raw.is_number() && raw.get<double>() != 0);
    case K::Choice:
        // Named string choices (layouts, orientations) stay strings
        if (raw.is_string() && !raw.get<std::string>().empty() &&
            !std::isdigit(static_cast<unsigned char>(raw.get<std::string>()[0])) && raw.get<std::string>()[0] != '-')
            return raw;
        [[fallthrough]];
    case K::Int:
        if (raw.is_number()) return raw.get<long long>();
        if (raw.is_string()) return std::atoll(raw.get<std::string>().c_str());
        return 0;
    case K::Float:
        if (raw.is_number()) return raw.get<double>();
        if (raw.is_string()) return std::atof(raw.get<std::string>().c_str());
        return 0.0;
    case K::Color:
        if (raw.is_number()) return rgbaOf(static_cast<uint32_t>(raw.get<long long>()));
        if (raw.is_string()) {
            const auto g = gradientOf(raw.get<std::string>());
            return g["colors"].empty() ? nlohmann::json("rgba(00000000)") : g["colors"][0];
        }
        return "rgba(00000000)";
    case K::Gradient: return gradientOf(raw.is_string() ? raw.get<std::string>() : std::string());
    case K::Vec2:
        if (raw.is_array() && raw.size() == 2) return raw;
        return {0, 0};
    case K::Gaps: return gapsOf(raw);
    case K::Text:
        // Hyprland reads an empty string back as "[[EMPTY]]"
        if (raw.is_string()) return raw.get<std::string>() == "[[EMPTY]]" ? nlohmann::json("") : raw;
        return raw.dump();
    }
    return raw;
}

std::string luaValue(HyprOptions::Kind kind, const nlohmann::json& v) {
    using K = HyprOptions::Kind;
    switch (kind) {
    case K::Bool: return v.get<bool>() ? "true" : "false";
    case K::Int:
    case K::Choice:
        if (v.is_string()) return quoted(v.get<std::string>());
        return std::to_string(v.get<long long>());
    case K::Float: return std::format("{}", v.get<double>());
    case K::Text:
    case K::Color: return quoted(v.get<std::string>());
    case K::Gradient: {
        std::string colors;
        for (const auto& c : v["colors"]) colors += (colors.empty() ? "" : ", ") + quoted(c.get<std::string>());
        return std::format("{{ colors = {{ {} }}, angle = {} }}", colors, v.value("angle", 0));
    }
    case K::Vec2: return std::format("{{ {}, {} }}", v[0].get<double>(), v[1].get<double>());
    case K::Gaps:
        return std::format("{{ top = {}, right = {}, bottom = {}, left = {} }}", v[0].get<int>(), v[1].get<int>(),
                           v[2].get<int>(), v[3].get<int>());
    }
    return "nil";
}

// "decoration:blur:size" → {"decoration", "blur", "size"}; "col.x" nests too
std::vector<std::string> path(const std::string& name) {
    std::vector<std::string> out;
    std::string part;
    for (char c : name) {
        if (c == ':' || c == '.') {
            out.push_back(part);
            part.clear();
        } else {
            part += c;
        }
    }
    out.push_back(part);
    return out;
}

// A Lua key: bare when it can be, else ["quoted"]
std::string key(const std::string& k) {
    const bool bare = !k.empty() && !std::isdigit(static_cast<unsigned char>(k[0])) &&
                      std::all_of(k.begin(), k.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    return bare ? k : "[" + quoted(k) + "]";
}

std::string sentence(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

} // namespace

HyprOptions& HyprOptions::get() {
    static HyprOptions instance;
    return instance;
}

void HyprOptions::load() {
    auto& h = Hyprland::get();
    const auto descriptions = h.requestJson("descriptions");
    if (!descriptions || !descriptions->is_array()) return;
    // Every value in one batched request: replies are JSON objects back to back
    std::string batch = "[[BATCH]]";
    for (const auto& d : *descriptions) batch += "j/getoption " + d.value("name", "") + ";";
    const std::string reply = h.request(batch);
    std::map<std::string, nlohmann::json> values;
    size_t at = 0;
    while ((at = reply.find('{', at)) != std::string::npos) {
        const size_t end = reply.find('}', at);
        if (end == std::string::npos) break;
        // vec2 values hold no braces, so each object ends at its first '}'
        try {
            auto j = nlohmann::json::parse(reply.substr(at, end - at + 1));
            const std::string name = j.value("option", ""); // before j moves out
            values[name] = std::move(j);
        } catch (...) {
        }
        at = end + 1;
    }

    const auto custom = Config::get().value<nlohmann::json>(kStore, nlohmann::json::object());
    std::vector<Option> out;
    for (const auto& d : *descriptions) {
        Option o;
        o.name = d.value("name", "");
        o.description = sentence(d.value("description", ""));
        const auto it = values.find(o.name);
        std::string type;
        nlohmann::json raw;
        if (it != values.end())
            for (auto& [k, v] : it->second.items())
                if (k != "option" && k != "set") {
                    type = k;
                    raw = v;
                }
        if (d.contains("map") && d["map"].is_array() && !d["map"].empty()) {
            o.kind = Kind::Choice;
            for (const auto& entry : d["map"])
                for (auto& [k, v] : entry.items()) o.choices.push_back({v, label(k)});
            std::ranges::sort(o.choices, [](const auto& a, const auto& b) { return a.first.dump() < b.first.dump(); });
        } else if (type == "bool") {
            o.kind = Kind::Bool;
        } else if (type == "int") {
            o.kind = looksLikeColor(o.name) ? Kind::Color : Kind::Int;
        } else if (type == "float") {
            o.kind = Kind::Float;
        } else if (type == "gradient") {
            o.kind = o.name.find("col.") != std::string::npos ? Kind::Gradient : Kind::Color;
        } else if (type == "vec2") {
            o.kind = Kind::Vec2;
        } else if (type == "css") {
            o.kind = Kind::Gaps;
        } else {
            o.kind = Kind::Text;
            if (auto c = kStringChoices.find(o.name); c != kStringChoices.end()) {
                o.kind = Kind::Choice;
                for (const auto& s : c->second) o.choices.push_back({s, label(s)});
            }
        }
        if (d.contains("min") && d.contains("max") && d["min"].is_number() && d["max"].is_number()) {
            o.ranged = true;
            o.min = d["min"].get<double>();
            o.max = d["max"].get<double>();
        }
        o.fallback = normalise(o.kind, d.value("default", nlohmann::json()));
        o.value = raw.is_null() ? o.fallback : normalise(o.kind, raw);
        o.custom = custom.contains(o.name);
        out.push_back(std::move(o));
    }
    m_options = std::move(out);
    loadAnimations();
    changed.emit();
}

void HyprOptions::loadAnimations() {
    const auto reply = Hyprland::get().requestJson("animations");
    if (!reply || !reply->is_array() || reply->size() < 2) return;
    const auto kept = Config::get().value<nlohmann::json>(kAnimations, nlohmann::json::object());
    const auto keptCurves = Config::get().value<nlohmann::json>(kCurves, nlohmann::json::object());
    m_animations.clear();
    for (const auto& a : (*reply)[0]) {
        Animation x;
        x.name = a.value("name", "");
        if (x.name.starts_with("__")) continue; // Hyprland's own
        const auto p = kParents.find(x.name);
        x.parent = p != kParents.end() ? p->second : "global";
        x.overridden = a.value("overridden", false);
        x.enabled = a.value("enabled", true);
        x.speed = a.value("speed", 1.0);
        x.curve = a.value("bezier", "");
        x.style = a.value("style", "");
        x.custom = kept.contains(x.name);
        m_animations.push_back(std::move(x));
    }
    effective();
    m_curves.clear();
    for (const auto& b : (*reply)[1]) {
        Curve c;
        c.name = b.value("name", "");
        c.x0 = b.value("X0", 0.0);
        c.y0 = b.value("Y0", 0.0);
        c.x1 = b.value("X1", 1.0);
        c.y1 = b.value("Y1", 1.0);
        c.custom = keptCurves.contains(c.name);
        m_curves.push_back(std::move(c));
    }
    std::ranges::sort(m_curves, [](const Curve& a, const Curve& b) { return a.name < b.name; });
}

void HyprOptions::effective() {
    // Parents before children: walk the tree from the root down
    std::map<std::string, Animation*> byName;
    for (auto& a : m_animations) byName[a.name] = &a;
    std::function<void(Animation&)> resolve = [&](Animation& a) {
        if (a.overridden || a.parent.empty()) return;
        auto it = byName.find(a.parent);
        if (it == byName.end()) return;
        resolve(*it->second);
        a.enabled = it->second->enabled;
        a.speed = it->second->speed;
        a.curve = it->second->curve;
        // A parent's style only carries over within the same family
        if (a.style.empty()) a.style = it->second->style;
    };
    for (auto& a : m_animations) resolve(a);
}

const HyprOptions::Animation* HyprOptions::animation(const std::string& name) const {
    for (const auto& a : m_animations)
        if (a.name == name) return &a;
    return nullptr;
}

std::string HyprOptions::applyAnimation(const Animation& a) {
    std::string lua = std::format("eval hl.animation({{ leaf = {}, enabled = {}", quoted(a.name), a.enabled ? "true" : "false");
    if (a.enabled) {
        lua += std::format(", speed = {}", a.speed);
        if (!a.curve.empty()) lua += ", bezier = " + quoted(a.curve);
        if (!a.style.empty()) lua += ", style = " + quoted(a.style);
    }
    const std::string reply = Hyprland::get().request(lua + " })");
    if (reply.empty() || reply.starts_with("ok")) return {};
    const auto at = reply.rfind(": ");
    return at == std::string::npos ? reply : sentence(reply.substr(at + 2));
}

void HyprOptions::remember(const Step& step) {
    m_undo.push_back(step);
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
}

std::string HyprOptions::setAnimation(const std::string& name, bool enabled, double speed, const std::string& curve,
                                      const std::string& style) {
    Animation* a = nullptr;
    for (auto& x : m_animations)
        if (x.name == name) a = &x;
    if (!a) return "Unknown animation";
    Animation next = *a;
    next.overridden = true;
    next.enabled = enabled;
    next.speed = std::clamp(speed, 0.1, 100.0);
    next.curve = curve;
    next.style = style;
    if (auto err = applyAnimation(next); !err.empty()) return err;
    auto kept = Config::get().value<nlohmann::json>(kAnimations, nlohmann::json::object());
    Step step{What::Animation, name, std::nullopt,
              {{"overridden", a->overridden}, {"enabled", a->enabled}, {"speed", a->speed}, {"curve", a->curve}, {"style", a->style}}};
    if (kept.contains(name)) step.custom = kept[name];
    remember(step);
    kept[name] = {{"enabled", enabled}, {"speed", next.speed}, {"curve", curve}, {"style", style}};
    Config::get().set(kAnimations, kept);
    *a = next;
    a->custom = true;
    effective();
    save();
    changed.emit();
    return {};
}

std::string HyprOptions::resetAnimation(const std::string& name) {
    Animation* a = nullptr;
    for (auto& x : m_animations)
        if (x.name == name) a = &x;
    if (!a) return "Unknown animation";
    auto kept = Config::get().value<nlohmann::json>(kAnimations, nlohmann::json::object());
    Step step{What::Animation, name, std::nullopt,
              {{"overridden", a->overridden}, {"enabled", a->enabled}, {"speed", a->speed}, {"curve", a->curve}, {"style", a->style}}};
    if (kept.contains(name)) step.custom = kept[name];
    remember(step);
    kept.erase(name);
    Config::get().set(kAnimations, kept);
    // Hyprland can't un-override a leaf live: give it the parent's values
    // now; from the next reload it inherits again (or takes the config's)
    a->custom = false;
    a->overridden = false;
    a->style.clear();
    effective();
    applyAnimation(*a);
    save();
    changed.emit();
    return {};
}

std::string HyprOptions::setCurve(const std::string& name, double x0, double y0, double x1, double y1) {
    if (name.empty()) return "A curve needs a name";
    const std::string reply = Hyprland::get().request(std::format(
        "eval hl.curve({}, {{ type = \"bezier\", points = {{ {{{}, {}}}, {{{}, {}}} }} }})", quoted(name), x0, y0, x1, y1));
    if (!reply.empty() && !reply.starts_with("ok")) return reply;
    auto kept = Config::get().value<nlohmann::json>(kCurves, nlohmann::json::object());
    Step step{What::Curve, name, std::nullopt, nullptr};
    for (const auto& c : m_curves)
        if (c.name == name) step.value = {c.x0, c.y0, c.x1, c.y1};
    if (kept.contains(name)) step.custom = kept[name];
    remember(step);
    kept[name] = {x0, y0, x1, y1};
    Config::get().set(kCurves, kept);
    bool found = false;
    for (auto& c : m_curves)
        if (c.name == name) {
            c = {name, x0, y0, x1, y1, true};
            found = true;
        }
    if (!found) {
        m_curves.push_back({name, x0, y0, x1, y1, true});
        std::ranges::sort(m_curves, [](const Curve& a, const Curve& b) { return a.name < b.name; });
    }
    save();
    changed.emit();
    return {};
}

void HyprOptions::removeCurve(const std::string& name) {
    auto kept = Config::get().value<nlohmann::json>(kCurves, nlohmann::json::object());
    if (!kept.contains(name)) return;
    kept.erase(name);
    Config::get().set(kCurves, kept);
    // Still defined until the next reload, but no longer kept or listed
    std::erase_if(m_curves, [&](const Curve& c) { return c.name == name; });
    save();
    changed.emit();
}

const HyprOptions::Option* HyprOptions::find(const std::string& name) const {
    for (const auto& o : m_options)
        if (o.name == name) return &o;
    return nullptr;
}

HyprOptions::Option* HyprOptions::findMutable(const std::string& name) {
    for (auto& o : m_options)
        if (o.name == name) return &o;
    return nullptr;
}

std::string HyprOptions::lua(const Option& o, const nlohmann::json& value) {
    const auto parts = path(o.name);
    std::string open, close;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        open += key(parts[i]) + " = { ";
        close += " }";
    }
    return "{ " + open + key(parts.back()) + " = " + luaValue(o.kind, value) + close + " }";
}

std::string HyprOptions::apply(const Option& o, const nlohmann::json& value) {
    const std::string reply = Hyprland::get().request("eval hl.config(" + lua(o, value) + ")");
    if (reply.empty() || reply.starts_with("ok")) return {};
    // "error: return hl.config(...);:1: error setting 'x': why" → why
    const auto at = reply.rfind("': ");
    return at == std::string::npos ? reply : sentence(reply.substr(at + 3));
}

std::string HyprOptions::set(const std::string& name, const nlohmann::json& given) {
    Option* o = findMutable(name);
    if (!o) return "Unknown option";
    // Hyprland takes values past an option's range: keep to it here
    nlohmann::json value = given;
    if (o->ranged && value.is_number()) {
        if (o->kind == Kind::Int) value = std::clamp<long long>(value.get<long long>(), static_cast<long long>(o->min), static_cast<long long>(o->max));
        if (o->kind == Kind::Float) value = std::clamp(value.get<double>(), o->min, o->max);
    }
    const nlohmann::json before = o->value;
    const auto custom = Config::get().value<nlohmann::json>(kStore, nlohmann::json::object());
    std::optional<nlohmann::json> kept;
    if (custom.contains(name)) kept = custom[name];
    if (auto err = apply(*o, value); !err.empty()) return err;
    remember({What::Option, name, kept, before});
    if (!kept) Config::get().set(std::string(kBaseline) + "/" + name, before);
    o->value = value;
    o->custom = true;
    // Option names hold no '/' or '~', so they need no pointer escaping
    Config::get().set(std::string(kStore) + "/" + name, value);
    save();
    changed.emit();
    return {};
}

std::string HyprOptions::reset(const std::string& name) {
    Option* o = findMutable(name);
    if (!o) return "Unknown option";
    const nlohmann::json before = o->value;
    auto custom = Config::get().value<nlohmann::json>(kStore, nlohmann::json::object());
    std::optional<nlohmann::json> kept;
    if (custom.contains(name)) kept = custom[name];
    // What the config files had, else Hyprland's own default
    auto baseline = Config::get().value<nlohmann::json>(kBaseline, nlohmann::json::object());
    const nlohmann::json target = baseline.contains(name) ? baseline[name] : o->fallback;
    if (auto err = apply(*o, target); !err.empty()) return err;
    remember({What::Option, name, kept, before});
    o->value = target;
    o->custom = false;
    custom.erase(name);
    baseline.erase(name);
    Config::get().set(kStore, custom);
    Config::get().set(kBaseline, baseline);
    save();
    changed.emit();
    return {};
}

std::string HyprOptions::undo() {
    if (m_undo.empty()) return {};
    const Step step = m_undo.back();
    m_undo.pop_back();
    if (step.what == What::Animation) {
        for (auto& a : m_animations)
            if (a.name == step.name) {
                a.overridden = step.value.value("overridden", true);
                a.enabled = step.value.value("enabled", true);
                a.speed = step.value.value("speed", 1.0);
                a.curve = step.value.value("curve", "");
                a.style = step.value.value("style", "");
                a.custom = step.custom.has_value();
                applyAnimation(a);
            }
        auto kept = Config::get().value<nlohmann::json>(kAnimations, nlohmann::json::object());
        if (step.custom) kept[step.name] = *step.custom;
        else kept.erase(step.name);
        Config::get().set(kAnimations, kept);
        effective();
        save();
        changed.emit();
        return step.name;
    }
    if (step.what == What::Curve) {
        auto kept = Config::get().value<nlohmann::json>(kCurves, nlohmann::json::object());
        if (step.custom) kept[step.name] = *step.custom;
        else kept.erase(step.name);
        Config::get().set(kCurves, kept);
        if (step.value.is_array()) {
            const auto& v = step.value;
            Hyprland::get().request(std::format("eval hl.curve({}, {{ type = \"bezier\", points = {{ {{{}, {}}}, {{{}, {}}} }} }})",
                                                quoted(step.name), v[0].get<double>(), v[1].get<double>(), v[2].get<double>(),
                                                v[3].get<double>()));
            for (auto& c : m_curves)
                if (c.name == step.name) c = {step.name, v[0], v[1], v[2], v[3], step.custom.has_value()};
        } else {
            std::erase_if(m_curves, [&](const Curve& c) { return c.name == step.name; });
        }
        save();
        changed.emit();
        return step.name;
    }
    Option* o = findMutable(step.name);
    if (!o) return {};
    if (auto err = apply(*o, step.value); !err.empty()) return err;
    o->value = step.value;
    auto custom = Config::get().value<nlohmann::json>(kStore, nlohmann::json::object());
    if (step.custom) {
        custom[step.name] = *step.custom;
    } else {
        // Back to before Settings first changed it: no baseline to keep
        custom.erase(step.name);
        auto baseline = Config::get().value<nlohmann::json>(kBaseline, nlohmann::json::object());
        baseline.erase(step.name);
        Config::get().set(kBaseline, baseline);
    }
    o->custom = step.custom.has_value();
    Config::get().set(kStore, custom);
    save();
    changed.emit();
    return o->name;
}

std::vector<HyprOptions::Monitor> HyprOptions::monitors() const {
    std::vector<Monitor> out;
    const auto reply = Hyprland::get().requestJson("monitors all");
    if (!reply || !reply->is_array()) return out;
    const auto kept = Config::get().value<nlohmann::json>(kMonitors, nlohmann::json::object());
    for (const auto& m : *reply) {
        Monitor x;
        x.name = m.value("name", "");
        x.description = m.value("description", "");
        x.key = x.description.empty() ? x.name : "desc:" + x.description;
        x.width = m.value("width", 0);
        x.height = m.value("height", 0);
        x.refresh = m.value("refreshRate", 0.0);
        x.x = m.value("x", 0);
        x.y = m.value("y", 0);
        x.scale = m.value("scale", 1.0);
        x.transform = m.value("transform", 0);
        x.disabled = m.value("disabled", false);
        x.vrr = m.value("vrr", false);
        x.focused = m.value("focused", false);
        x.mirrorOf = m.value("mirrorOf", "none");
        if (x.mirrorOf == "none") x.mirrorOf.clear();
        // 10-bit formats name their channels in tens: XRGB2101010
        x.bitdepth = m.value("currentFormat", "").find("2101010") != std::string::npos ? 10 : 8;
        x.cm = m.value("colorManagementPreset", "srgb");
        x.sdrBrightness = m.value("sdrBrightness", 1.0);
        x.sdrSaturation = m.value("sdrSaturation", 1.0);
        for (const auto& mode : m.value("availableModes", nlohmann::json::array())) x.modes.push_back(mode.get<std::string>());
        if (kept.contains(x.key)) x.kept = kept[x.key];
        out.push_back(std::move(x));
    }
    return out;
}

std::string HyprOptions::applyMonitor(const std::string& key, const nlohmann::json& fields) {
    const std::string reply = Hyprland::get().request("eval hl.monitor(" + monitorLua(key, fields) + ")");
    if (reply.empty() || reply.starts_with("ok")) return {};
    const auto at = reply.rfind("': ");
    return at == std::string::npos ? reply : sentence(reply.substr(at + 3));
}

void HyprOptions::keepMonitor(const std::string& key, const nlohmann::json& fields) {
    auto kept = Config::get().value<nlohmann::json>(kMonitors, nlohmann::json::object());
    if (!kept.contains(key)) kept[key] = nlohmann::json::object();
    for (auto& [k, v] : fields.items()) kept[key][k] = v;
    Config::get().set(kMonitors, kept);
    save();
    changed.emit();
}

void HyprOptions::forgetMonitor(const std::string& key) {
    auto kept = Config::get().value<nlohmann::json>(kMonitors, nlohmann::json::object());
    kept.erase(key);
    Config::get().set(kMonitors, kept);
    save();
    // Rules can't be taken back live: the config, without this one, again
    Hyprland::get().request("reload");
    changed.emit();
}

std::vector<HyprOptions::Startup> HyprOptions::startups() const {
    std::vector<Startup> out;
    for (const auto& s : Config::get().value<nlohmann::json>(kStartup, nlohmann::json::array()))
        out.push_back({s.value("command", ""), s.value("label", ""), s.value("enabled", true)});
    return out;
}

void HyprOptions::setStartups(const std::vector<Startup>& list) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& s : list) out.push_back({{"command", s.command}, {"label", s.label}, {"enabled", s.enabled}});
    Config::get().set(kStartup, out);
    save();
    changed.emit();
}

std::vector<std::pair<std::string, std::string>> HyprOptions::environment() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& v : Config::get().value<nlohmann::json>(kEnv, nlohmann::json::array()))
        if (v.is_array() && v.size() == 2) out.push_back({v[0].get<std::string>(), v[1].get<std::string>()});
    return out;
}

void HyprOptions::setEnvironment(const std::vector<std::pair<std::string, std::string>>& vars) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& [k, v] : vars) {
        out.push_back({k, v});
        // Live too, for whatever Hyprland starts from now on
        Hyprland::get().request(std::format("eval hl.env({}, {})", quoted(k), quoted(v)));
    }
    Config::get().set(kEnv, out);
    save();
    changed.emit();
}

void HyprOptions::save() {
    // Every kept option, as a function shell.lua calls after the config
    const auto custom = Config::get().value<nlohmann::json>(kStore, nlohmann::json::object());
    std::string body;
    // The session's environment, before anything starts
    for (const auto& [k, v] : environment()) body += std::format("\tpcall(hl.env, {}, {})\n", quoted(k), quoted(v));
    // Monitors next: everything else is laid out on them
    for (auto& [key, fields] : Config::get().value<nlohmann::json>(kMonitors, nlohmann::json::object()).items())
        body += "\tpcall(hl.monitor, " + monitorLua(key, fields) + ")\n";
    // Curves next: the animations below may use them
    for (auto& [name, p] : Config::get().value<nlohmann::json>(kCurves, nlohmann::json::object()).items())
        if (p.is_array() && p.size() == 4)
            body += std::format("\tpcall(hl.curve, {}, {{ type = \"bezier\", points = {{ {{{}, {}}}, {{{}, {}}} }} }})\n",
                                quoted(name), p[0].get<double>(), p[1].get<double>(), p[2].get<double>(), p[3].get<double>());
    for (auto& [leaf, a] : Config::get().value<nlohmann::json>(kAnimations, nlohmann::json::object()).items()) {
        std::string args = std::format("leaf = {}, enabled = {}", quoted(leaf), a.value("enabled", true) ? "true" : "false");
        if (a.value("enabled", true)) {
            args += std::format(", speed = {}", a.value("speed", 1.0));
            if (!a.value("curve", "").empty()) args += ", bezier = " + quoted(a.value("curve", ""));
            if (!a.value("style", "").empty()) args += ", style = " + quoted(a.value("style", ""));
        }
        body += "\tpcall(hl.animation, { " + args + " })\n";
    }
    for (auto& [name, value] : custom.items()) {
        const Option* o = find(name);
        if (!o) continue;
        // One call each: a value Hyprland refuses doesn't stop the rest
        body += "\tpcall(hl.config, " + lua(*o, value) + ")\n";
    }
    const std::string dir = paths::home() + "/.config/hypr/material3ui";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string file = dir + "/hyprland_custom.lua";
    std::ofstream out(file + ".tmp");
    out << "-- Hyprland options changed in Material3UI Settings. Generated: edit them\n"
           "-- there (each has a reset), or delete this file to drop them all.\n"
           "-- shell.lua runs this after the rest of the config, so it wins.\n"
           "-- `stage` is \"load\", \"start\" (once, after Hyprland starts) or \"reload\".\n"
           "return function(stage)\n"
        << body;
    // Startup apps: once, when Hyprland has started
    std::string starts;
    for (const auto& s : startups())
        if (s.enabled && !s.command.empty()) starts += "\t\tpcall(hl.exec_cmd, " + quoted(s.command) + ")\n";
    if (!starts.empty()) out << "\tif stage == \"start\" then\n" << starts << "\tend\n";
    out << "end\n";
    out.close();
    std::filesystem::rename(file + ".tmp", file, ec);
}

} // namespace m3
