#include "wayland/LayerSurface.h"

#include "wayland/Display.h"
#include "wayland/LayerShellProtocol.h"

#include "ext-session-lock-v1-client-protocol.h"

#include <format>
#include <wayland-client.h>

namespace m3::wl {

namespace {

const zwlr_layer_surface_v1_listener kLayer = {
    .configure = [](void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
        static_cast<LayerSurface*>(data)->configure(serial, w, h);
    },
    .closed = [](void* data, zwlr_layer_surface_v1*) { static_cast<LayerSurface*>(data)->closed(); },
};

const ext_session_lock_surface_v1_listener kLockSurface = {
    .configure = [](void* data, ext_session_lock_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
        static_cast<LockSurface*>(data)->configure(serial, w, h);
    },
};

} // namespace

// ── LayerSurface ──

LayerSurface::LayerSurface(Output* output, Options options) : Surface(output), m_options(std::move(options)) {
    m_tag = m_options.ns;
    create();
}

LayerSurface::~LayerSurface() {
    destroySurface();
}

void LayerSurface::create() {
    createSurface();
    auto& d = Display::get();
    m_layer = zwlr_layer_shell_v1_get_layer_surface(d.layerShell(), wlSurface(), m_output ? m_output->handle : nullptr,
                                                    static_cast<uint32_t>(m_options.layer), m_options.ns.c_str());
    zwlr_layer_surface_v1_add_listener(m_layer, &kLayer, this);
    zwlr_layer_surface_v1_set_anchor(m_layer, m_options.anchor);
    zwlr_layer_surface_v1_set_size(m_layer, m_options.width, m_options.height);
    zwlr_layer_surface_v1_set_exclusive_zone(m_layer, m_options.exclusiveZone);
    zwlr_layer_surface_v1_set_margin(m_layer, m_options.marginTop, m_options.marginRight, m_options.marginBottom,
                                     m_options.marginLeft);
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layer, static_cast<uint32_t>(m_options.keyboard));
    // The first commit carries no buffer; the compositor answers with configure
    wl_surface_commit(wlSurface());
}

void LayerSurface::destroyRole() {
    if (m_layer) zwlr_layer_surface_v1_destroy(m_layer);
    m_layer = nullptr;
}

void LayerSurface::setAnchor(uint32_t anchor) {
    if (anchor == m_options.anchor) return;
    m_options.anchor = anchor;
    if (m_layer) {
        zwlr_layer_surface_v1_set_anchor(m_layer, anchor);
        wl_surface_commit(wlSurface());
    }
}

void LayerSurface::setSize(int width, int height) {
    m_options.width = width;
    m_options.height = height;
    if (m_layer) {
        zwlr_layer_surface_v1_set_size(m_layer, width, height);
        wl_surface_commit(wlSurface());
    }
}

void LayerSurface::setExclusiveZone(int zone) {
    m_options.exclusiveZone = zone;
    if (m_layer) {
        zwlr_layer_surface_v1_set_exclusive_zone(m_layer, zone);
        wl_surface_commit(wlSurface());
    }
}

void LayerSurface::setKeyboard(Keyboard mode) {
    m_options.keyboard = mode;
    if (m_layer) {
        zwlr_layer_surface_v1_set_keyboard_interactivity(m_layer, static_cast<uint32_t>(mode));
        wl_surface_commit(wlSurface());
    }
}

void LayerSurface::setVisible(bool visible) {
    if (visible == m_visible) return;
    m_visible = visible;
    // Unmapping a layer surface means destroying it; it is cheap to recreate
    if (visible)
        create();
    else
        destroySurface();
}

void LayerSurface::configure(uint32_t serial, uint32_t width, uint32_t height) {
    zwlr_layer_surface_v1_ack_configure(m_layer, serial);
    configured(width ? static_cast<int>(width) : m_options.width, height ? static_cast<int>(height) : m_options.height);
}

std::string LayerSurface::describe() const {
    const auto sz = size();
    return std::format("surface {} {:.0f}x{:.0f} anchor={} margin={},{},{},{}", m_options.ns, sz.w, sz.h, m_options.anchor,
                       m_options.marginTop, m_options.marginRight, m_options.marginBottom, m_options.marginLeft);
}

void LayerSurface::closed() {
    // The compositor dropped us (output gone); the owner rebuilds on hotplug
    destroySurface();
}

// ── LockSurface ──

LockSurface::LockSurface(Output* output, void* lock) : Surface(output) {
    m_tag = "material3ui-lock";
    createSurface();
    auto* ls = ext_session_lock_v1_get_lock_surface(static_cast<ext_session_lock_v1*>(lock), wlSurface(), output->handle);
    ext_session_lock_surface_v1_add_listener(ls, &kLockSurface, this);
    m_lockSurface = ls;
}

LockSurface::~LockSurface() {
    destroySurface();
}

void LockSurface::destroyRole() {
    if (m_lockSurface) ext_session_lock_surface_v1_destroy(static_cast<ext_session_lock_surface_v1*>(m_lockSurface));
    m_lockSurface = nullptr;
}

void LockSurface::configure(uint32_t serial, uint32_t width, uint32_t height) {
    ext_session_lock_surface_v1_ack_configure(static_cast<ext_session_lock_surface_v1*>(m_lockSurface), serial);
    configured(static_cast<int>(width), static_cast<int>(height));
}

} // namespace m3::wl
