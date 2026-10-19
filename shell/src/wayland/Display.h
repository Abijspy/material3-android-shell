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
struct xdg_wm_base;
struct wl_pointer;
struct wl_keyboard;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct wp_fractional_scale_manager_v1;
struct wp_viewporter;
struct wl_output;
struct wl_surface;
struct zwlr_layer_shell_v1;
struct wp_cursor_shape_manager_v1;
struct wp_cursor_shape_device_v1;
struct ext_background_effect_manager_v1;

namespace m3::wl {

class Surface;

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

    // A surface going away must not keep pointer or keyboard focus
    void forget(Surface* surface);

    bool connect();

    wl_display* display() const { return m_display; }
    wl_compositor* compositor() const { return m_compositor; }
    zwlr_layer_shell_v1* layerShell() const { return m_layerShell; }
    void* sessionLockManager() const { return m_lockManager; } // ext_session_lock_manager_v1*
    void* idleNotifier() const { return m_idleNotifier; }      // ext_idle_notifier_v1*
    wl_seat* seat() const { return m_seat; }
    xdg_wm_base* wmBase() const { return m_wmBase; }  // app windows (xdg-shell)
    uint32_t buttonSerial() const { return m_buttonSerial; } // the last pointer press, for window moves
    wp_fractional_scale_manager_v1* fractionalScale() const { return m_fractionalScale; }
    wp_viewporter* viewporter() const { return m_viewporter; }
    // Set when the compositor can blur behind surfaces (ext-background-effect)
    ext_background_effect_manager_v1* backgroundEffect() const { return m_blurCapable ? m_backgroundEffect : nullptr; }
    void backgroundEffectCapabilities(uint32_t flags);

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
    void setButtonSerial(uint32_t serial) { m_buttonSerial = serial; }
    void pointerAxis(uint32_t axis, double value);
    void pointerFrame();
    void keymap(int fd, uint32_t size);
    void keyboardEnter(wl_surface* surface);
    void keyboardLeave(wl_surface* surface);
    void key(uint32_t key, bool pressed);
    void modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void repeatInfo(int32_t rate, int32_t delay);

private:
    Display() = default;

    wl_display* m_display = nullptr;
    wl_registry* m_registry = nullptr;
    wl_compositor* m_compositor = nullptr;
    zwlr_layer_shell_v1* m_layerShell = nullptr;
    wl_seat* m_seat = nullptr;
    xdg_wm_base* m_wmBase = nullptr;
    uint32_t m_buttonSerial = 0;
    wl_pointer* m_pointer = nullptr;
    wp_cursor_shape_manager_v1* m_cursorShapeManager = nullptr;
    wp_cursor_shape_device_v1* m_cursorShape = nullptr;
    wp_fractional_scale_manager_v1* m_fractionalScale = nullptr;
    wp_viewporter* m_viewporter = nullptr;
    ext_background_effect_manager_v1* m_backgroundEffect = nullptr;
    bool m_blurCapable = false;
    wl_keyboard* m_keyboard = nullptr;
    void* m_lockManager = nullptr;
    void* m_idleNotifier = nullptr;

    xkb_context* m_xkb = nullptr;
    xkb_keymap* m_keymap = nullptr;
    xkb_state* m_xkbState = nullptr;
    Surface* m_keyboardFocus = nullptr;
    int m_repeatRate = 25, m_repeatDelay = 600;
    uint32_t m_repeatKey = 0;
    EventLoop::TimerId m_repeatTimer = 0;
    void sendKey(uint32_t key, bool repeat);
    std::vector<std::unique_ptr<Output>> m_outputs;
    bool m_reading = false;

    Surface* m_pointerFocus = nullptr;
    double m_pointerX = 0, m_pointerY = 0;
    double m_scrollAccum = 0;
};

} // namespace m3::wl
