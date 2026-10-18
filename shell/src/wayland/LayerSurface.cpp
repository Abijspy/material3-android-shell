#include "wayland/LayerSurface.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "wayland/Display.h"
#include "wayland/LayerShellProtocol.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include <cmath>
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
        static_cast<LayerSurface*>(data)->preferredScale(static_cast<float>(scale), false);
    },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

const wp_fractional_scale_v1_listener kFractional = {
    .preferred_scale = [](void* data, wp_fractional_scale_v1*, uint32_t scale120) {
        static_cast<LayerSurface*>(data)->preferredScale(static_cast<float>(scale120) / 120.0f, true);
    },
};

bool debugFrames() {
    static const bool on = std::getenv("M3_DEBUG_FRAMES") != nullptr;
    return on;
}

} // namespace

LayerSurface::LayerSurface(Output* output, Options options) : m_output(output), m_options(std::move(options)) {
    m_scale = output ? static_cast<float>(output->scale) : 1.0f;
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
    // Fractional scale needs a viewport to map the buffer to logical size
    if (d.fractionalScale() && d.viewporter()) {
        m_fractional = wp_fractional_scale_manager_v1_get_fractional_scale(d.fractionalScale(), m_surface);
        wp_fractional_scale_v1_add_listener(m_fractional, &kFractional, this);
        m_viewport = wp_viewporter_get_viewport(d.viewporter(), m_surface);
    }
    m_layer = zwlr_layer_shell_v1_get_layer_surface(d.layerShell(), m_surface, m_output ? m_output->handle : nullptr,
                                                    static_cast<uint32_t>(m_options.layer), m_options.ns.c_str());
    zwlr_layer_surface_v1_add_listener(m_layer, &kLayer, this);
    zwlr_layer_surface_v1_set_anchor(m_layer, m_options.anchor);
    zwlr_layer_surface_v1_set_size(m_layer, m_options.width, m_options.height);
    zwlr_layer_surface_v1_set_exclusive_zone(m_layer, m_options.exclusiveZone);
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layer, static_cast<uint32_t>(m_options.keyboard));
    // The first commit carries no buffer; the compositor answers with configure
    wl_surface_commit(m_surface);
    m_configured = false;
}

void LayerSurface::destroy() {
    Display::get().forget(this);
    m_hovered = m_pressed = nullptr;
    m_hasKeyboard = false;
    if (m_frame) wl_callback_destroy(m_frame);
    m_frame = nullptr;
    if (m_eglSurface != EGL_NO_SURFACE) Renderer::get().destroySurface(m_eglSurface);
    m_eglSurface = EGL_NO_SURFACE;
    if (m_eglWindow) wl_egl_window_destroy(m_eglWindow);
    m_eglWindow = nullptr;
    if (m_viewport) wp_viewport_destroy(m_viewport);
    m_viewport = nullptr;
    if (m_fractional) wp_fractional_scale_v1_destroy(m_fractional);
    m_fractional = nullptr;
    if (m_layer) zwlr_layer_surface_v1_destroy(m_layer);
    m_layer = nullptr;
    if (m_surface) wl_surface_destroy(m_surface);
    m_surface = nullptr;
}

void LayerSurface::setRoot(std::unique_ptr<ui::Node> root) {
    m_hovered = m_pressed = m_focus = nullptr;
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
    if (visible)
        create();
    else
        destroy();
}

int LayerSurface::bufferWidth() const {
    return std::max(1, static_cast<int>(std::lround(m_width * m_scale)));
}

int LayerSurface::bufferHeight() const {
    return std::max(1, static_cast<int>(std::lround(m_height * m_scale)));
}

void LayerSurface::resizeBuffer() {
    // Nothing to size before the first configure (and 0×0 is a protocol error)
    if (m_width <= 0 || m_height <= 0) return;
    if (m_eglWindow) wl_egl_window_resize(m_eglWindow, bufferWidth(), bufferHeight(), 0, 0);
    if (m_viewport) wp_viewport_set_destination(m_viewport, m_width, m_height);
}

