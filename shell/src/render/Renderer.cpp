#include "render/Renderer.h"

#include "core/Log.h"

#include <EGL/eglext.h>
#include <cmath>
#include <wayland-egl.h>

namespace m3 {

namespace {

// Both programs draw a unit quad stretched over `rect` (logical px).
const char* kVertex = R"(
attribute vec2 a_pos;
uniform vec2 u_viewport;
uniform vec4 u_rect;
varying vec2 v_local;
void main() {
    vec2 p = u_rect.xy + a_pos * u_rect.zw;
    v_local = a_pos * u_rect.zw;
    gl_Position = vec4(p / u_viewport * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
)";

// Signed distance to a rounded box with a radius per quadrant; coverage from
// the distance gives 1px anti-aliasing at any scale.
const char* kRectFragment = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec4 u_rect;
uniform vec4 u_radii;   // tl tr br bl
uniform vec4 u_fill;    // premultiplied
uniform vec4 u_border;  // premultiplied
uniform float u_border_width;
uniform float u_px;     // logical size of one device pixel
varying vec2 v_local;

float sdRoundBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 halfSize = u_rect.zw * 0.5;
    vec2 p = v_local - halfSize;
    float r = p.x < 0.0 ? (p.y < 0.0 ? u_radii.x : u_radii.w) : (p.y < 0.0 ? u_radii.y : u_radii.z);
    r = min(r, min(halfSize.x, halfSize.y));
    float d = sdRoundBox(p, halfSize, r);
    float outer = clamp(0.5 - d / u_px, 0.0, 1.0);
    vec4 color = u_fill;
    if (u_border_width > 0.0) {
        float inner = clamp(0.5 - (d + u_border_width) / u_px, 0.0, 1.0);
        color = mix(u_border, u_fill, inner);
    }
    gl_FragColor = color * outer;
}
)";

const char* kMaskFragment = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec4 u_rect;
uniform vec4 u_tint;    // premultiplied
uniform float u_turn;   // 1: a quarter turn clockwise (text up a side bar)
uniform sampler2D u_tex;
varying vec2 v_local;
void main() {
    vec2 uv = v_local / u_rect.zw;
    if (u_turn > 0.5) uv = vec2(uv.y, 1.0 - uv.x);
    float a = texture2D(u_tex, uv).a;
    gl_FragColor = u_tint * a;
}
)";

const char* kImageFragment = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec4 u_rect;
uniform float u_opacity;
uniform vec4 u_radii;   // tl tr br bl; 0: square corners
uniform float u_px;
uniform float u_turn;   // as the mask's
uniform sampler2D u_tex;
varying vec2 v_local;

float sdRoundBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    // Rounded like a rect (album art filling a card), anti-aliased the same way
    vec2 halfSize = u_rect.zw * 0.5;
    vec2 p = v_local - halfSize;
    float r = p.x < 0.0 ? (p.y < 0.0 ? u_radii.x : u_radii.w) : (p.y < 0.0 ? u_radii.y : u_radii.z);
    r = min(r, min(halfSize.x, halfSize.y));
    float coverage = clamp(0.5 - sdRoundBox(p, halfSize, r) / u_px, 0.0, 1.0);
    vec2 uv = v_local / u_rect.zw;
    if (u_turn > 0.5) uv = vec2(uv.y, 1.0 - uv.x);
    // Cairo stores premultiplied ARGB32 as BGRA bytes on little-endian
    gl_FragColor = texture2D(u_tex, uv).bgra * u_opacity * coverage;
}
)";

// One direction of a Gaussian (the blur is two passes: across, then down)
// whose radius grows towards the strong edge. Textures hold framebuffer
// rows, bottom-up; v_local runs top-down.
const char* kBlurFragment = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec4 u_rect;
uniform sampler2D u_tex;
uniform vec2 u_step;     // one texture pixel along this pass's direction, in uv
uniform float u_radius;  // device px at the strong edge
uniform float u_top;     // 1: strong at the top, 0: at the bottom, 2: even
varying vec2 v_local;
void main() {
    vec2 uv = v_local / u_rect.zw;
    float s = mix(uv.y, 1.0 - uv.y, min(u_top, 1.0));
    s = u_top > 1.5 ? 1.0 : s * s * (3.0 - 2.0 * s);
    float sigma = max(u_radius * s * 0.5, 0.001);
    vec2 at = vec2(uv.x, 1.0 - uv.y);
    // 33 taps over ±3 sigma; linear filtering fills between them
    float spacing = max(sigma * 3.0 / 16.0, 1.0);
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int i = -16; i <= 16; i++) {
        float d = float(i) * spacing;
        float w = exp(-0.5 * d * d / (sigma * sigma));
        sum += texture2D(u_tex, at + u_step * d) * w;
        total += w;
    }
    gl_FragColor = sum / total;
}
)";

