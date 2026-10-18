#pragma once

#include "core/Signal.h"

#include <vector>

namespace m3 {

// Idle behaviour through ext-idle-notify-v1: lock after /idle/lockAfter
// seconds and switch screens off after /idle/screenOffAfter (0 disables
// either). "Caffeine" inhibits both.
class Idle {
public:
    static Idle& get();

    void start(); // after the Wayland connection is up
    bool inhibited() const { return m_inhibited; }
    void setInhibited(bool on);

    Signal<> lockRequested;
    Signal<> changed;

    // Protocol callbacks
    void idled(void* notification);
    void resumed(void* notification);

private:
    Idle() = default;
    void arm();
    void disarm();

    struct Watch {
        void* notification = nullptr; // ext_idle_notification_v1*
        bool lock = false;            // else: screen off
    };
    std::vector<Watch> m_watches;
    bool m_inhibited = false, m_screenOff = false;
};

} // namespace m3
