#pragma once

#include "core/EventLoop.h"

#include <functional>
#include <string>

namespace m3 {

// Caps Lock and Num Lock as the keyboard's lights say (/sys/class/leds/
// *::capslock, *::numlock), looked at every 300 ms (sysfs tells no one);
// `changed` with which and whether it's on. Whatever turned it (any
// keyboard, any window focused)
class LockKeys {
public:
    static LockKeys& get();
    void start(std::function<void(const std::string& which, bool on)> changed);

private:
    LockKeys() = default;
    void poll();
    std::string m_caps, m_num; // the lights' brightness files
    int m_capsOn = -1, m_numOn = -1;
    std::function<void(const std::string&, bool)> m_changed;
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3
