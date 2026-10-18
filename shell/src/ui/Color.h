#pragma once

#include <algorithm>
#include <optional>
#include <string>

namespace m3 {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static std::optional<Color> fromHex(const std::string& hex);

    Color withAlpha(float alpha) const { return {r, g, b, a * alpha}; }

    static Color mix(const Color& x, const Color& y, float t) {
        return {x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t};
    }

    bool operator==(const Color&) const = default;
};

// A colour as widgets store it: a Material 3 role resolved against the live
// palette at paint time (so wallpaper changes recolour everything), or a
// fixed value.
struct ColorRef {
    std::string role;
    float alpha = 1;
    std::optional<Color> fixed;

    ColorRef() = default;
    ColorRef(const char* r, float a = 1) : role(r), alpha(a) {}
    ColorRef(std::string r, float a = 1) : role(std::move(r)), alpha(a) {}
    ColorRef(Color c) : fixed(c) {}

    static ColorRef none() { return ColorRef(Color{0, 0, 0, 0}); }
    Color resolve() const;
};

} // namespace m3
