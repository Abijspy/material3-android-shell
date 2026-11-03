#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <string>

namespace m3 {

// The dark theme on a schedule (Look & feel → Colours → Automatically):
// /background/darkAuto "sun" from sunset to sunrise where you are (the
// weather's place, else your time zone's), "custom" from /background/darkFrom
// to /background/darkTo, "off" (or unset) as you leave it. It switches at
// those times only, so turning it over by hand holds until the next one.
class DarkSchedule {
public:
    static DarkSchedule& get();
    void start();

    std::string mode() const; // "off", "sun", "custom"
    // Dark at `now` by the schedule (false when it's off)
    bool wantsDark(std::time_t now) const;
    // "Dark from 17:43 to 06:03", for Settings ("" when off)
    std::string summary() const;

    Signal<> changed;

private:
    DarkSchedule() = default;
    void check(bool force);
    bool where(double& lat, double& lon, std::string& place) const;
    // Today's sunset and the sunrise it ends with, as local minutes past
    // midnight; false when the sun doesn't rise or set today (`dark`: the
    // polar night)
    bool sunMinutes(std::time_t now, int& darkFrom, int& darkTo, bool& dark) const;
    EventLoop::TimerId m_timer = 0;
    int m_last = -1; // what the schedule last said: 0 light, 1 dark
    std::string m_shape;
};

} // namespace m3
