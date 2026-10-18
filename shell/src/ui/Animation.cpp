#include "ui/Animation.h"

#include <algorithm>
#include <cmath>

namespace m3::ui {

float Easing::at(float t) const {
    // Solve bezier x(u) = t by Newton iterations, then evaluate y(u)
    auto bez = [](float a, float b, float u) {
        const float v = 1 - u;
        return 3 * v * v * u * a + 3 * v * u * u * b + u * u * u;
    };
    auto dbez = [](float a, float b, float u) {
        const float v = 1 - u;
        return 3 * v * v * a + 6 * v * u * (b - a) + 3 * u * u * (1 - b);
    };
    float u = t;
    for (int i = 0; i < 8; ++i) {
        const float d = dbez(x1, x2, u);
        if (std::abs(d) < 1e-5f) break;
        u = std::clamp(u - (bez(x1, x2, u) - t) / d, 0.0f, 1.0f);
    }
    return bez(y1, y2, u);
}

float Tween::progress() const {
    const float duration = static_cast<float>(m_duration) * motion::scale;
    if (duration <= 0) return 1;
    const auto ms = std::chrono::duration<float, std::milli>(AnimClock::now() - m_start).count();
    return std::clamp(ms / duration, 0.0f, 1.0f);
}

void Tween::set(float target, bool animate) {
    if (target == m_to) return;
    m_from = animate ? value() : target;
    m_to = target;
    m_start = AnimClock::now();
}

float Tween::value() const {
    const float p = progress();
    return p >= 1 ? m_to : m_from + (m_to - m_from) * m_easing.at(p);
}

bool Tween::running() const {
    return m_from != m_to && progress() < 1;
}

void ColorTween::set(const ColorRef& target, bool animate) {
    if (target.role == m_to.role && target.alpha == m_to.alpha && target.fixed == m_to.fixed) return;
    if (animate && running()) {
        // Freeze the in-flight blend as the new start
        m_from = ColorRef(value());
    } else {
        m_from = animate ? m_to : target;
    }
    m_to = target;
    m_t = Tween(0, motion::shortMs, Easing::standard());
    m_t.set(1, animate);
}

Color ColorTween::value() const {
    const float t = m_t.value();
    if (t >= 1) return m_to.resolve();
    return Color::mix(m_from.resolve(), m_to.resolve(), t);
}

} // namespace m3::ui
