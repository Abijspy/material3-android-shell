#pragma once

#include "core/EventLoop.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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
struct wl_data_device_manager;
struct wl_data_device;
struct wl_data_offer;
struct wl_data_source;

namespace m3::wl {

class Surface;

// A connected monitor. `name` is the connector ("DP-1"), which is also what
// niri calls the output.
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
    void* gammaManager() const { return m_gammaManager; }      // zwlr_gamma_control_manager_v1* (night light)
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
    // The held modifiers changed (ui::Modifier bits): the app switcher ends
    // when Super is let go
    std::function<void(uint32_t mods)> onModifiers;
    // The modifiers held now (ui::Modifier bits): Ctrl- and Shift-clicks
    uint32_t heldModifiers() const { return m_heldMods; }
    void pointerAxisSource(uint32_t source);
    void pointerAxisStop(uint32_t axis);
    void keymap(int fd, uint32_t size);
    void keyboardEnter(wl_surface* surface);
    void keyboardLeave(wl_surface* surface);
    void key(uint32_t key, bool pressed);
    void modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void repeatInfo(int32_t rate, int32_t delay);

    // Drag and drop of files (text/uri-list): a drag from this surface,
    // started while a button's held (Surface does it for nodes with
    // dragFiles)
    void startDrag(Surface* origin, const std::vector<std::string>& paths);
    bool dragging() const { return m_dragSource != nullptr; }
    void offerMime(wl_data_offer* offer, const char* mime);
    void selectionOffer(wl_data_offer* offer);
    void dndEnter(uint32_t serial, wl_surface* surface, double x, double y, wl_data_offer* offer);
    void dndMotion(double x, double y);
    void dndLeave();
    void dndDrop();
    void sourceSend(const char* mime, int fd);
    void sourceDone(wl_data_source* source);

private:
    Display() = default;

    wl_display* m_display = nullptr;
    uint32_t m_heldMods = 0;
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
    void* m_gammaManager = nullptr;

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
    bool m_axisPrecise = false; // this frame's scrolling is a touchpad's
    bool m_axisStopped = false; // the fingers lifted this frame

    void setUpDataDevice();
    void dropOffer(wl_data_offer* offer);
    wl_data_device_manager* m_dataDeviceManager = nullptr;
    wl_data_device* m_dataDevice = nullptr;
    std::unordered_map<wl_data_offer*, std::vector<std::string>> m_offers; // their types
    wl_data_offer *m_selection = nullptr, *m_dndOffer = nullptr;
    Surface* m_dndSurface = nullptr;
    uint32_t m_dndSerial = 0;
    double m_dndX = 0, m_dndY = 0;
    bool m_dndAccepted = false, m_dndActionsSet = false, m_dndDropping = false;
    wl_data_source* m_dragSource = nullptr;
    std::string m_dragUris, m_dragText;
};

} // namespace m3::wl
