#include "shell/ClockFaces.h"

#include "core/EventLoop.h"
#include "render/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace m3::shell::kit {

using namespace m3::ui;

namespace {
float g_itemScale = 1;
}

float widgetScale() {
    return Theme::get().areaScale(Theme::AreaWidgets) * g_itemScale;
}

WidgetScaleScope::WidgetScaleScope(float scale) : m_previous(g_itemScale) {
    g_itemScale = scale;
}

WidgetScaleScope::~WidgetScaleScope() {
    g_itemScale = m_previous;
}

SecondTicker::SecondTicker(std::function<void()> tick) : m_tick(std::move(tick)) {
    schedule();
}

SecondTicker::~SecondTicker() {
    if (m_timer) EventLoop::get().cancel(m_timer);
}

void SecondTicker::schedule() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count() % 1000;
    m_timer = EventLoop::get().addTimer(static_cast<int>(1000 - ms) + 5, [this] {
        m_timer = 0;
        if (m_tick) m_tick();
        schedule();
    });
}

AnalogFace::AnalogFace(Style style, float size) : m_style(style), m_size(size) {
    m_ticker = std::make_unique<SecondTicker>([this] { requestRedraw(); });
}

void AnalogFace::doPaint(Renderer& r) {
    const auto& b = rect();
    const float cx = b.x + b.w / 2, cy = b.y + b.h / 2, R = std::min(b.w, b.h) / 2;
    auto& theme = Theme::get();
    constexpr float tau = 2 * static_cast<float>(M_PI);
    auto dot = [&](float x, float y, float d, const Color& c) { r.rect({x - d / 2, y - d / 2, d, d}, c, Radii(d / 2)); };
    // A rounded stroke from the centre (a little behind it) out at angle a
    auto hand = [&](float a, float length, float thick, const Color& c) {
        const float dx = std::sin(a), dy = -std::cos(a);
        for (float t = -thick * 0.4f; t <= length; t += 0.75f) dot(cx + dx * t, cy + dy * t, thick, c);
    };
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    const float sec = static_cast<float>(tm.tm_sec);
    const float min = static_cast<float>(tm.tm_min) + sec / 60;
    const float hour = static_cast<float>(tm.tm_hour % 12) + min / 60;

    if (m_style == Style::Cookie) {
        // Twelve bumps round a disc: the scalloped "cookie" of Android's clock
        const Color face = theme.color("primary_container");
        const float bump = R * 0.2f;
        dot(cx, cy, (R - bump * 0.55f) * 2, face);
        for (int i = 0; i < 12; ++i) {
            const float a = tau * static_cast<float>(i) / 12;
            dot(cx + std::sin(a) * (R - bump), cy - std::cos(a) * (R - bump), bump * 2, face);
        }
        hand(tau * hour / 12, R * 0.48f, R * 0.15f, theme.color("primary"));
        hand(tau * min / 60, R * 0.7f, R * 0.1f, theme.color("tertiary"));
        // The seconds: a dot circling just inside the edge
        const float sa = tau * sec / 60;
        dot(cx + std::sin(sa) * R * 0.8f, cy - std::cos(sa) * R * 0.8f, R * 0.08f, theme.color("on_primary_container"));
        dot(cx, cy, R * 0.07f, theme.color("on_primary"));
    } else {
        dot(cx, cy, R * 2, theme.color("surface").withAlpha(0.45f));
        for (int i = 0; i < 12; ++i) {
            const float a = tau * static_cast<float>(i) / 12;
            const bool quarter = i % 3 == 0;
            dot(cx + std::sin(a) * R * 0.84f, cy - std::cos(a) * R * 0.84f, R * (quarter ? 0.075f : 0.04f),
                theme.color(quarter ? "primary" : "on_surface_variant"));
        }
        hand(tau * hour / 12, R * 0.45f, R * 0.06f, theme.color("on_surface"));
        hand(tau * min / 60, R * 0.66f, R * 0.04f, theme.color("on_surface"));
        hand(tau * sec / 60, R * 0.74f, R * 0.018f, theme.color("primary"));
        dot(cx, cy, R * 0.08f, theme.color("primary"));
    }
}

} // namespace m3::shell::kit
