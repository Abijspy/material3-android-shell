#include "wayland/LayerSurface.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "wayland/Display.h"

#include "wayland/LayerShellProtocol.h"

#include <cstdlib>
#include <wayland-client.h>
#include <wayland-egl.h>

namespace m3::wl {

namespace {

const zwlr_layer_surface_v1_listener kLayer = {
    .configure = [](void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
        static_cast<LayerSurface*>(data)->configure(serial, w, h);
    },
    .closed = [](void* data, zwlr_layer_surface_v1*) { static_cast<LayerSurface*>(data)->closed(); },
};

const wl_callback_listener kFrame = {
    .done = [](void* data, wl_callback*, uint32_t) { static_cast<LayerSurface*>(data)->frameDone(); },
};

const wl_surface_listener kSurface = {
    .enter = [](void*, wl_surface*, wl_output*) {},
    .leave = [](void*, wl_surface*, wl_output*) {},
    .preferred_buffer_scale = [](void* data, wl_surface*, int32_t scale) {
        static_cast<LayerSurface*>(data)->preferredScale(scale);
    },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

} // namespace

LayerSurface::LayerSurface(Output* output, Options options) : m_output(output), m_options(std::move(options)) {
    m_scale = output ? output->scale : 1;
    create();
}

LayerSurface::~LayerSurface() {
    *m_alive = false;
    destroy();
    m_root.reset();
}

void LayerSurface::create() {
    auto& d = Display::get();
    m_surface = wl_compositor_create_surface(d.compositor());
    wl_surface_add_listener(m_surface, &kSurface, this);
    m_layer = zwlr_layer_shell_v1_get_layer_surface(d.layerShell(), m_surface, m_output ? m_output->handle : nullptr,
                                                    static_cast<uint32_t>(m_options.layer), m_options.ns.c_str());
    zwlr_layer_surface_v1_add_listener(m_layer, &kLayer, this);
    zwlr_layer_surface_v1_set_anchor(m_layer, m_options.anchor);
    zwlr_layer_surface_v1_set_size(m_layer, m_options.width, m_options.height);
    zwlr_layer_surface_v1_set_exclusive_zone(m_layer, m_options.exclusiveZone);
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layer, m_options.keyboard ? 1 : 0);
    // The first commit carries no buffer; the compositor answers with configure
    wl_surface_commit(m_surface);
    m_configured = false;
}

void LayerSurface::destroy() {
    m_hovered = m_pressed = nullptr;
    if (m_frame) wl_callback_destroy(m_frame);
    m_frame = nullptr;
    if (m_eglSurface != EGL_NO_SURFACE) Renderer::get().destroySurface(m_eglSurface);
    m_eglSurface = EGL_NO_SURFACE;
    if (m_eglWindow) wl_egl_window_destroy(m_eglWindow);
    m_eglWindow = nullptr;
    if (m_layer) zwlr_layer_surface_v1_destroy(m_layer);
    m_layer = nullptr;
    if (m_surface) wl_surface_destroy(m_surface);
    m_surface = nullptr;
}

void LayerSurface::setRoot(std::unique_ptr<ui::Node> root) {
    m_root = std::move(root);
    m_root->setHost(this);
    requestLayout();
}

void LayerSurface::setSize(int width, int height) {
    m_options.width = width;
    m_options.height = height;
    if (m_layer) {
        zwlr_layer_surface_v1_set_size(m_layer, width, height);
        wl_surface_commit(m_surface);
    }
}

void LayerSurface::setExclusiveZone(int zone) {
    m_options.exclusiveZone = zone;
    if (m_layer) {
        zwlr_layer_surface_v1_set_exclusive_zone(m_layer, zone);
        wl_surface_commit(m_surface);
    }
}

void LayerSurface::setVisible(bool visible) {
    if (visible == m_visible) return;
    m_visible = visible;
    // Unmapping a layer surface means destroying it; it is cheap to recreate
    if (visible) {
        create();
    } else {
        destroy();
    }
}

void LayerSurface::configure(uint32_t serial, uint32_t width, uint32_t height) {
    zwlr_layer_surface_v1_ack_configure(m_layer, serial);
    m_width = width ? static_cast<int>(width) : m_options.width;
    m_height = height ? static_cast<int>(height) : m_options.height;
    if (!m_eglWindow) {
        m_eglWindow = wl_egl_window_create(m_surface, m_width * m_scale, m_height * m_scale);
        m_eglSurface = Renderer::get().createSurface(m_eglWindow);
    } else {
        wl_egl_window_resize(m_eglWindow, m_width * m_scale, m_height * m_scale, 0, 0);
    }
    m_configured = true;
    m_needsLayout = m_dirty = true;
    // Render now: a configured surface must commit a buffer to be shown
    render();
}

void LayerSurface::closed() {
    // The compositor dropped us (output gone); the owner rebuilds on hotplug
    destroy();
}

void LayerSurface::preferredScale(int32_t scale) {
    if (scale == m_scale || scale < 1) return;
    m_scale = scale;
    if (m_eglWindow) wl_egl_window_resize(m_eglWindow, m_width * m_scale, m_height * m_scale, 0, 0);
    TextEngine::get().clear();
    requestLayout();
}

void LayerSurface::requestLayout() {
    m_needsLayout = true;
    requestRedraw();
}

void LayerSurface::requestRedraw() {
    m_dirty = true;
    schedule();
}

void LayerSurface::nodeDestroyed(ui::Node* node) {
    if (m_hovered == node) m_hovered = nullptr;
    if (m_pressed == node) m_pressed = nullptr;
}

void LayerSurface::schedule() {
    // A frame callback in flight will repaint when it fires
    if (m_scheduled || m_frame || !m_configured) return;
    m_scheduled = true;
    EventLoop::get().defer([this, alive = m_alive] {
        if (!*alive) return;
        m_scheduled = false;
        if (m_dirty) render();
    });
}

void LayerSurface::frameDone() {
    wl_callback_destroy(m_frame);
    m_frame = nullptr;
    if (std::getenv("M3_DEBUG_FRAMES")) log::info("done dirty={} anim={}", m_dirty, m_root && m_root->animating());
    if (m_dirty || (m_root && m_root->animating())) render();
}

void LayerSurface::layout() {
    if (!m_root) return;
    const SizeF size{static_cast<float>(m_width), static_cast<float>(m_height)};
    m_root->measure(size);
    m_root->arrange({0, 0, size.w, size.h});
    m_needsLayout = false;
}

void LayerSurface::render() {
    if (!m_configured || m_eglSurface == EGL_NO_SURFACE) return;
    // One frame in flight at a time: the pending callback repaints when it fires
    if (m_frame) {
        m_dirty = true;
        return;
    }
    if (m_needsLayout) layout();
    m_dirty = false;

    auto& r = Renderer::get();
    r.begin(m_eglSurface, m_width * m_scale, m_height * m_scale, static_cast<float>(m_scale));
    if (m_root) m_root->paint(r);
    // Painting may flag more work (running animations); that becomes the next frame
    m_dirty = m_dirty || (m_root && m_root->animating());
    if (std::getenv("M3_DEBUG_FRAMES")) log::info("frame {} {}x{} dirty={}", m_options.ns, m_width, m_height, m_dirty);

    m_frame = wl_surface_frame(m_surface);
    wl_callback_add_listener(m_frame, &kFrame, this);
    wl_surface_set_buffer_scale(m_surface, m_scale);
    r.end(m_eglSurface); // eglSwapBuffers commits the surface
    TextEngine::get().collect();
}

// ── Input ──

void LayerSurface::pointerMotion(double x, double y) {
    ui::Node* hit = m_root ? m_root->hitTest(static_cast<float>(x), static_cast<float>(y)) : nullptr;
    if (hit == m_hovered) return;
    if (m_hovered) m_hovered->onHover(false);
    m_hovered = hit;
    if (m_hovered) m_hovered->onHover(true);
}

void LayerSurface::pointerLeave() {
    if (m_hovered) m_hovered->onHover(false);
    m_hovered = m_pressed = nullptr;
}

void LayerSurface::pointerButton(uint32_t button, bool pressed) {
    if (pressed) {
        m_pressed = m_hovered;
        if (m_pressed) m_pressed->onPress(button);
    } else {
        // A click is press and release on the same node
        ui::Node* target = m_pressed == m_hovered ? m_pressed : nullptr;
        m_pressed = nullptr;
        if (target) target->onClick(button);
    }
}

void LayerSurface::pointerScroll(int direction) {
    if (m_hovered) m_hovered->onScroll(direction);
}

} // namespace m3::wl
