#include "services/Keybinds.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "services/Hyprland.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace m3 {

namespace {

const char* kMods[] = {"SUPER", "CTRL", "ALT", "SHIFT"};

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string storeFile() {
    return paths::configDir() + "/keybinds.json";
}

std::string luaFile() {
    return paths::home() + "/.config/hypr/material3ui/keybinds_custom.lua";
}

std::string luaString(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

} // namespace

Keybinds& Keybinds::get() {
    static Keybinds instance;
    return instance;
}

Keybinds::Keybinds() {
    load();
    // Hyprland re-reads its config after we write: pick up the new binds
    Hyprland::get().event.connectForever([this](const std::string& name, const std::string&) {
        if (name == "configreloaded") EventLoop::get().defer([this] { refresh(); });
    });
}

bool Keybinds::supported() const {
    return Hyprland::get().available() && Hyprland::get().usingLua();
}

std::string Keybinds::normalize(const std::string& keys) {
    static const std::map<std::string, std::string> aliases = {
        {"CONTROL", "CTRL"}, {"WIN", "SUPER"}, {"META", "SUPER"}, {"MOD4", "SUPER"}, {"MOD1", "ALT"}};
    bool mods[4] = {};
    std::string key;
    size_t start = 0;
    while (start <= keys.size()) {
        size_t end = keys.find('+', start);
        if (end == std::string::npos) end = keys.size();
        std::string p = keys.substr(start, end - start);
        p.erase(0, p.find_first_not_of(' '));
        if (const auto last = p.find_last_not_of(' '); last != std::string::npos) p.erase(last + 1);
        start = end + 1;
        if (p.empty()) continue;
        std::string up = upper(p);
        if (auto a = aliases.find(up); a != aliases.end()) up = a->second;
        bool isMod = false;
        for (int i = 0; i < 4; ++i)
            if (up == kMods[i]) mods[i] = isMod = true;
        if (!isMod) key = p;
    }
    std::string out;
    for (int i = 0; i < 4; ++i)
        if (mods[i]) out += std::string(kMods[i]) + " + ";
    return out + key;
}

std::string Keybinds::idOf(const std::string& keys) {
    return lower(normalize(keys));
}

std::string Keybinds::pretty(const std::string& combo) {
    static const std::map<std::string, std::string> names = {
        {"super", "Super"}, {"ctrl", "Ctrl"}, {"alt", "Alt"}, {"shift", "Shift"}, {"super_l", "Super (tap)"},
        {"return", "Enter"}, {"escape", "Esc"}, {"period", "."}, {"comma", ","}, {"space", "Space"}, {"tab", "Tab"},
        {"print", "PrtSc"}, {"delete", "Del"}, {"mouse:272", "Left click"}, {"mouse:273", "Right click"},
        {"mouse_down", "Scroll down"}, {"mouse_up", "Scroll up"}, {"left", "←"}, {"right", "→"}, {"up", "↑"}, {"down", "↓"},
        {"xf86audioraisevolume", "Vol +"}, {"xf86audiolowervolume", "Vol −"}, {"xf86audiomute", "Mute"},
        {"xf86audiomicmute", "Mic mute"}, {"xf86audioplay", "Play"}, {"xf86audiopause", "Pause"},
        {"xf86audionext", "Next"}, {"xf86audioprev", "Prev"}, {"xf86monbrightnessup", "Bright +"},
        {"xf86monbrightnessdown", "Bright −"}};
    std::string out;
    const std::string n = normalize(combo);
    if (lower(n) == "super + super_l") return "Super (tap)";
    size_t start = 0;
    while (start < n.size()) {
        size_t end = n.find(" + ", start);
        if (end == std::string::npos) end = n.size();
        const std::string part = n.substr(start, end - start);
        auto it = names.find(lower(part));
        out += (out.empty() ? "" : " + ") + (it != names.end() ? it->second : part.size() == 1 ? upper(part) : part);
        start = end + 3;
    }
    return out;
}

void Keybinds::load() {
    std::ifstream in(storeFile());
    if (!in) return;
    try {
        const auto j = nlohmann::json::parse(in);
        m_overrides = j.value("overrides", nlohmann::json::object());
        m_labels = j.value("labels", nlohmann::json::object());
        m_custom.clear();
        for (const auto& c : j.value("custom", nlohmann::json::array()))
            m_custom.push_back({c.value("keys", ""), c.value("command", ""), c.value("label", "")});
    } catch (const std::exception& e) {
        log::warn("keybinds.json: {}", e.what());
    }
}

void Keybinds::refresh() {
    m_binds.clear();
    m_others.clear();
    auto list = Hyprland::get().requestJson("binds");
    if (list && list->is_array()) {
        std::map<std::string, std::string> reverse; // current id → default id
        for (const auto& [id, combo] : m_overrides.items())
            if (combo.is_string()) reverse[idOf(combo.get<std::string>())] = id;
        std::vector<std::string> customIds;
        for (const auto& c : m_custom) customIds.push_back(idOf(c.keys));
        std::map<std::string, int> counts;
        for (const auto& b : *list) {
            if (!b.value("submap", "").empty()) continue;
            const int mask = b.value("modmask", 0);
            const int bits[4] = {64, 4, 8, 1};
            std::string combo;
            for (int i = 0; i < 4; ++i)
                if (mask & bits[i]) combo += std::string(kMods[i]) + " + ";
            combo += b.value("key", "");
            const std::string label = b.value("has_description", false) ? b.value("description", "") : "";
            const std::string id = idOf(combo);
            ++counts[id];
            if (label.starts_with("Custom: ") && std::ranges::find(customIds, id) != customIds.end()) continue;
            const auto colon = label.find(": ");
            if (colon != std::string::npos && colon > 0) {
                const auto def = reverse.find(id);
                m_binds.push_back({def != reverse.end() ? def->second : id, combo, label, label.substr(0, colon),
                                   label.substr(colon + 2), def != reverse.end(), false});
            } else {
                m_others.push_back({combo, !label.empty() ? label : (b.value("dispatcher", "") + " " + b.value("arg", ""))});
            }
        }
        for (const auto& c : customIds) ++counts[c];
        for (auto& b : m_binds) b.conflict = counts[idOf(b.combo)] > 1;
    }
    changed.emit();
}

std::vector<Keybinds::Bind> Keybinds::disabled() const {
    std::vector<Bind> out;
    for (const auto& [id, v] : m_overrides.items()) {
        if (!v.is_boolean() || v.get<bool>()) continue;
        const std::string label = m_labels.value(id, id);
        const auto colon = label.find(": ");
        out.push_back({id, "", label, colon != std::string::npos ? label.substr(0, colon) : "Other",
                       colon != std::string::npos ? label.substr(colon + 2) : label, true, false});
    }
    return out;
}

std::vector<std::string> Keybinds::conflictsFor(const std::string& combo, const std::string& exceptId) const {
    std::vector<std::string> hits;
    const std::string c = idOf(combo);
    for (const auto& b : m_binds)
        if (idOf(b.combo) == c && b.id != exceptId) hits.push_back(b.action);
    for (const auto& o : m_others)
        if (idOf(o.combo) == c) hits.push_back(o.label);
    for (const auto& k : m_custom)
        if (idOf(k.keys) == c) hits.push_back(k.label);
    return hits;
}

void Keybinds::remap(const std::string& id, const std::string& combo, const std::string& label) {
    if (idOf(combo) == id)
        m_overrides.erase(id);
    else
        m_overrides[id] = normalize(combo);
    m_labels[id] = label;
    commit();
}

void Keybinds::disable(const std::string& id, const std::string& label) {
    m_overrides[id] = false;
    m_labels[id] = label;
    commit();
}

void Keybinds::reset(const std::string& id) {
    m_overrides.erase(id);
    m_labels.erase(id);
    commit();
}

void Keybinds::resetAll() {
    m_overrides = nlohmann::json::object();
    m_labels = nlohmann::json::object();
    commit();
}

void Keybinds::addCustom(const std::string& keys, const std::string& command, const std::string& label) {
    m_custom.push_back({normalize(keys), command, label.empty() ? command : label});
    commit();
}

void Keybinds::removeCustom(size_t index) {
    if (index < m_custom.size()) m_custom.erase(m_custom.begin() + static_cast<long>(index));
    commit();
}

void Keybinds::commit() {
    nlohmann::json custom = nlohmann::json::array();
    for (const auto& c : m_custom) custom.push_back({{"keys", c.keys}, {"command", c.command}, {"label", c.label}});
    std::error_code ec;
    fs::create_directories(paths::configDir(), ec);
    std::ofstream(storeFile()) << nlohmann::json{{"overrides", m_overrides}, {"labels", m_labels}, {"custom", custom}}.dump(2);

    // The Lua side reads this before binding (see bindings.lua)
    std::string lua = "-- Written by Material3UI (Settings → Keybinds).\n-- Edit binds there; changes made here are overwritten.\n"
                      "return {\n\toverrides = {\n";
    std::vector<std::string> ids;
    for (const auto& [id, _] : m_overrides.items()) ids.push_back(id);
    std::ranges::sort(ids);
    for (const auto& id : ids) {
        const auto& v = m_overrides[id];
        lua += "\t\t[" + luaString(id) + "] = " + (v.is_string() ? luaString(v.get<std::string>()) : std::string("false")) + ",\n";
    }
    lua += "\t},\n\tcustom = {\n";
    for (const auto& c : m_custom)
        lua += "\t\t{ keys = " + luaString(c.keys) + ", command = " + luaString(c.command) + ", label = " + luaString(c.label) + " },\n";
    lua += "\t},\n}\n";
    fs::create_directories(fs::path(luaFile()).parent_path(), ec);
    std::ofstream(luaFile()) << lua;

    Hyprland::get().request("reload");
    changed.emit();
}

} // namespace m3
