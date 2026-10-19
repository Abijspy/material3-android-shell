#pragma once

#include "ui/Color.h"

#include <chrono>

namespace m3::ui {

// Material 3 motion: cubic-bezier easing curves and durations.
struct Easing {
    float x1, y1, x2, y2;
    float at(float t) const;

    static constexpr Easing standard() { return {0.2f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Easing emphasizedDecelerate() { return {0.05f, 0.7f, 0.1f, 1.0f}; }
    static constexpr Easing emphasizedAccelerate() { return {0.3f, 0.0f, 0.8f, 0.15f}; }
    // M3 Expressive spatial springs, as overshooting curves
    static constexpr Easing expressiveFast() { return {0.42f, 1.67f, 0.21f, 0.90f}; }
    static constexpr Easing expressiveDefault() { return {0.38f, 1.21f, 0.22f, 1.00f}; }
};

namespace motion {
inline constexpr int shortMs = 150;
inline constexpr int mediumMs = 300;
inline constexpr int longMs = 500;
// Settings → Appearance → Animation speed: durations are multiplied by this;
// 0 turns animation off
inline float scale = 1;
} // namespace motion

using AnimClock = std::chrono::steady_clock;

// A value that eases from where it is towards a target. Reading value()
// samples the curve at the current time, so there is no tick bookkeeping;
// running() tells the surface it needs another frame.
class Tween {
public:
    explicit Tween(float initial = 0, int durationMs = motion::mediumMs, Easing easing = Easing::emphasizedDecelerate())
        : m_from(initial), m_to(initial), m_duration(durationMs), m_easing(easing) {}

    void set(float target, bool animate = true);
    // For animations that vary their length (a fling, a wheel step)
    void setDuration(int ms) { m_duration = ms; }
    float value() const;
    float target() const { return m_to; }
    bool running() const;

private:
    float progress() const;

    float m_from, m_to;
    int m_duration;
    Easing m_easing;
    AnimClock::time_point m_start{};
};

// A colour transition between two palette references, re-resolved every
// frame so a theme change mid-animation still lands on the new colours.
class ColorTween {
public:
    explicit ColorTween(ColorRef initial = ColorRef::none(), int durationMs = motion::shortMs)
        : m_from(initial), m_to(initial), m_t(1, durationMs, Easing::standard()) {}

    void set(const ColorRef& target, bool animate = true);
    Color value() const;
    bool running() const { return m_t.running(); }

private:
    ColorRef m_from, m_to;
    Tween m_t;
};

} // namespace m3::ui
