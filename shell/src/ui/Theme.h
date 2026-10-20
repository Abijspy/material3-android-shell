#pragma once

#include "ui/Color.h"

#include "core/Signal.h"

#include <functional>
#include <cstdint>
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
    float textScale() const { return m_textScale; }       // Accessibility → Text size
    // Text sizes of parts of the shell (Look & feel → Text size), times the
    // shell's: a node's textArea picks one
    enum TextArea : uint8_t { AreaNone, AreaBar, AreaLock, AreaWidgets, AreaCount };
    float areaScale(uint8_t area) const { return area < AreaCount ? m_areaScale[area] : 1; }
    bool boldText() const { return m_boldText; }          // Accessibility → Bold text
    float widgetBlur() const { return m_widgetBlur; }     // radius behind desktop and lock widgets; 0: off
    // Scrolling (Look & feel → Scrolling): eased wheel steps, their length,
    // and how far a touchpad flick glides on (0: not at all)
    bool smoothScroll() const { return m_smoothScroll; }
    float scrollStep() const { return m_scrollStep; }
    float scrollGlide() const { return m_scrollGlide; }

    Signal<> changed;

private:
    Theme();
    void load();
    void notify();
    void loadEffects();

    std::map<std::string, Color> m_palette;
    bool m_blur = true;
    float m_barOpacity = 0.8f, m_panelOpacity = 0.9f, m_roundness = 1, m_textScale = 1;
    bool m_boldText = false;
    float m_areaScale[AreaCount] = {1, 1, 1, 1};
    float m_widgetBlur = 6;
    bool m_smoothScroll = true;
    float m_scrollStep = 64, m_scrollGlide = 1;
};

} // namespace m3
