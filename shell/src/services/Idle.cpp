#include "services/Idle.h"

#include "core/Config.h"
#include "services/Hyprland.h"
#include "wayland/Display.h"

#include "ext-idle-notify-v1-client-protocol.h"

#include <wayland-client.h>

namespace m3 {

namespace {

const ext_idle_notification_v1_listener kListener = {
    .idled = [](void*, ext_idle_notification_v1* n) { Idle::get().idled(n); },
    .resumed = [](void*, ext_idle_notification_v1* n) { Idle::get().resumed(n); },
};

void screens(bool on) {
    Hyprland::get().dispatch(on ? "hl.dsp.dpms(\"on\")" : "hl.dsp.dpms(\"off\")", on ? "dpms on" : "dpms off");
}

} // namespace

Idle& Idle::get() {
    static Idle instance;
    return instance;
}

void Idle::start() {
    arm();
    Config::get().changed.connectForever([this] {
        disarm();
        arm();
    });
}

void Idle::arm() {
    auto& d = wl::Display::get();
    auto* notifier = static_cast<ext_idle_notifier_v1*>(d.idleNotifier());
    if (!notifier || !d.seat() || m_inhibited) return;
    auto add = [&](int seconds, bool lock) {
        if (seconds <= 0) return;
        auto* n = ext_idle_notifier_v1_get_idle_notification(notifier, static_cast<uint32_t>(seconds) * 1000, d.seat());
        ext_idle_notification_v1_add_listener(n, &kListener, nullptr);
        m_watches.push_back({n, lock});
    };
    const auto& cfg = Config::get();
    add(cfg.value<int>("/idle/lockAfter", 600), true);
    add(cfg.value<int>("/idle/screenOffAfter", 900), false);
}

void Idle::disarm() {
    for (auto& w : m_watches) ext_idle_notification_v1_destroy(static_cast<ext_idle_notification_v1*>(w.notification));
    m_watches.clear();
}

void Idle::setInhibited(bool on) {
    if (on == m_inhibited) return;
    m_inhibited = on;
    disarm();
    arm();
    changed.emit();
}

void Idle::idled(void* notification) {
    for (const auto& w : m_watches) {
        if (w.notification != notification) continue;
        if (w.lock) {
            lockRequested.emit();
        } else {
            m_screenOff = true;
            screens(false);
        }
    }
}

void Idle::resumed(void* notification) {
    for (const auto& w : m_watches)
        if (w.notification == notification && !w.lock && m_screenOff) {
            m_screenOff = false;
            screens(true);
        }
}

} // namespace m3
