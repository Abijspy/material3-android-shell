#include "core/Guard.h"
#include "wayland/Display.h"

#include "core/Log.h"
#include "wayland/Surface.h"

#include "cursor-shape-v1-client-protocol.h"
#include "ext-background-effect-v1-client-protocol.h"
#include "ext-idle-notify-v1-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wayland/LayerShellProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace m3::wl {

namespace {

const ext_background_effect_manager_v1_listener kBackgroundEffect = {
    .capabilities = [](void*, ext_background_effect_manager_v1*, uint32_t flags) {
        Display::get().backgroundEffectCapabilities(flags);
    },
};

bool debugFocus() {
    static const bool on = std::getenv("M3_DEBUG_FOCUS") != nullptr;
    return on;
}

const wl_registry_listener kRegistry = {
    .global = [](void* data, wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->addGlobal(r, name, iface, version); });
    },
    .global_remove = [](void* data, wl_registry*, uint32_t name) { m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->removeGlobal(name); }); },
};

const wl_output_listener kOutput = {
    .geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    .mode = [](void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {},
    .done = [](void* data, wl_output*) { Display::get().outputDone(static_cast<Output*>(data)); },
    .scale = [](void* data, wl_output*, int32_t factor) { static_cast<Output*>(data)->scale = factor; },
    .name = [](void* data, wl_output*, const char* name) { static_cast<Output*>(data)->name = name; },
    .description = [](void* data, wl_output*, const char* d) { static_cast<Output*>(data)->description = d; },
};

const wl_seat_listener kSeat = {
    .capabilities = [](void* data, wl_seat*, uint32_t caps) { m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->seatCapabilities(caps); }); },
    .name = [](void*, wl_seat*, const char*) {},
};

// Newer libwayland adds events (warp) that are never sent at the version we
// bind; leaving them unset is intended.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
const wl_pointer_listener kPointer = {
    .enter = [](void* data, wl_pointer*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerEnter(serial, s, wl_fixed_to_double(x), wl_fixed_to_double(y)); });
    },
    .leave = [](void* data, wl_pointer*, uint32_t, wl_surface* s) { m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerLeave(s); }); },
    .motion = [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerMotion(wl_fixed_to_double(x), wl_fixed_to_double(y)); });
    },
    .button = [](void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->setButtonSerial(serial); });
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerButton(button, state == WL_POINTER_BUTTON_STATE_PRESSED); });
    },
    .axis = [](void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerAxis(axis, wl_fixed_to_double(value)); });
    },
    .frame = [](void* data, wl_pointer*) { m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerFrame(); }); },
    .axis_source = [](void* data, wl_pointer*, uint32_t source) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerAxisSource(source); });
    },
    .axis_stop = [](void* data, wl_pointer*, uint32_t, uint32_t axis) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->pointerAxisStop(axis); });
    },
    .axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_value120 = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {},
};

// The compositor pings app windows to see that we're alive
const xdg_wm_base_listener kWmBase = {
    .ping = [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
};

const wl_keyboard_listener kKeyboard = {
    .keymap = [](void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
        if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
            m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->keymap(fd, size); });
        else
            close(fd);
    },
    .enter = [](void* data, wl_keyboard*, uint32_t, wl_surface* s, wl_array*) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->keyboardEnter(s); });
    },
    .leave = [](void* data, wl_keyboard*, uint32_t, wl_surface* s) { m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->keyboardLeave(s); }); },
    .key = [](void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED); });
    },
    .modifiers = [](void* data, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t group) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->modifiers(dep, lat, lock, group); });
    },
    .repeat_info = [](void* data, wl_keyboard*, int32_t rate, int32_t delay) {
        m3::guard("a Wayland event", [&] { static_cast<Display*>(data)->repeatInfo(rate, delay); });
    },
};
#pragma GCC diagnostic pop

} // namespace

Display& Display::get() {
    static Display instance;
    return instance;
}