// A blurred copy of the frame (framebuffer rows, bottom-up, RGBA) drawn back
// with rounded corners, behind a widget's card
const char* kBackdropFragment = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform vec4 u_rect;
uniform vec4 u_radii;
uniform float u_px;
uniform sampler2D u_tex;
varying vec2 v_local;

float sdRoundBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 halfSize = u_rect.zw * 0.5;
    vec2 p = v_local - halfSize;
    float r = p.x < 0.0 ? (p.y < 0.0 ? u_radii.x : u_radii.w) : (p.y < 0.0 ? u_radii.y : u_radii.z);
    r = min(r, min(halfSize.x, halfSize.y));
    float coverage = clamp(0.5 - sdRoundBox(p, halfSize, r) / u_px, 0.0, 1.0);
    vec2 uv = v_local / u_rect.zw;
    gl_FragColor = texture2D(u_tex, vec2(uv.x, 1.0 - uv.y)) * coverage;
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof buf, nullptr, buf);
        log::error("shader: {}", buf);
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(p, 0, "a_pos");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetProgramInfoLog(p, sizeof buf, nullptr, buf);
        log::error("program: {}", buf);
        return 0;
    }
    return p;
}

void premultiplied(GLint loc, const Color& c, float opacity) {
    const float a = c.a * opacity;
    glUniform4f(loc, c.r * a, c.g * a, c.b * a, a);
}

} // namespace

Renderer& Renderer::get() {
    static Renderer instance;
    return instance;
}

bool Renderer::init(wl_display* display) {
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    m_display = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, nullptr)
                                   : eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display));
    if (m_display == EGL_NO_DISPLAY || !eglInitialize(m_display, nullptr, nullptr)) {
        log::error("EGL initialisation failed");
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(m_display, attrs, &m_config, 1, &count) || count == 0) {
        log::error("no RGBA8 EGL config");
        return false;
    }
    const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctxAttrs);
    if (m_context == EGL_NO_CONTEXT) {
        log::error("cannot create a GLES2 context");
        return false;
    }
    // Surfaceless current context so programs can be built before any surface exists
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
    return buildPrograms();
}

bool Renderer::buildPrograms() {
    m_rectProg.id = link(kVertex, kRectFragment);
    m_maskProg.id = link(kVertex, kMaskFragment);
    m_imageProg.id = link(kVertex, kImageFragment);
    m_blurProg.id = link(kVertex, kBlurFragment);
    m_backdropProg.id = link(kVertex, kBackdropFragment);
    if (auto& d = m_backdropProg; d.id) {
        d.viewport = glGetUniformLocation(d.id, "u_viewport");
        d.rect = glGetUniformLocation(d.id, "u_rect");
        d.radii = glGetUniformLocation(d.id, "u_radii");
        d.px = glGetUniformLocation(d.id, "u_px");
        d.tex = glGetUniformLocation(d.id, "u_tex");
    }
    if (!m_rectProg.id || !m_maskProg.id || !m_imageProg.id) return false;

    auto& b = m_blurProg;
    if (b.id) {
        b.viewport = glGetUniformLocation(b.id, "u_viewport");
        b.rect = glGetUniformLocation(b.id, "u_rect");
        b.tex = glGetUniformLocation(b.id, "u_tex");
        b.step = glGetUniformLocation(b.id, "u_step");
        b.radius = glGetUniformLocation(b.id, "u_radius");
        b.top = glGetUniformLocation(b.id, "u_top");
    }

    auto& im = m_imageProg;
    im.viewport = glGetUniformLocation(im.id, "u_viewport");
    im.rect = glGetUniformLocation(im.id, "u_rect");
    im.opacity = glGetUniformLocation(im.id, "u_opacity");
    im.radii = glGetUniformLocation(im.id, "u_radii");
    im.px = glGetUniformLocation(im.id, "u_px");
    im.tex = glGetUniformLocation(im.id, "u_tex");
    im.turn = glGetUniformLocation(im.id, "u_turn");

    auto& r = m_rectProg;
    r.viewport = glGetUniformLocation(r.id, "u_viewport");
    r.rect = glGetUniformLocation(r.id, "u_rect");
    r.radii = glGetUniformLocation(r.id, "u_radii");
    r.fill = glGetUniformLocation(r.id, "u_fill");
    r.border = glGetUniformLocation(r.id, "u_border");
    r.borderWidth = glGetUniformLocation(r.id, "u_border_width");
    r.px = glGetUniformLocation(r.id, "u_px");

    auto& m = m_maskProg;
    m.viewport = glGetUniformLocation(m.id, "u_viewport");
    m.rect = glGetUniformLocation(m.id, "u_rect");
    m.tint = glGetUniformLocation(m.id, "u_tint");
    m.tex = glGetUniformLocation(m.id, "u_tex");
    m.turn = glGetUniformLocation(m.id, "u_turn");

    const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &m_quad);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    return true;
}

