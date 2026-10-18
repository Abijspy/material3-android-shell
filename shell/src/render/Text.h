#pragma once

#include "ui/Geometry.h"

#include <GLES2/gl2.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace m3 {

struct TextStyle {
    std::string family;
    float size = 14;     // logical px
    int weight = 400;
    float maxWidth = 0;  // > 0 ellipsizes at the end (or wraps, with maxLines > 1)
    int maxLines = 1;
    // Material Symbols axes; used when the family is the icon font
    float fill = 0;
    bool icon = false;

    bool operator==(const TextStyle&) const = default;
};

struct TextMetrics {
    float width = 0;
    float height = 0;
};

// Pango lays text out, Cairo rasterises it to an alpha mask at the surface's
// scale, and the mask is cached as a GL texture keyed by text, style and
// scale. Unused textures are evicted after a few frames.
class TextEngine {
public:
    struct Texture {
        GLuint id = 0;
        TextMetrics logical; // size in logical px
    };

    static TextEngine& get();

    TextMetrics measure(const std::string& text, const TextStyle& style);
    const Texture& texture(const std::string& text, const TextStyle& style, float scale);

    // Called after each frame
    void collect();
    // Fonts or config changed: drop everything
    void clear();

private:
    TextEngine();

    struct Entry {
        Texture tex;
        uint64_t lastUsed = 0;
    };

    std::string key(const std::string& text, const TextStyle& style, float scale) const;

    std::unordered_map<std::string, TextMetrics> m_metrics;
    std::unordered_map<std::string, Entry> m_textures;
    uint64_t m_frame = 0;
    void* m_measureContext = nullptr; // PangoContext*
};

} // namespace m3