bool Display::connect() {
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        log::error("cannot connect to a Wayland compositor (is WAYLAND_DISPLAY set?)");
        return false;
    }
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegistry, this);
    wl_display_roundtrip(m_display); // globals
    wl_display_roundtrip(m_display); // output names and seat capabilities

    if (!m_compositor || !m_layerShell) {
        log::error("the compositor does not support wlr-layer-shell");
        return false;
    }
    EventLoop::get().add(this);
    return true;
}

void Display::addGlobal(wl_registry* r, uint32_t name, const char* iface, uint32_t version) {
    const std::string_view i(iface);
    if (i == wl_compositor_interface.name) {
        m_compositor = static_cast<wl_compositor*>(wl_registry_bind(r, name, &wl_compositor_interface, std::min(version, 6u)));
    } else if (i == zwlr_layer_shell_v1_interface.name) {
        m_layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (i == xdg_wm_base_interface.name) {
        m_wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(r, name, &xdg_wm_base_interface, std::min(version, 5u)));
        xdg_wm_base_add_listener(m_wmBase, &kWmBase, nullptr);
    } else if (i == wl_seat_interface.name && !m_seat) {
        m_seat = static_cast<wl_seat*>(wl_registry_bind(r, name, &wl_seat_interface, std::min(version, 5u)));
        wl_seat_add_listener(m_seat, &kSeat, this);
    } else if (i == wp_cursor_shape_manager_v1_interface.name) {
        m_cursorShapeManager = static_cast<wp_cursor_shape_manager_v1*>(
            wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1));
    } else if (i == wp_fractional_scale_manager_v1_interface.name) {
        m_fractionalScale = static_cast<wp_fractional_scale_manager_v1*>(
            wl_registry_bind(r, name, &wp_fractional_scale_manager_v1_interface, 1));
    } else if (i == ext_session_lock_manager_v1_interface.name) {
        m_lockManager = wl_registry_bind(r, name, &ext_session_lock_manager_v1_interface, 1);
    } else if (i == ext_idle_notifier_v1_interface.name) {
        m_idleNotifier = wl_registry_bind(r, name, &ext_idle_notifier_v1_interface, 1);
    } else if (i == ext_background_effect_manager_v1_interface.name) {
        m_backgroundEffect = static_cast<ext_background_effect_manager_v1*>(
            wl_registry_bind(r, name, &ext_background_effect_manager_v1_interface, 1));
        ext_background_effect_manager_v1_add_listener(m_backgroundEffect, &kBackgroundEffect, this);
    } else if (i == wp_viewporter_interface.name) {
        m_viewporter = static_cast<wp_viewporter*>(wl_registry_bind(r, name, &wp_viewporter_interface, 1));
    } else if (i == wl_output_interface.name && version >= 4) {
        auto out = std::make_unique<Output>();
        out->globalName = name;
        out->handle = static_cast<wl_output*>(wl_registry_bind(r, name, &wl_output_interface, 4));
        wl_output_add_listener(out->handle, &kOutput, out.get());
        m_outputs.push_back(std::move(out));
    }
}

void Display::backgroundEffectCapabilities(uint32_t flags) {
    const bool capable = flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR;
    if (capable == m_blurCapable) return;
    m_blurCapable = capable;
    log::info("compositor blur {}", capable ? "available" : "unavailable");
    for (auto* s : Surface::all()) s->requestRedraw();
}

void Display::removeGlobal(uint32_t name) {
    auto it = std::find_if(m_outputs.begin(), m_outputs.end(), [name](auto& o) { return o->globalName == name; });
    if (it == m_outputs.end()) return;
    if (onOutputRemoved) onOutputRemoved(it->get());
    wl_output_release((*it)->handle);
    m_outputs.erase(it);
}

void Display::outputDone(Output* output) {
    if (output->ready) return;
    output->ready = true;
    if (onOutputReady) onOutputReady(output);
}

