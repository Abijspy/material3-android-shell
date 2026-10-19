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
#include <map>
#include <sstream>

namespace m3 {

namespace {

constexpr const char* kStore = "/hyprland/options";
// The value an option had from the config files before Settings first
// changed it: what a reset goes back to
constexpr const char* kBaseline = "/hyprland/baseline";

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
    m_undo.push_back({name, kept, before});
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
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
    m_undo.push_back({name, kept, before});
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

void HyprOptions::save() {
    // Every kept option, as a function shell.lua calls after the config
    const auto custom = Config::get().value<nlohmann::json>(kStore, nlohmann::json::object());
    std::string body;
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
           "return function()\n"
        << body << "end\n";
    out.close();
    std::filesystem::rename(file + ".tmp", file, ec);
}

} // namespace m3
