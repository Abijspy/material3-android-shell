#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace m3 {

// Digital Wellbeing, as Android's: screen time per app (the focused
// window's, while unlocked, the screen on and someone at it), unlocks and
// notifications per day, kept 30 days in ~/.local/state/material3ui/
// wellbeing; app timers that close an app for the rest of the day once
// its time is up (a minute's warning first); Focus mode, which keeps
// distracting apps closed while it's on; and Bedtime, which turns the
// screen grey and Do not disturb on, on a schedule or by hand.
class Wellbeing {
public:
    struct Day {
        std::map<std::string, int> seconds;       // by app (window class)
        std::map<std::string, int> notifications; // by app name, as they say it
        int unlocks = 0;
        int total() const;
    };

    static Wellbeing& get();
    void start(); // after Hyprland and the lock screen are up

    const Day& today() const { return m_today; }
    Day day(int daysAgo) const; // 0 today, from disk for the rest

    // An app's name and icon from its window class (Apps' entries)
    static std::string nameOf(const std::string& cls);
    static std::string iconOf(const std::string& cls);

    // Timers: minutes a day, 0 none
    int limit(const std::string& cls) const;
    void setLimit(const std::string& cls, int minutes);
    bool timeUp(const std::string& cls) const;

    // Focus mode: these apps stay closed while it's on
    bool focus() const;
    void setFocus(bool on);
    std::vector<std::string> focusApps() const;
    void setFocusApp(const std::string& cls, bool distracting);

    // Reminders: a break after this many minutes on screen without one
    // (stepping away or locking resets it), and a daily goal, said once
    // when passed; 0 is off
    int breakEvery() const;
    int goal() const;
    int session() const { return m_session; } // seconds on screen since the last break

    // Bedtime: on a schedule (start, end as "HH:MM") or by hand
    bool bedtime() const { return m_bedtime; } // in effect now
    void setBedtimeNow(bool on);               // by hand, until the schedule's next change

    // The lock screen tells: counted as an unlock, and no screen time while locked
    std::function<bool()> locked;
    void unlocked();

    Signal<> changed;
    ~Wellbeing(); // a save still pending is written

private:
    Wellbeing() = default;
    void write();
    void tick();
    void flush(const std::string& cls); // time since the last count, to cls
    void enforce();
    void applyBedtime();
    bool scheduledBedtime() const;
    void load();
    void save();
    static std::string dateKey(std::time_t t);

    Day m_today;
    std::string m_key;      // today's date: a new day starts a new Day
    std::string m_current;  // the focused class being counted
    std::time_t m_since = 0; // counted up to here
    bool m_bedtime = false, m_bedtimeByHand = false, m_handOn = false, m_ourDnd = false;
    std::map<std::string, bool> m_warned; // a minute left, said once a day
    int m_session = 0;     // seconds on screen without a break
    bool m_goalSaid = false;
    void remind();
    std::map<std::string, std::time_t> m_closedAt; // closed for its timer: said once
    EventLoop::TimerId m_timer = 0, m_saveTimer = 0;
};

} // namespace m3
