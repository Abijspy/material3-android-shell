#pragma once

#include "ui/Node.h"

#include <algorithm>
#include <functional>
#include <memory>

// Clock faces shared by the desktop widgets, the lock screen and the login
// screen
namespace m3::shell::kit {

// Ticks once a second, on the second, while it lives
class SecondTicker {
public:
    explicit SecondTicker(std::function<void()> tick);
    ~SecondTicker();

private:
    void schedule();
    std::function<void()> m_tick;
    unsigned long long m_timer = 0;
};

// An analog face. Material You: a scalloped dial in the accent's container
// colour with thick, rounded hands and an orbiting seconds dot. Minimal: a
// ring of dots with slim hands and a thin seconds hand.
class AnalogFace : public ui::Node {
public:
    enum class Style { Cookie, Dots };
    AnalogFace(Style style, float size);

protected:
    // Up to `size`, smaller where it's given less (a lock screen cell)
    SizeF doMeasure(SizeF available) override {
        const float s = std::max(48.0f, std::min({m_size, available.w, available.h}));
        return {s, s};
    }
    void doPaint(Renderer& r) override;

private:
    Style m_style;
    float m_size;
    std::unique_ptr<SecondTicker> m_ticker;
};

} // namespace m3::shell::kit
