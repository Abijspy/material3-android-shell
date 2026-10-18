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
uniform sampler2D u_tex;
varying vec2 v_local;
void main() {
    float a = texture2D(u_tex, v_local / u_rect.zw).a;
    gl_FragColor = u_tint * a;
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
    if (!m_rectProg.id || !m_maskProg.id) return false;

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
    glUniform4f(p.rect, r.x, r.y, r.w, r.h);
    glUniform4f(p.radii, radii.tl, radii.tr, radii.br, radii.bl);
    premultiplied(p.fill, fill, o);
    premultiplied(p.border, border, o);
    glUniform1f(p.borderWidth, borderWidth);
    glUniform1f(p.px, 1.0f / m_scale);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::mask(GLuint texture, const RectF& r, const Color& tint) {
    const float o = m_opacity.back();
    if (!texture || tint.a * o <= 0) return;
    auto& p = m_maskProg;
    glUseProgram(p.id);
    glUniform2f(p.viewport, m_bufferWidth / m_scale, m_bufferHeight / m_scale);
    glUniform4f(p.rect, r.x, r.y, r.w, r.h);
    premultiplied(p.tint, tint, o);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(p.tex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::pushOpacity(float o) {
    m_opacity.push_back(m_opacity.back() * o);
}

void Renderer::popOpacity() {
    if (m_opacity.size() > 1) m_opacity.pop_back();
}

void Renderer::pushClip(const RectF& r) {
    m_clips.push_back(m_clips.empty() ? r : m_clips.back().intersected(r));
    applyClip();
}

void Renderer::popClip() {
    if (!m_clips.empty()) m_clips.pop_back();
    applyClip();
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
