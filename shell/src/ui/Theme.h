#pragma once

#include "ui/Color.h"

#include "core/Signal.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace m3 {

// Material 3 palette from Matugen's colors.json (roles such as "primary",
// "surface_container", "on_surface_variant"), falling back to the M3 baseline
// dark scheme. Also the shell's fonts and the bar metrics from config.json.
class Theme {
public:
    static Theme& get();

    Color color(const std::string& role) const;

    std::string fontFamily() const;
    std::string iconFamily() const { return "Material Symbols Rounded"; }
    std::string monoFamily() const;

    // Effects from config.json, cached on every reload (read while painting)
    bool blur() const { return m_blur; }
    float barOpacity() const { return m_barOpacity; }     // bar backgrounds
    float panelOpacity() const { return m_panelOpacity; } // popups, toasts, OSD
    float roundness() const { return m_roundness; }       // multiplies corner radii

    Signal<> changed;

private:
    Theme();
    void load();
    void notify();
    void loadEffects();

    std::map<std::string, Color> m_palette;
    bool m_blur = true;
    float m_barOpacity = 0.8f, m_panelOpacity = 0.9f, m_roundness = 1;
};

} // namespace m3