void LayerSurface::configure(uint32_t serial, uint32_t width, uint32_t height) {
    zwlr_layer_surface_v1_ack_configure(m_layer, serial);
    m_width = width ? static_cast<int>(width) : m_options.width;
    m_height = height ? static_cast<int>(height) : m_options.height;
    if (!m_eglWindow) {
        m_eglWindow = wl_egl_window_create(m_surface, bufferWidth(), bufferHeight());
        m_eglSurface = Renderer::get().createSurface(m_eglWindow);
    }
    resizeBuffer();
    m_configured = true;
    m_needsLayout = m_dirty = true;
    // A configured surface must commit a buffer to be shown
    render();
}

void LayerSurface::closed() {
    // The compositor dropped us (output gone); the owner rebuilds on hotplug
    destroy();
}

void LayerSurface::preferredScale(float scale, bool fractional) {
    // Fractional-capable compositors also send the integer hint; ignore it
    if (m_fractional && !fractional) return;
    if (scale <= 0 || scale == m_scale) return;
    m_scale = scale;
    resizeBuffer();
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
    if (m_focus == node) m_focus = nullptr;
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
    r.begin(m_eglSurface, bufferWidth(), bufferHeight(), m_scale);
    if (m_root) m_root->paint(r);
    // Painting may flag more work (running animations); that becomes the next frame
    m_dirty = m_dirty || (m_root && m_root->animating());
    if (debugFrames()) log::info("frame {} {}x{}@{} dirty={}", m_options.ns, m_width, m_height, m_scale, m_dirty);

    m_frame = wl_surface_frame(m_surface);
    wl_callback_add_listener(m_frame, &kFrame, this);
    if (!m_viewport) wl_surface_set_buffer_scale(m_surface, static_cast<int>(m_scale));
    r.end(m_eglSurface); // eglSwapBuffers commits the surface
    TextEngine::get().collect();
}

// ── Input ──

void LayerSurface::pointerMotion(double x, double y) {
    m_pointerX = static_cast<float>(x);
    m_pointerY = static_cast<float>(y);
    ui::Node* hit = m_root ? m_root->hitTest(m_pointerX, m_pointerY) : nullptr;
    if (hit != m_hovered) {
        if (m_hovered) m_hovered->onHover(false);
        m_hovered = hit;
        if (m_hovered) m_hovered->onHover(true);
    }
    // A pressed node keeps receiving motion (slider drags)
    if (m_pressed) m_pressed->onDrag(m_pointerX, m_pointerY);
}

void LayerSurface::pointerLeave() {
    if (m_hovered) m_hovered->onHover(false);
    if (m_pressed) m_pressed->onRelease();
    m_hovered = m_pressed = nullptr;
}

void LayerSurface::pointerButton(uint32_t button, bool pressed) {
    if (pressed) {
        m_pressed = m_hovered;
        if (m_pressed) {
            if (m_pressed->focusable()) setFocus(m_pressed);
            m_pressed->onPress(button, m_pointerX, m_pointerY);
        } else if (onEmptyClick) {
            onEmptyClick(m_pointerX, m_pointerY);
        }
    } else {
        // A click is press and release on the same node
        ui::Node* target = m_pressed;
        m_pressed = nullptr;
        if (!target) return;
        target->onRelease();
        if (target == m_hovered) target->onClick(button);
    }
}

void LayerSurface::pointerScroll(int direction) {
    // The deepest node under the pointer that takes scrolling, else its ancestors
    for (ui::Node* n = m_hovered ? m_hovered : (m_root ? m_root->hitTest(m_pointerX, m_pointerY, true) : nullptr); n;
         n = n->parentNode())
        if (n->onScroll(direction)) return;
}

void LayerSurface::setFocus(ui::Node* node) {
    if (node == m_focus) return;
    if (m_focus) m_focus->onFocus(false);
    m_focus = node;
    if (m_focus) m_focus->onFocus(true);
}

void LayerSurface::keyboardFocus(bool focused) {
    if (std::getenv("M3_DEBUG_FOCUS")) log::info("keyboard {} {}", m_options.ns, focused);
    m_hasKeyboard = focused;
    if (m_focus) m_focus->onFocus(focused);
}

void LayerSurface::key(const ui::KeyEvent& event) {
    for (ui::Node* n = m_focus; n; n = n->parentNode())
        if (n->onKey(event)) return;
    if (onKey) onKey(event);
}

} // namespace m3::wl
