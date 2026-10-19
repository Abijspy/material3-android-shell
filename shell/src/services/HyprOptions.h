#pragma once

#include "core/Signal.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace m3 {

// Every Hyprland option, as the running compositor describes it
// (`descriptions` for the text, range and choices; `getoption` for the type
// and value), and the ones changed in Settings. A change applies live
// through the socket and is kept in config.json (/hyprland/options), from
// which hyprland_custom.lua is written; shell.lua applies that file after
// the rest of the config, so what Settings shows is what Hyprland uses.
class HyprOptions {
public:
    enum class Kind { Bool, Int, Float, Text, Choice, Color, Gradient, Vec2, Gaps };

    // Values in one form per kind: bool; int; double; string (Text, and
    // Color as "rgba(RRGGBBAA)"); int (Choice); {"colors": [...], "angle":
    // n} (Gradient); [x, y] (Vec2); [top, right, bottom, left] (Gaps)
    struct Option {
        std::string name;        // general:col.active_border
        std::string description; // Hyprland's, sentence-cased
        Kind kind = Kind::Text;
        nlohmann::json value;    // what Hyprland uses now
        nlohmann::json fallback; // Hyprland's default
        bool ranged = false;
        double min = 0, max = 0;
        std::vector<std::pair<nlohmann::json, std::string>> choices; // value, label
        bool custom = false;     // set in Settings (kept in hyprland_custom.lua)
    };

    static HyprOptions& get();

    // Reads the options from the compositor (cheap: two socket requests)
    void load();
    bool loaded() const { return !m_options.empty(); }
    const std::vector<Option>& options() const { return m_options; }
    const Option* find(const std::string& name) const;

    // Applies live and keeps it; the error Hyprland gave, if any
    std::string set(const std::string& name, const nlohmann::json& value);
    // Back to what the config files had (else Hyprland's default), no
    // longer kept
    std::string reset(const std::string& name);
    // Steps back through set() and reset() calls
    bool canUndo() const { return !m_undo.empty(); }
    std::string undo();

    // The Lua for a value (for the generated file and live applies)
    static std::string lua(const Option& o, const nlohmann::json& value);

    Signal<> changed;

private:
    HyprOptions() = default;
    Option* findMutable(const std::string& name);
    std::string apply(const Option& o, const nlohmann::json& value);
    void save();

    struct Step {
        std::string name;
        std::optional<nlohmann::json> custom; // what Settings kept before
        nlohmann::json value;                 // what Hyprland used before
    };
    std::vector<Option> m_options;
    std::vector<Step> m_undo;
};

} // namespace m3
