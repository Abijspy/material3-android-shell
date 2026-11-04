#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <functional>
#include <string>

namespace m3 {

// Something on a schedule: from sunset to sunrise where you are (the
// weather's place, else your time zone's), or between two times. <key>Auto
// "sun", "custom" or "off", <key>From and <key>To ("HH:MM"). It switches at
// those times only, so a turn by hand holds until the next one. The dark
// theme (darkSchedule) and night light (nightSchedule)
class SunSchedule {
public:
    struct Spec {
        std::string key;      // "/background/dark": /background/darkAuto, …From, …To
        std::string from, to; // the set times' defaults
        std::string what;     // the summary's word: "Dark from sunset at …"
        std::function<bool()> current;
        std::function<void(bool)> apply;
        std::string name;     // for the log
    };
    explicit SunSchedule(Spec spec) : m_spec(std::move(spec)) {}
    void start();

    std::string mode() const; // "off", "sun", "custom"
    bool wants(std::time_t now) const; // on by the schedule at `now` (false when off)
    std::string summary() const;       // "Dark from 17:43 to 06:03" ("" when off)
    const std::string& key() const { return m_spec.key; }
    const std::string& defaultFrom() const { return m_spec.from; }
    const std::string& defaultTo() const { return m_spec.to; }
    Signal<> changed;

private:
    void check(bool force);
    bool sunMinutes(std::time_t now, int& from, int& to, bool& on) const;
    Spec m_spec;
    EventLoop::TimerId m_timer = 0;
    int m_last = -1;
    std::string m_shape;
};

SunSchedule& darkSchedule();
SunSchedule& nightSchedule();

// Where the schedules find the sun: the weather's place, else the time
// zone's city; false when neither is known
bool scheduleLocation(double& lat, double& lon, std::string& place);

} // namespace m3
