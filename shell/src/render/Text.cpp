#include "render/Text.h"

#include <cairo.h>
#include <cmath>
#include <format>
#include <pango/pangocairo.h>
#include <vector>

namespace m3 {

namespace {

PangoLayout* makeLayout(PangoContext* ctx, const std::string& text, const TextStyle& style, float scale) {
    PangoLayout* layout = pango_layout_new(ctx);
    PangoFontDescription* desc = pango_font_description_from_string(style.family.c_str());
    pango_font_description_set_absolute_size(desc, style.size * scale * PANGO_SCALE);
    pango_font_description_set_weight(desc, static_cast<PangoWeight>(style.weight));
    if (style.icon) {
        const auto axes = std::format("FILL={},wght={},GRAD=0,opsz={}", style.fill, style.weight,
                                      std::clamp(style.size, 20.0f, 48.0f));
        pango_font_description_set_variations(desc, axes.c_str());
    }
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    if (style.maxWidth > 0) {
        pango_layout_set_width(layout, static_cast<int>(style.maxWidth * scale * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        if (style.maxLines > 1) {
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_height(layout, -style.maxLines); // negative: a line count
        }
    }
    pango_layout_set_single_paragraph_mode(layout, style.maxLines <= 1);
    pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
    return layout;
}

} // namespace

TextEngine& TextEngine::get() {
    static TextEngine instance;
    return instance;
}

TextEngine::TextEngine() {
    m_now = m_lastSweep = std::chrono::steady_clock::now();
    auto* map = pango_cairo_font_map_get_default();
    m_measureContext = pango_font_map_create_context(map);
}

std::string TextEngine::key(const std::string& text, const TextStyle& style, float scale) const {
    return std::format("{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}\x1f{}", text, style.family, style.size, style.weight,
                       style.maxWidth, style.fill, scale, style.maxLines);
}

TextMetrics TextEngine::measure(const std::string& text, const TextStyle& style) {
    const auto k = key(text, style, 1);
    if (auto it = m_metrics.find(k); it != m_metrics.end()) return it->second;
    PangoLayout* layout = makeLayout(static_cast<PangoContext*>(m_measureContext), text, style, 1);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, nullptr, &logical);
    g_object_unref(layout);
    TextMetrics m{static_cast<float>(logical.width), static_cast<float>(logical.height)};
    if (m_metrics.size() > 32768) m_metrics.clear();
    return m_metrics[k] = m;
}

const TextEngine::Texture& TextEngine::texture(const std::string& text, const TextStyle& style, float scale) {
    const auto k = key(text, style, scale);
    auto& entry = m_textures[k];
    entry.lastUsed = m_now;
    if (entry.tex.id) return entry.tex;

    // Lay out at device scale so glyphs are hinted for the real pixel grid.
    // The context comes from the Cairo font map, so Cairo can draw its layouts.
    PangoLayout* layout = makeLayout(static_cast<PangoContext*>(m_measureContext), text, style, scale);
    PangoRectangle logical;
    pango_layout_get_pixel_extents(layout, nullptr, &logical);
    const int w = std::max(1, logical.width), h = std::max(1, logical.height);

    // ARGB32 rows are always width*4 bytes, which GLES2 can upload directly
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, -logical.x, -logical.y);
    pango_cairo_show_layout(cr, layout);
    cairo_surface_flush(surface);

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Only alpha is sampled, and it sits in the same byte for BGRA and RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surface));

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    entry.tex = Texture{id, {static_cast<float>(w) / scale, static_cast<float>(h) / scale}};
    return entry.tex;
}

void TextEngine::collect() {
    using namespace std::chrono;
    m_now = steady_clock::now();
    // Sweep once a second, not after every frame of every surface
    if (m_now - m_lastSweep < seconds(1)) return;
    m_lastSweep = m_now;
    // Unused for 30 s, or for 5 s once the cache grows past its budget
    const auto keep = m_textures.size() > 3000 ? seconds(5) : seconds(30);
    std::vector<GLuint> dead;
    std::erase_if(m_textures, [&](auto& kv) {
        if (m_now - kv.second.lastUsed < keep) return false;
        dead.push_back(kv.second.tex.id);
        return true;
    });
    if (!dead.empty()) glDeleteTextures(static_cast<GLsizei>(dead.size()), dead.data());
}

void TextEngine::clear() {
    for (auto& [_, e] : m_textures) glDeleteTextures(1, &e.tex.id);
    m_textures.clear();
    m_metrics.clear();
}

} // namespace m3
