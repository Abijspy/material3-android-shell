#pragma once

#include "core/Signal.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace m3 {

namespace wl {
struct Output;
}

// Night light: warmer colours (Settings → Display, Quick Settings), at
// /nightLight/temperature while /nightLight/enabled. On Hyprland through
// hyprsunset, as before; elsewhere (niri) the shell sets each screen's gamma
// itself (wlr-gamma-control), which goes back to normal when it stops.
class NightLight {
public:
    static NightLight& get();
    void start(); // as Settings left it (after the outputs are up)

    bool on() const;
    int temperature() const;
    void set(bool on);
    void setTemperature(int kelvin);

    // Screens came or went: each gets its own gamma
    void outputsChanged();
    void forget(wl::Output* output); // before it goes

    // The share of red, green and blue left at `kelvin` (6500: all of them)
    static void tint(int kelvin, double& r, double& g, double& b);

    Signal<> changed;

    struct Control; // a screen's gamma, held while it's warm

private:
    NightLight() = default;
    bool native() const; // the gamma ourselves, not hyprsunset
    void apply();
    void applyGamma(bool on);
    std::vector<std::unique_ptr<Control>> m_controls;
    int m_appliedOn = -1, m_appliedK = -1; // what's in effect
};

} // namespace m3
