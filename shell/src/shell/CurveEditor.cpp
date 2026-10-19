#include "shell/CurveEditor.h"

#include "render/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

namespace {

// One coordinate of the curve at parameter t, control values a and b
double cubic(double a, double b, double t) {
    const double u = 1 - t;
    return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
}

// Dots along a line, as the wavy indicators draw: round and anti-aliased
void line(Renderer& r, float x0, float y0, float x1, float y1, float width, const Color& c) {
    const float len = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, static_cast<int>(len / 1.5f));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
        r.rect({x - width / 2, y - width / 2, width, width}, c, Radii(width / 2));
    }
}

void drawCurve(Renderer& r, const Bezier& b, float left, float top, float w, float h, double yMin, double yMax, float width,
               const Color& c) {
    auto px = [&](double x, double y) {
        return std::pair<float, float>{left + static_cast<float>(x) * w,
                                       top + h - static_cast<float>((y - yMin) / (yMax - yMin)) * h};
    };
    // Sampled in t: x(t) bunches up where the curve is steep, so step finely
    auto [lx, ly] = px(0, 0);
    for (int i = 1; i <= 200; ++i) {
        const double t = i / 200.0;
        auto [x, y] = px(cubic(b[0], b[2], t), cubic(b[1], b[3], t));
        line(r, lx, ly, x, y, width, c);
        lx = x;
        ly = y;
    }
}

} // namespace

double bezierAt(const Bezier& b, double x) {
    x = std::clamp(x, 0.0, 1.0);
    // x(t) rises from 0 to 1 when both x control values lie in [0, 1]:
    // bisection finds t for this x
    double lo = 0, hi = 1, t = x;
    for (int i = 0; i < 40; ++i) {
        t = (lo + hi) / 2;
        if (cubic(b[0], b[2], t) < x) lo = t;
        else hi = t;
    }
    return cubic(b[1], b[3], t);
}

CurveEditor::CurveEditor(std::shared_ptr<Bezier> curve, float height) : m_curve(std::move(curve)), m_height(height) {}

CurveEditor::Frame CurveEditor::frame() const {
    const auto& b = rect();
    const auto& c = *m_curve;
    // Room for overshoot: the grid spans what the points need, at least 0–1
    const double yMin = std::min({0.0, c[1], c[3]}) - 0.1, yMax = std::max({1.0, c[1], c[3]}) + 0.1;
    const float pad = 22;
    return {b.x + pad, b.y + pad, b.w - pad * 2, b.h - pad * 2, yMin, yMax};
}

void CurveEditor::doPaint(Renderer& r) {
    const auto& b = rect();
    const auto& theme = Theme::get();
    r.rect(b, theme.color("surface_container_highest"), Radii(24));
    const Frame f = frame();
    auto py = [&](double y) { return f.top + f.h - static_cast<float>((y - f.yMin) / (f.yMax - f.yMin)) * f.h; };
    const Color grid = theme.color("on_surface").withAlpha(0.1f);
    // The 0 and 1 lines, and quarters across
    for (double y : {0.0, 1.0}) r.rect({f.left, py(y) - 0.5f, f.w, 1}, grid);
    for (int i = 0; i <= 4; ++i) r.rect({f.left + f.w * static_cast<float>(i) / 4 - 0.5f, f.top, 1, f.h}, grid.withAlpha(0.06f));

    const auto& c = *m_curve;
    const float x0 = f.left, y0 = py(0), x3 = f.left + f.w, y3 = py(1);
    const float p1x = f.left + static_cast<float>(c[0]) * f.w, p1y = py(c[1]);
    const float p2x = f.left + static_cast<float>(c[2]) * f.w, p2y = py(c[3]);
    // Handles from each end to its control point
    const Color handle = theme.color("on_surface_variant").withAlpha(0.6f);
    line(r, x0, y0, p1x, p1y, 2, handle);
    line(r, x3, y3, p2x, p2y, 2, handle);
    drawCurve(r, c, f.left, f.top, f.w, f.h, f.yMin, f.yMax, 4, theme.color("primary"));
    // Ends, then the draggable points (larger while held)
    for (auto [x, y] : {std::pair{x0, y0}, std::pair{x3, y3}})
        r.rect({x - 5, y - 5, 10, 10}, theme.color("on_surface_variant"), Radii(5));
    const float s0 = m_grab == 0 ? 26.0f : 20.0f, s1 = m_grab == 1 ? 26.0f : 20.0f;
    r.rect({p1x - s0 / 2, p1y - s0 / 2, s0, s0}, theme.color("primary"), Radii(s0 / 2));
    r.rect({p2x - s1 / 2, p2y - s1 / 2, s1, s1}, theme.color("tertiary"), Radii(s1 / 2));
}

