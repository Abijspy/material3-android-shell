#include "wayland/Window.h"

#include "wayland/Display.h"

#include "xdg-shell-client-protocol.h"

#include <format>
#include <wayland-client.h>

namespace m3::wl {

namespace {

const xdg_surface_listener kXdgSurface = {
    .configure = [](void* data, xdg_surface*, uint32_t serial) { static_cast<Window*>(data)->surfaceConfigure(serial); },
};

// Newer xdg-shell adds events (bounds, capabilities) we don't need
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const xdg_toplevel_listener kToplevel = {
    // The states aren't read: Hyprland marks every window it may tile as
    // maximised (so apps drop their shadows), which says nothing about the
    // maximise button. The window tracks what it asked for itself.
    .configure = [](void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
        static_cast<Window*>(data)->toplevelConfigure(w, h);
    },
    .close = [](void* data, xdg_toplevel*) { static_cast<Window*>(data)->close(); },
    .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t) {},
    .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {},
};
#pragma GCC diagnostic pop

} // namespace

Window::Window(Options options) : Surface(nullptr), m_options(std::move(options)) {
    m_tag = m_options.appId;
    auto& d = Display::get();
    createSurface();
    m_xdgSurface = xdg_wm_base_get_xdg_surface(d.wmBase(), wlSurface());
    xdg_surface_add_listener(m_xdgSurface, &kXdgSurface, this);
    m_toplevel = xdg_surface_get_toplevel(m_xdgSurface);
    xdg_toplevel_add_listener(m_toplevel, &kToplevel, this);
    // The app id is what docks, task lists and window rules match on
    xdg_toplevel_set_app_id(m_toplevel, m_options.appId.c_str());
    xdg_toplevel_set_title(m_toplevel, m_options.title.c_str());
    if (m_options.minWidth > 0 || m_options.minHeight > 0)
        xdg_toplevel_set_min_size(m_toplevel, m_options.minWidth, m_options.minHeight);
    // The first commit carries no buffer; the compositor answers with configure
    wl_surface_commit(wlSurface());
}

Window::~Window() {
    destroySurface();
}

void Window::destroyRole() {
    if (m_toplevel) xdg_toplevel_destroy(m_toplevel);
    if (m_xdgSurface) xdg_surface_destroy(m_xdgSurface);
    m_toplevel = nullptr;
    m_xdgSurface = nullptr;
}

void Window::setTitle(const std::string& title) {
    m_options.title = title;
    if (m_toplevel) xdg_toplevel_set_title(m_toplevel, title.c_str());
}

void Window::setMaximized(bool maximized) {
    if (!m_toplevel || maximized == m_maximized) return;
    m_maximized = maximized;
    if (onStateChanged) onStateChanged();
    if (maximized)
        xdg_toplevel_set_maximized(m_toplevel);
    else
        xdg_toplevel_unset_maximized(m_toplevel);
}

void Window::startMove() {
    auto& d = Display::get();
    if (m_toplevel && d.seat()) xdg_toplevel_move(m_toplevel, d.seat(), d.buttonSerial());
}

void Window::toplevelConfigure(int width, int height) {
    m_pendingW = width;
    m_pendingH = height;
}

void Window::surfaceConfigure(uint32_t serial) {
    xdg_surface_ack_configure(m_xdgSurface, serial);
    // 0 × 0: the compositor leaves the size to us
    const int w = m_pendingW > 0 ? m_pendingW : m_options.width;
    const int h = m_pendingH > 0 ? m_pendingH : m_options.height;
    configured(w, h);
}

void Window::close() {
    if (onCloseRequested) onCloseRequested();
}

std::string Window::describe() const {
    const auto sz = size();
    return std::format("window {} {:.0f}x{:.0f}{}", m_options.appId, sz.w, sz.h, m_maximized ? " maximized" : "");
}

} // namespace m3::wl
