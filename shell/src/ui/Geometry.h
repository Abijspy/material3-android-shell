#pragma once

#include <algorithm>

namespace m3 {

struct SizeF {
    float w = 0, h = 0;
};

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;

    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }

    RectF intersected(const RectF& o) const {
        const float l = std::max(x, o.x), t = std::max(y, o.y);
        const float r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return {l, t, std::max(0.0f, r - l), std::max(0.0f, b - t)};
    }
};

struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
    Insets() = default;
    Insets(float all) : left(all), top(all), right(all), bottom(all) {}
    Insets(float h, float v) : left(h), top(v), right(h), bottom(v) {}
    Insets(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
};

} // namespace m3
