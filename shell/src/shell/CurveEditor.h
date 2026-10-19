#pragma once

#include "ui/Node.h"

#include <array>
#include <functional>
#include <memory>

namespace m3::shell {

// A cubic bezier from (0,0) to (1,1): the two control points, as Hyprland's
// curves (and CSS's cubic-bezier) take them
using Bezier = std::array<double, 4>; // x0 y0 x1 y1

// y at time x (0–1) along the curve, solving x(t) = x first
double bezierAt(const Bezier& b, double x);

// The curve on a grid, its two control points draggable. Overshoot shows:
// the grid grows to take points above 1 or below 0.
class CurveEditor : public ui::Node {
public:
    explicit CurveEditor(std::shared_ptr<Bezier> curve, float height = 240);

    std::function<void()> onChanged;   // while dragging
    std::function<void()> onCommitted; // released

    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, m_height}; }
    void doPaint(Renderer& r) override;

private:
    struct Frame {
        float left, top, w, h;
        double yMin, yMax;
    };
    Frame frame() const;
    std::shared_ptr<Bezier> m_curve;
    float m_height;
    int m_grab = -1; // 0: first point, 1: second
};

// The curve in motion: a dot travelling a track on the curve's timing, over
// and over, with a pause at each end
class CurvePreview : public ui::Node {
public:
    explicit CurvePreview(std::shared_ptr<Bezier> curve, std::function<double()> seconds);

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 36}; }
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return true; }

private:
    std::shared_ptr<Bezier> m_curve;
    std::function<double()> m_seconds; // one run's length
};

// A small static drawing of a curve (lists of curves)
class CurveThumb : public ui::Node {
public:
    explicit CurveThumb(Bezier curve, float size = 40) : m_curve(curve), m_size(size) {}

protected:
    SizeF doMeasure(SizeF) override { return {m_size, m_size}; }
    void doPaint(Renderer& r) override;

private:
    Bezier m_curve;
    float m_size;
};

} // namespace m3::shell