void Display::seatCapabilities(uint32_t caps) {
    const bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    if (hasKeyboard && !m_keyboard) {
        if (!m_xkb) m_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        m_keyboard = wl_seat_get_keyboard(m_seat);
        wl_keyboard_add_listener(m_keyboard, &kKeyboard, this);
    } else if (!hasKeyboard && m_keyboard) {
        wl_keyboard_release(m_keyboard);
        m_keyboard = nullptr;
    }

    const bool hasPointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (hasPointer && !m_pointer) {
        m_pointer = wl_seat_get_pointer(m_seat);
        wl_pointer_add_listener(m_pointer, &kPointer, this);
        if (m_cursorShapeManager) m_cursorShape = wp_cursor_shape_manager_v1_get_pointer(m_cursorShapeManager, m_pointer);
    } else if (!hasPointer && m_pointer) {
        if (m_cursorShape) wp_cursor_shape_device_v1_destroy(m_cursorShape);
        m_cursorShape = nullptr;
        wl_pointer_release(m_pointer);
        m_pointer = nullptr;
    }
}

void Display::pointerEnter(uint32_t serial, wl_surface* surface, double x, double y) {
    if (debugFocus()) log::info("wl_pointer.enter {:.0f},{:.0f}", x, y);
    m_pointerFocus = Surface::fromSurface(surface);
    m_pointerX = x;
    m_pointerY = y;
    // Without a cursor of our own the pointer would keep whatever it showed last
    if (m_cursorShape) wp_cursor_shape_device_v1_set_shape(m_cursorShape, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
    if (m_pointerFocus) m_pointerFocus->pointerMotion(x, y);
}

void Display::pointerLeave(wl_surface* surface) {
    if (auto* s = Surface::fromSurface(surface)) s->pointerLeave();
    m_pointerFocus = nullptr;
}

void Display::pointerMotion(double x, double y) {
    m_pointerX = x;
    m_pointerY = y;
    if (m_pointerFocus) m_pointerFocus->pointerMotion(x, y);
}

void Display::pointerButton(uint32_t button, bool pressed) {
    if (debugFocus()) log::info("wl_pointer.button {} {} focus={}", button, pressed, static_cast<void*>(m_pointerFocus));
    if (m_pointerFocus) m_pointerFocus->pointerButton(button, pressed);
}

void Display::pointerAxis(uint32_t axis, double value) {
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) m_scrollAccum += value;
}

void Display::pointerAxisSource(uint32_t source) {
    m_axisPrecise = source == WL_POINTER_AXIS_SOURCE_FINGER || source == WL_POINTER_AXIS_SOURCE_CONTINUOUS;
}

void Display::pointerAxisStop(uint32_t axis) {
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) m_axisStopped = true;
}

void Display::pointerFrame() {
    // Scroll views take the exact distance; everything else gets steps
    if (m_scrollAccum != 0 && m_pointerFocus && m_pointerFocus->pointerScrollPixels(static_cast<float>(m_scrollAccum), m_axisPrecise)) {
        m_scrollAccum = 0;
    } else if (std::abs(m_scrollAccum) >= 10 && m_pointerFocus) {
        // One notch is 10 units on most mice; touchpads send many small deltas
        m_pointerFocus->pointerScroll(m_scrollAccum > 0 ? 1 : -1);
        m_scrollAccum = 0;
    }
    if (m_axisStopped && m_pointerFocus) m_pointerFocus->pointerScrollEnd();
    m_axisStopped = false;
    m_axisPrecise = false; // each frame names its source again
}

void Display::forget(Surface* surface) {
    if (m_pointerFocus == surface) m_pointerFocus = nullptr;
    if (m_keyboardFocus == surface) {
        m_keyboardFocus = nullptr;
        if (m_repeatTimer) EventLoop::get().cancel(m_repeatTimer);
        m_repeatTimer = 0;
    }
}

// ── Keyboard ──

void Display::keymap(int fd, uint32_t size) {
    if (debugFocus()) log::info("wl_keyboard.keymap {}", size);
    void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    xkb_keymap* keymap = xkb_keymap_new_from_string(m_xkb, static_cast<const char*>(map),
                                                   XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!keymap) return;
    if (m_xkbState) xkb_state_unref(m_xkbState);
    if (m_keymap) xkb_keymap_unref(m_keymap);
    m_keymap = keymap;
    m_xkbState = xkb_state_new(keymap);
}

