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

    Signal<> changed;

private:
    Theme();
    void load();
    void notify();

    std::map<std::string, Color> m_palette;
};

} // namespace m3