void CurveEditor::onPress(uint32_t button, float x, float y) {
    if (button != BTN_LEFT) return;
    const Frame f = frame();
    auto py = [&](double v) { return f.top + f.h - static_cast<float>((v - f.yMin) / (f.yMax - f.yMin)) * f.h; };
    const auto& c = *m_curve;
    const float d0 = std::hypot(x - (f.left + static_cast<float>(c[0]) * f.w), y - py(c[1]));
    const float d1 = std::hypot(x - (f.left + static_cast<float>(c[2]) * f.w), y - py(c[3]));
    // The nearer point, if the press is anywhere near one
    m_grab = std::min(d0, d1) > 40 ? -1 : d0 <= d1 ? 0 : 1;
    if (m_grab >= 0) onDrag(x, y);
}

void CurveEditor::onDrag(float x, float y) {
    if (m_grab < 0) return;
    const Frame f = frame();
    // x stays within 0–1 (time only runs forward); y may overshoot
    const double cx = std::clamp(static_cast<double>((x - f.left) / f.w), 0.0, 1.0);
    const double cy = std::clamp(f.yMin + (f.yMax - f.yMin) * static_cast<double>((f.top + f.h - y) / f.h), -1.0, 2.0);
    auto& c = *m_curve;
    c[static_cast<size_t>(m_grab * 2)] = std::round(cx * 100) / 100;
    c[static_cast<size_t>(m_grab * 2 + 1)] = std::round(cy * 100) / 100;
    requestRedraw();
    if (onChanged) onChanged();
}

void CurveEditor::onRelease() {
    if (m_grab < 0) return;
    m_grab = -1;
    requestRedraw();
    if (onCommitted) onCommitted();
}

CurvePreview::CurvePreview(std::shared_ptr<Bezier> curve, std::function<double()> seconds)
    : m_curve(std::move(curve)), m_seconds(std::move(seconds)) {}

void CurvePreview::doPaint(Renderer& r) {
    const auto& b = rect();
    const auto& theme = Theme::get();
    const float cy = b.y + b.h / 2, dot = 24;
    r.rect({b.x, cy - 2, b.w, 4}, theme.color("on_surface").withAlpha(0.15f), Radii(2));
    // One run, then a pause, then the same run again
    const double run = std::max(0.05, m_seconds ? m_seconds() : 0.5), pause = 0.6;
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double at = std::fmod(now, run + pause);
    const double p = bezierAt(*m_curve, std::min(1.0, at / run));
    const float x = b.x + dot / 2 + static_cast<float>(p) * (b.w - dot);
    // The travelled stretch, then the dot
    r.rect({b.x, cy - 2, std::max(0.0f, x - b.x), 4}, theme.color("primary"), Radii(2));
    r.rect({x - dot / 2, cy - dot / 2, dot, dot}, theme.color("primary"), Radii(dot / 2));
    requestRedraw();
}

void CurveThumb::doPaint(Renderer& r) {
    const auto& b = rect();
    const auto& theme = Theme::get();
    r.rect(b, theme.color("surface_container_highest"), Radii(m_size / 4));
    const double yMin = std::min({0.0, m_curve[1], m_curve[3]}), yMax = std::max({1.0, m_curve[1], m_curve[3]});
    const float pad = m_size / 6;
    drawCurve(r, m_curve, b.x + pad, b.y + pad, b.w - pad * 2, b.h - pad * 2, yMin, yMax, 2.5f, theme.color("primary"));
}

} // namespace m3::shell
