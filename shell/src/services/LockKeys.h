#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <string>

namespace m3 {

// Caps Lock and Num Lock as the keyboard's lights say (/sys/class/leds/
// *::capslock, *::numlock), looked at every 300 ms once started (sysfs
// tells no one), whatever turned them (any keyboard, any window focused):
// the OSD, and the lock screen's and login screen's password field
class LockKeys {
public:
    static LockKeys& get();
    void start(); // more than once is fine
    bool capsOn() const { return m_capsOn == 1; }
    bool numOn() const { return m_numOn == 1; }
    Signal<const std::string&, bool> toggled; // "caps" or "num", and whether on now (not the first look)

private:
    LockKeys() = default;
    void poll();
    std::string m_caps, m_num; // the lights' brightness files
    int m_capsOn = -1, m_numOn = -1;
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3
