#pragma once

#include "ui/Color.h"
#include "ui/Geometry.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <vector>

struct wl_display;
struct wl_egl_window;

namespace m3 {

// Per-corner radii, clockwise from top-left
struct Radii {
    float tl = 0, tr = 0, br = 0, bl = 0;
    Radii() = default;
    Radii(float all) : tl(all), tr(all), br(all), bl(all) {}
    Radii(float a, float b, float c, float d) : tl(a), tr(b), br(c), bl(d) {}
};

// One EGL context shared by every surface, and the two programs the UI needs:
// an anti-aliased rounded rectangle (fill + border, SDF) and an alpha-mask
// textured quad for text and icons. Coordinates are logical pixels.
class Renderer {
public:
    static Renderer& get();

    bool init(wl_display* display);
    bool ready() const { return m_context != EGL_NO_CONTEXT; }

    EGLSurface createSurface(wl_egl_window* window);
    void destroySurface(EGLSurface surface);

    void begin(EGLSurface surface, int bufferWidth, int bufferHeight, float scale);
    void end(EGLSurface surface);

    void rect(const RectF& r, const Color& fill, Radii radii = {}, float borderWidth = 0, Color border = {});
    void mask(GLuint texture, const RectF& r, const Color& tint);

    // Multiplies every colour drawn until popped (fades, disabled states)
    void pushOpacity(float o);
    void popOpacity();

    void pushClip(const RectF& r);
    void popClip();

    float scale() const { return m_scale; }

private:
    Renderer() = default;
    bool buildPrograms();
    void applyClip();

    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig m_config = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;

    struct RectProgram {
        GLuint id = 0;
        GLint viewport, rect, radii, fill, border, borderWidth, px;
    } m_rectProg;
    struct MaskProgram {
        GLuint id = 0;
        GLint viewport, rect, tint, tex;
    } m_maskProg;
    GLuint m_quad = 0;

    int m_bufferWidth = 0, m_bufferHeight = 0;
    float m_scale = 1;
    std::vector<float> m_opacity{1.0f};
    std::vector<RectF> m_clips;
};

} // namespace m3
