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

    // An animation leaf (windowsIn, fade…): its own values when overridden,
    // else its parent's (effective values are filled in either way)
    struct Animation {
        std::string name, parent; // parent "" for global
        bool overridden = false, enabled = true;
        double speed = 1;         // deciseconds, as Hyprland counts
        std::string curve, style;
        bool custom = false;      // set in Settings
    };
    // A named bezier from (0,0) to (1,1) through two control points
    struct Curve {
        std::string name;
        double x0 = 0, y0 = 0, x1 = 1, y1 = 1;
        bool custom = false;
    };

    // A connected monitor as Hyprland runs it now, and what Settings keeps
    // for it. `key` names it in rules: "desc:<description>" when it has one
    // (stays right whichever port it moves to), else its connector.
    struct Monitor {
        std::string name, description, key;
        int width = 0, height = 0, x = 0, y = 0, transform = 0, bitdepth = 8;
        double refresh = 0, scale = 1, sdrBrightness = 1, sdrSaturation = 1;
        bool disabled = false, vrr = false, focused = false;
        std::string mirrorOf, cm;
        std::vector<std::string> modes; // "1920x1080@60.01Hz"
        nlohmann::json kept = nlohmann::json::object(); // Settings' fields for it
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

    const std::vector<Animation>& animations() const { return m_animations; }
    const std::vector<Curve>& curves() const { return m_curves; }
    const Animation* animation(const std::string& name) const;
    std::string setAnimation(const std::string& name, bool enabled, double speed, const std::string& curve,
                             const std::string& style);
    // Stops overriding: takes the parent's values again
    std::string resetAnimation(const std::string& name);
    std::string setCurve(const std::string& name, double x0, double y0, double x1, double y1);
    // Only curves made in Settings can go (the config may use the others)
    void removeCurve(const std::string& name);

    // Monitors: read live; apply changes live (risky ones are confirmed by
    // the page before keepMonitor saves them); forget drops Settings' rule
    std::vector<Monitor> monitors() const;
    std::string applyMonitor(const std::string& key, const nlohmann::json& fields);
    void keepMonitor(const std::string& key, const nlohmann::json& fields);
    void forgetMonitor(const std::string& key);

    // The Lua for a value (for the generated file and live applies)
    static std::string lua(const Option& o, const nlohmann::json& value);

    Signal<> changed;

private:
    HyprOptions() = default;
    Option* findMutable(const std::string& name);
    std::string apply(const Option& o, const nlohmann::json& value);
    void save();

    enum class What { Option, Animation, Curve };
    struct Step {
        What what = What::Option;
        std::string name;
        std::optional<nlohmann::json> custom; // what Settings kept before
        nlohmann::json value;                 // what Hyprland used before
    };
    void loadAnimations();
    void effective(); // fills inherited animations from their parents
    std::string applyAnimation(const Animation& a);
    void remember(const Step& step);
    std::vector<Option> m_options;
    std::vector<Animation> m_animations;
    std::vector<Curve> m_curves;
    std::vector<Step> m_undo;
};

} // namespace m3
