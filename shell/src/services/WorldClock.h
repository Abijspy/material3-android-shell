#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <string>
#include <vector>

namespace m3 {

// World clocks (the clock panel's World tab, the World clock desktop
// widget): the time zones chosen, in /clock/zones as their ids
// ("Asia/Tokyo"), each one's time, day and offset from here
class WorldClock {
public:
    struct Place {
        std::string id, city, country;
        std::string time;   // "10:27" (or "10:27 AM", as the clocks are set)
        std::string day;    // "Today", "Tomorrow", "Yesterday"
        std::string offset; // "+3:30 h", "−10 h", "Same time"
    };
    static WorldClock& get();
    void start(); // the minute's tick
    std::vector<std::string> zones() const;
    std::vector<Place> places() const;
    void add(const std::string& id);
    void remove(const std::string& id);
    Signal<> changed; // the list, and each minute

private:
    WorldClock() = default;
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3
