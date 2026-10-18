#pragma once

#include "core/EventLoop.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_seat;
struct wl_pointer;
struct wl_output;
struct wl_surface;
struct zwlr_layer_shell_v1;
struct wp_cursor_shape_manager_v1;
struct wp_cursor_shape_device_v1;

namespace m3::wl {

class LayerSurface;

// A connected monitor. `name` is the connector ("DP-1"), which is also what
// Hyprland calls the monitor.
struct Output {
    wl_output* handle = nullptr;
    uint32_t globalName = 0;
    std::string name;
    std::string description;
    int scale = 1;
    bool ready = false; // first `done` received
};

// The Wayland connection: globals, outputs, the pointer, and the event-loop
// glue. Protocol transport only; compositor specifics live in services.
class Display : public EventSource {
public:
    static Display& get();

    bool connect();

    wl_display* display() const { return m_display; }
    wl_compositor* compositor() const { return m_compositor; }
    zwlr_layer_shell_v1* layerShell() const { return m_layerShell; }

    const std::vector<std::unique_ptr<Output>>& outputs() const { return m_outputs; }
    std::function<void(Output*)> onOutputReady;
    std::function<void(Output*)> onOutputRemoved;

    // EventSource
    int fd() const override;
    void prepare() override;
    void dispatch(short revents) override;

    // Used by the protocol listeners
    void addGlobal(wl_registry* registry, uint32_t name, const char* iface, uint32_t version);
    void removeGlobal(uint32_t name);
    void outputDone(Output* output);
    void seatCapabilities(uint32_t caps);
    void pointerEnter(uint32_t serial, wl_surface* surface, double x, double y);
    void pointerLeave(wl_surface* surface);
    void pointerMotion(double x, double y);
    void pointerButton(uint32_t button, bool pressed);
    void pointerAxis(uint32_t axis, double value);
    void pointerFrame();

private:
    Display() = default;

    wl_display* m_display = nullptr;
    wl_registry* m_registry = nullptr;
    wl_compositor* m_compositor = nullptr;
    zwlr_layer_shell_v1* m_layerShell = nullptr;
    wl_seat* m_seat = nullptr;
    wl_pointer* m_pointer = nullptr;
    wp_cursor_shape_manager_v1* m_cursorShapeManager = nullptr;
    wp_cursor_shape_device_v1* m_cursorShape = nullptr;
    std::vector<std::unique_ptr<Output>> m_outputs;
    bool m_reading = false;

    LayerSurface* m_pointerFocus = nullptr;
    double m_pointerX = 0, m_pointerY = 0;
    double m_scrollAccum = 0;
};

} // namespace m3::wl
