#pragma once

#include "core/Signal.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace m3 {

// Hyprland keybind editing, compatible with the QML editor: Material3UI's
// binds are registered by hypr/material3ui/bindings.lua with a "Group:
// Action" description and keyed by their default combo (lowercased); edits
// live in ~/.config/material3ui/keybinds.json and are rendered to
// hypr/material3ui/keybinds_custom.lua, then Hyprland reloads.
class Keybinds {
public:
    struct Bind {
        std::string id;    // default combo, lowercased: "super + ctrl + t"
        std::string combo; // current combo as Hyprland reports it
        std::string label, group, action;
        bool changed = false, conflict = false;
    };
    struct Other {
        std::string combo, label; // binds from user.lua and elsewhere
    };
    struct Custom {
        std::string keys, command, label;
        std::string action; // a Lua dispatcher (hl.dsp…), used instead of the command
        bool mouse = false; // a mouse drag bind (bindm)
    };

    static Keybinds& get();

    bool supported() const; // needs Hyprland's Lua config
    void refresh();

    const std::vector<Bind>& binds() const { return m_binds; }
    const std::vector<Other>& others() const { return m_others; }
    std::vector<Bind> disabled() const;
    const std::vector<Custom>& custom() const { return m_custom; }

    void remap(const std::string& id, const std::string& combo, const std::string& label);
    void disable(const std::string& id, const std::string& label);
    void reset(const std::string& id);
    void resetAll();
    void addCustom(const std::string& keys, const std::string& command, const std::string& label,
                   const std::string& action = {}, bool mouse = false);
    void removeCustom(size_t index);

    // Combos: "CTRL + SUPER + t" → "SUPER + CTRL + t"; ids are lowercased
    static std::string normalize(const std::string& keys);
    static std::string idOf(const std::string& keys) ;
    static std::string pretty(const std::string& combo); // "Super + Ctrl + T"
    std::vector<std::string> conflictsFor(const std::string& combo, const std::string& exceptId) const;

    Signal<> changed;

private:
    Keybinds();
    void load();
    void commit();

    std::vector<Bind> m_binds;
    std::vector<Other> m_others;
    nlohmann::json m_overrides = nlohmann::json::object(); // id → combo | false
    nlohmann::json m_labels = nlohmann::json::object();
    std::vector<Custom> m_custom;
};

} // namespace m3
