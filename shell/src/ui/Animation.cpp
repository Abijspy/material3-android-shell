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

float Tween::springTime() const {
    if (motion::scale <= 0) return 1e6f; // animations off: settled at once
    return std::chrono::duration<float>(AnimClock::now() - m_start).count() / motion::scale;
}

// x'' = -k·d - 2ζ√k·x', d = x - target: the closed form, from (d0, v0)
void Tween::springAt(float t, float& d, float& v) const {
    const float w0 = std::sqrt(m_spring.stiffness), z = m_spring.damping;
    const float d0 = m_from - m_to, v0 = m_v0;
    if (z < 1) {
        const float wd = w0 * std::sqrt(1 - z * z);
        const float b = (v0 + z * w0 * d0) / wd;
        const float e = std::exp(-z * w0 * t), c = std::cos(wd * t), s = std::sin(wd * t);
        d = e * (d0 * c + b * s);
        v = e * (-z * w0 * (d0 * c + b * s) + (-d0 * wd * s + b * wd * c));
    } else {
        // Critically damped (the effects springs)
        const float b = v0 + w0 * d0;
        const float e = std::exp(-w0 * t);
        d = e * (d0 + b * t);
        v = e * (b - w0 * (d0 + b * t));
    }
}

float Tween::progress() const {
    const float duration = static_cast<float>(m_duration) * motion::scale;
    if (duration <= 0) return 1;
    const auto ms = std::chrono::duration<float, std::milli>(AnimClock::now() - m_start).count();
    return std::clamp(ms / duration, 0.0f, 1.0f);
}

void Tween::set(float target, bool animate) {
    if (target == m_to) return;
    if (m_springy) {
        // From where it is, at the speed it has
        float pos = value(), vel = 0;
        if (running()) {
            float d = 0;
            springAt(springTime(), d, vel);
            pos = m_to + d;
        }
        m_from = animate ? pos : target;
        m_v0 = animate ? vel : 0;
        m_to = target;
        m_start = AnimClock::now();
        return;
    }
    m_from = animate ? value() : target;
    m_to = target;
    m_start = AnimClock::now();
}

float Tween::value() const {
    if (m_springy) {
        if (!running()) return m_to;
        float d, v;
        springAt(springTime(), d, v);
        return m_to + d;
    }
    const float p = progress();
    return p >= 1 ? m_to : m_from + (m_to - m_from) * m_easing.at(p);
}

bool Tween::running() const {
    if (m_springy) {
        if (m_from == m_to && m_v0 == 0) return false;
        float d, v;
        springAt(springTime(), d, v);
        // Settled: within a thousandth of the move, and nearly still
        const float span = std::max(std::abs(m_from - m_to), 1e-3f);
        return std::abs(d) > span * 1e-3f || std::abs(v) > span * 1e-2f;
    }
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
    m_t = Tween(0, Spring::effectsFast());
    m_t.set(1, animate);
}

Color ColorTween::value() const {
    const float t = m_t.value();
    if (t >= 1) return m_to.resolve();
    return Color::mix(m_from.resolve(), m_to.resolve(), t);
}

} // namespace m3::ui