void Display::keyboardEnter(wl_surface* surface) {
    if (debugFocus()) log::info("wl_keyboard.enter {}", static_cast<void*>(surface));
    m_keyboardFocus = Surface::fromSurface(surface);
    if (m_keyboardFocus) m_keyboardFocus->keyboardFocus(true);
}

void Display::keyboardLeave(wl_surface* surface) {
    if (debugFocus()) log::info("wl_keyboard.leave {}", static_cast<void*>(surface));
    if (auto* s = Surface::fromSurface(surface)) s->keyboardFocus(false);
    m_keyboardFocus = nullptr;
    if (m_repeatTimer) EventLoop::get().cancel(m_repeatTimer);
    m_repeatTimer = 0;
}

void Display::modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    if (m_xkbState) xkb_state_update_mask(m_xkbState, depressed, latched, locked, 0, 0, group);
}

void Display::repeatInfo(int32_t rate, int32_t delay) {
    m_repeatRate = rate;
    m_repeatDelay = delay;
}

void Display::sendKey(uint32_t key, bool repeat) {
    if (!m_xkbState || !m_keyboardFocus) return;
    const xkb_keycode_t code = key + 8; // evdev → xkb
    ui::KeyEvent ev;
    ev.sym = xkb_state_key_get_one_sym(m_xkbState, code);
    char buf[64];
    if (xkb_state_key_get_utf8(m_xkbState, code, buf, sizeof buf) > 0) ev.text = buf;
    auto active = [&](const char* mod) {
        return xkb_state_mod_name_is_active(m_xkbState, mod, XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    if (active(XKB_MOD_NAME_SHIFT)) ev.mods |= ui::ModShift;
    if (active(XKB_MOD_NAME_CTRL)) ev.mods |= ui::ModCtrl;
    if (active(XKB_MOD_NAME_ALT)) ev.mods |= ui::ModAlt;
    if (active(XKB_MOD_NAME_LOGO)) ev.mods |= ui::ModSuper;
    // Control characters are not text
    if (!ev.text.empty() && static_cast<unsigned char>(ev.text[0]) < 0x20) ev.text.clear();
    if (ev.text == "\x7f") ev.text.clear();
    ev.repeat = repeat;
    m_keyboardFocus->key(ev);
}

void Display::key(uint32_t key, bool pressed) {
    if (debugFocus()) log::info("wl_keyboard.key {} {} focus={}", key, pressed, static_cast<void*>(m_keyboardFocus));
    if (!pressed) {
        if (key == m_repeatKey && m_repeatTimer) {
            EventLoop::get().cancel(m_repeatTimer);
            m_repeatTimer = 0;
        }
        return;
    }
    sendKey(key, false);
    if (m_repeatTimer) EventLoop::get().cancel(m_repeatTimer);
    m_repeatTimer = 0;
    if (m_repeatRate <= 0 || !m_keymap || !xkb_keymap_key_repeats(m_keymap, key + 8)) return;
    m_repeatKey = key;
    m_repeatTimer = EventLoop::get().addTimer(m_repeatDelay, [this, key] {
        sendKey(key, true);
        m_repeatTimer = EventLoop::get().addTimer(1000 / m_repeatRate, [this, key] { sendKey(key, true); }, true);
    });
}


int Display::fd() const {
    return wl_display_get_fd(m_display);
}

void Display::prepare() {
    if (m_reading) wl_display_cancel_read(m_display);
    while (wl_display_prepare_read(m_display) != 0) wl_display_dispatch_pending(m_display);
    m_reading = true;
    if (wl_display_flush(m_display) < 0 && errno != EAGAIN) {
        log::error("lost the Wayland connection");
        EventLoop::get().quit(1);
    }
}

void Display::dispatch(short revents) {
    if (m_reading) {
        if (revents & POLLIN)
            wl_display_read_events(m_display);
        else
            wl_display_cancel_read(m_display);
        m_reading = false;
    }
    if (revents & (POLLHUP | POLLERR)) {
        log::error("the compositor closed the connection");
        EventLoop::get().quit(1);
        return;
    }
    wl_display_dispatch_pending(m_display);
}

} // namespace m3::wl