EGLSurface Renderer::createSurface(wl_egl_window* window) {
    return eglCreateWindowSurface(m_display, m_config, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
}

void Renderer::destroySurface(EGLSurface surface) {
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, m_context);
    eglDestroySurface(m_display, surface);
}

void Renderer::begin(EGLSurface surface, int bufferWidth, int bufferHeight, float scale) {
    eglMakeCurrent(m_display, surface, surface, m_context);
    // Frame pacing comes from wl_surface.frame, never from a blocking swap
    eglSwapInterval(m_display, 0);
    m_bufferWidth = bufferWidth;
    m_bufferHeight = bufferHeight;
    m_scale = scale;
    m_opacity = {1.0f};
    m_clips.clear();
    m_xforms = {Xform{}};

    glViewport(0, 0, bufferWidth, bufferHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ARRAY_BUFFER, m_quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
}

void Renderer::end(EGLSurface surface) {
    eglSwapBuffers(m_display, surface);
}

void Renderer::rect(const RectF& r, const Color& fill, Radii radii, float borderWidth, Color border) {
    const float o = m_opacity.back();
    if (r.w <= 0 || r.h <= 0 || (fill.a * o <= 0 && (borderWidth <= 0 || border.a * o <= 0))) return;
    auto& p = m_rectProg;
    glUseProgram(p.id);
    glUniform2f(p.viewport, m_bufferWidth / m_scale, m_bufferHeight / m_scale);
    const auto& xf = m_xforms.back();
    const RectF m = xf.map(r);
    const Radii rr = scaled(radii, xf.s);
    glUniform4f(p.rect, m.x, m.y, m.w, m.h);
    glUniform4f(p.radii, rr.tl, rr.tr, rr.br, rr.bl);
    premultiplied(p.fill, fill, o);
    premultiplied(p.border, border, o);
    glUniform1f(p.borderWidth, borderWidth * xf.s);
    glUniform1f(p.px, 1.0f / m_scale);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::erase(const RectF& r, Radii radii) {
    if (r.w <= 0 || r.h <= 0) return;
    // What's drawn keeps (1 − coverage) of itself: a hole as smooth at its
    // edges as a rect is
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    m_opacity.push_back(1);
    rect(r, Color{0, 0, 0, 1}, radii);
    m_opacity.pop_back();
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::mask(GLuint texture, const RectF& r, const Color& tint, bool turned) {
    const float o = m_opacity.back();
    if (!texture || tint.a * o <= 0) return;
    auto& p = m_maskProg;
    glUseProgram(p.id);
    glUniform2f(p.viewport, m_bufferWidth / m_scale, m_bufferHeight / m_scale);
    const RectF m = m_xforms.back().map(r);
    glUniform4f(p.rect, m.x, m.y, m.w, m.h);
    premultiplied(p.tint, tint, o);
    glUniform1f(p.turn, turned ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(p.tex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::image(GLuint texture, const RectF& r, float opacity, Radii radii, bool turned) {
    const float o = m_opacity.back() * opacity;
    if (!texture || o <= 0) return;
    auto& p = m_imageProg;
    glUseProgram(p.id);
    glUniform2f(p.viewport, m_bufferWidth / m_scale, m_bufferHeight / m_scale);
    const auto& xf = m_xforms.back();
    const RectF m = xf.map(r);
    const Radii rr = scaled(radii, xf.s);
    glUniform4f(p.rect, m.x, m.y, m.w, m.h);
    glUniform1f(p.opacity, o);
    glUniform4f(p.radii, rr.tl, rr.tr, rr.br, rr.bl);
    glUniform1f(p.px, 1.0f / m_scale);
    glUniform1f(p.turn, turned ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(p.tex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::blurEdge(const RectF& r, float radius, bool strongAtTop) {
    if (!m_blurProg.id || radius <= 0.25f || r.w <= 0 || r.h <= 0) return;
    const RectF m = m_xforms.back().map(r);
    // Whole device pixels inside the buffer, so the copy lines up exactly
    const int x0 = std::max(0, static_cast<int>(std::floor(m.x * m_scale)));
    const int y0 = std::max(0, static_cast<int>(std::floor(m.y * m_scale)));
    const int x1 = std::min(m_bufferWidth, static_cast<int>(std::ceil(m.right() * m_scale)));
    const int y1 = std::min(m_bufferHeight, static_cast<int>(std::ceil(m.bottom() * m_scale)));
    const int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    auto texture = [](GLuint& t) {
        if (t) return;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    texture(m_backdrop);
    texture(m_blurred);
    if (!m_blurFbo) glGenFramebuffers(1, &m_blurFbo);

    // What has been drawn under the strip so far
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_backdrop);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x0, m_bufferHeight - y1, w, h, 0);

    auto& p = m_blurProg;
    glUseProgram(p.id);
    glUniform1i(p.tex, 0);
    glUniform1f(p.radius, radius * m_scale);
    glUniform1f(p.top, strongAtTop ? 1.0f : 0.0f);
    // Replace, not blend: each pass writes the whole strip
    glDisable(GL_BLEND);

    // Across, into the second texture (same size, same row order)
    glBindTexture(GL_TEXTURE_2D, m_blurred);
    if (m_blurredW != w || m_blurredH != h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_blurredW = w;
        m_blurredH = h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurred, 0);
    const bool fbo = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (fbo) {
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, m_backdrop);
        glUniform2f(p.viewport, static_cast<float>(w), static_cast<float>(h));
        glUniform4f(p.rect, 0, 0, static_cast<float>(w), static_cast<float>(h));
        glUniform2f(p.step, 1.0f / static_cast<float>(w), 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_bufferWidth, m_bufferHeight);

    // Down, back onto the frame where the strip came from (untranslated:
    // the copy already sits there)
    glBindTexture(GL_TEXTURE_2D, fbo ? m_blurred : m_backdrop);
    glUniform2f(p.viewport, m_bufferWidth / m_scale, m_bufferHeight / m_scale);
    glUniform4f(p.rect, x0 / m_scale, y0 / m_scale, w / m_scale, h / m_scale);
    glUniform2f(p.step, 0, 1.0f / static_cast<float>(h));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_BLEND);
}

void Renderer::blurBackdrop(const RectF& r, float radius, Radii radii) {
    if (!m_blurProg.id || !m_backdropProg.id || radius <= 0.25f || r.w <= 0 || r.h <= 0) return;
    const RectF m = m_xforms.back().map(r);
    const int x0 = std::max(0, static_cast<int>(std::floor(m.x * m_scale)));
    const int y0 = std::max(0, static_cast<int>(std::floor(m.y * m_scale)));
    const int x1 = std::min(m_bufferWidth, static_cast<int>(std::ceil(m.right() * m_scale)));
    const int y1 = std::min(m_bufferHeight, static_cast<int>(std::ceil(m.bottom() * m_scale)));
    const int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;

    auto texture = [w, h](GLuint& t, int& tw, int& th) {
        if (!t) {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, t);
        if (tw != w || th != h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            tw = w;
            th = h;
        }
    };
    int bw = 0, bh = 0; // the copy is sized by glCopyTexImage2D itself
    texture(m_backdrop, bw, bh);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, x0, m_bufferHeight - y1, w, h, 0);
    texture(m_blurred, m_blurredW, m_blurredH);
    texture(m_blurred2, m_blurred2W, m_blurred2H);
    if (!m_blurFbo) glGenFramebuffers(1, &m_blurFbo);

    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    auto& p = m_blurProg;
    glUseProgram(p.id);
    glUniform1i(p.tex, 0);
    glUniform1f(p.radius, radius * m_scale);
    glUniform1f(p.top, 2.0f); // even all over
    glUniform2f(p.viewport, static_cast<float>(w), static_cast<float>(h));
    glUniform4f(p.rect, 0, 0, static_cast<float>(w), static_cast<float>(h));
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo);
    glViewport(0, 0, w, h);
    bool ok = true;
    // Across, then down: backdrop → blurred → blurred2
    for (auto [from, to, step] : {std::tuple{m_backdrop, m_blurred, 0}, std::tuple{m_blurred, m_blurred2, 1}}) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            ok = false;
            break;
        }
        glBindTexture(GL_TEXTURE_2D, from);
        glUniform2f(p.step, step ? 0 : 1.0f / static_cast<float>(w), step ? 1.0f / static_cast<float>(h) : 0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_bufferWidth, m_bufferHeight);
    glEnable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (!ok) return;

    // Back where it came from, rounded like the card over it
    auto& d = m_backdropProg;
    glUseProgram(d.id);
    glUniform2f(d.viewport, m_bufferWidth / m_scale, m_bufferHeight / m_scale);
    glUniform4f(d.rect, x0 / m_scale, y0 / m_scale, w / m_scale, h / m_scale);
    glUniform4f(d.radii, radii.tl, radii.tr, radii.br, radii.bl);
    glUniform1f(d.px, 1.0f / m_scale);
    glBindTexture(GL_TEXTURE_2D, m_blurred2);
    glUniform1i(d.tex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::pushOpacity(float o) {
    m_opacity.push_back(m_opacity.back() * o);
}

void Renderer::popOpacity() {
    if (m_opacity.size() > 1) m_opacity.pop_back();
}

void Renderer::pushTranslate(float dx, float dy) {
    Xform x = m_xforms.back();
    x.x += dx * x.s;
    x.y += dy * x.s;
    m_xforms.push_back(x);
}

void Renderer::pushScale(float cx, float cy, float factor) {
    // p → c + (p − c)·k, then the transform already there
    Xform x = m_xforms.back();
    x.x += x.s * cx * (1 - factor);
    x.y += x.s * cy * (1 - factor);
    x.s *= factor;
    m_xforms.push_back(x);
}

void Renderer::popTranslate() {
    if (m_xforms.size() > 1) m_xforms.pop_back();
}

void Renderer::pushClip(const RectF& rect) {
    // Clips are given in layout coordinates; apply the current transform
    const RectF r = m_xforms.back().map(rect);
    m_clips.push_back(m_clips.empty() ? r : m_clips.back().intersected(r));
    applyClip();
}

void Renderer::popClip() {
    if (!m_clips.empty()) m_clips.pop_back();
    applyClip();
}

bool Renderer::visible(const RectF& rect) const {
    if (m_clips.empty()) return true;
    const RectF m = m_xforms.back().map(rect);
    const auto& c = m_clips.back();
    // A little slack for borders and focus rings drawn just outside a rect
    constexpr float slack = 8;
    return m.x - slack < c.right() && m.right() + slack > c.x && m.y - slack < c.bottom() && m.bottom() + slack > c.y;
}

void Renderer::applyClip() {
    if (m_clips.empty()) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const auto& c = m_clips.back();
    glEnable(GL_SCISSOR_TEST);
    // GL's origin is bottom-left, in buffer pixels
    glScissor(static_cast<GLint>(std::floor(c.x * m_scale)),
              static_cast<GLint>(std::floor(m_bufferHeight - c.bottom() * m_scale)),
              static_cast<GLsizei>(std::ceil(c.w * m_scale)), static_cast<GLsizei>(std::ceil(c.h * m_scale)));
}

} // namespace m3
