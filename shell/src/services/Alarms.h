#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace m3 {

// Alarms and timers, as Android's Clock: alarms at a time of day, once or on
// chosen weekdays; timers counting down, paused, resumed, a minute added.
// Kept in ~/.local/share/material3ui/clock.json, so a running timer carries
// on across a restart. Due times are the wall clock's (a timerfd on
// CLOCK_REALTIME): what comes due while the computer sleeps goes off as it
// wakes, except an alarm by then long gone, which is said as missed.
// Whatever goes off rings (its sound over and over, louder each time, the
// screens woken) until stopped or snoozed, or /clock/silenceAfter minutes.
class Alarms {
public:
    struct Alarm {
        std::string id;
        int hour = 7, minute = 0;
        std::vector<int> days; // 0 Sunday … 6 Saturday; none: once, the next time it comes round
        std::string label;
        bool enabled = true;
        std::time_t snoozed = 0; // rings again then
    };
    struct Timer {
        std::string id;
        std::string label;
        int64_t length = 0; // ms, as set (with the minutes added)
        int64_t end = 0;    // running: when it's up (ms since the epoch); 0 paused
        int64_t left = 0;   // paused: ms to go
        bool running() const { return end > 0; }
        int64_t remaining(int64_t nowMs) const; // ms to go, never below 0
    };
    struct Ringing {
        std::string id; // the alarm's or timer's
        bool timer = false;
        std::string label;
        std::time_t since = 0;
    };

    static Alarms& get();

    // ── Alarms ──
    const std::vector<Alarm>& alarms() const { return m_alarms; }
    const Alarm* alarm(const std::string& id) const;
    // Adds one (no id yet) or replaces it; returns its id
    std::string save(Alarm alarm);
    void remove(const std::string& id);
    void setEnabled(const std::string& id, bool on);
    // When it goes off next after `from` (0 while it's off)
    static std::time_t next(const Alarm& a, std::time_t from);
    // The alarm going off soonest, and when (nullptr: none on)
    const Alarm* upcoming(std::time_t* when = nullptr) const;
    // "Every day", "Weekdays", "Mon, Wed, Fri", "Tomorrow", "Today"
    static std::string daysText(const Alarm& a);
    // "In 7 h 12 min", "In 3 min", "Now"
    static std::string untilText(std::time_t when);

    // ── Timers ──
    const std::vector<Timer>& timers() const { return m_timers; }
    std::string startTimer(int64_t ms, const std::string& label = {});
    void pause(const std::string& id);
    void resume(const std::string& id);
    void addTime(const std::string& id, int64_t ms); // +1:00
    void reset(const std::string& id);               // back to its length, paused
    void removeTimer(const std::string& id);
    bool anyRunning() const;
    // "25", "1:30", "1h 20m", "90s" → ms; 0 if it isn't a length
    static int64_t parseLength(const std::string& text);

    // ── Going off ──
    const std::vector<Ringing>& ringing() const { return m_ringing; }
    void stop(const std::string& id);   // an alarm stops; a timer goes back to its length
    void snooze(const std::string& id); // an alarm rings again in /clock/snooze minutes
    void stopAll();

    // ── Formats ──
    static int64_t nowMs();
    // As the clock reads: "07:00", or "7:00 am" with the 12-hour clock on
    static std::string timeText(int hour, int minute);
    // "4:05", "1:02:03"
    static std::string countdown(int64_t ms);
    // The sounds to choose from (Settings), as file and name
    static const std::vector<std::pair<std::string, std::string>>& sounds();
    // Plays the chosen sound once (Settings' preview), or stops it
    void preview(bool play);

    Signal<> changed; // alarms, timers or what's ringing
    Signal<> rang;    // something began ringing: the shell shows it

    // The wall-clock wake-up (a timerfd), defined in Alarms.cpp
    class Wake;

private:
    Alarms();
    void load();
    void write() const;
    void schedule();
    void due();
    void ring(const std::string& id, bool timer, const std::string& label);
    void play();
    void silence();
    void settle(); // what's ringing changed: the sound goes on, or stops
    std::string newId() const;

    std::vector<Alarm> m_alarms;
    std::vector<Timer> m_timers;
    std::vector<Ringing> m_ringing;
    std::map<std::string, std::time_t> m_armed; // alarm → when it goes off next
    std::unique_ptr<Wake> m_wake;
    // The sound: a player at a time, over and over while anything rings
    int m_player = -1, m_round = 0, m_failures = 0, m_preview = -1;
    unsigned m_generation = 0;
    int64_t m_started = 0;
    EventLoop::TimerId m_gap = 0, m_silenceTimer = 0;
};

} // namespace m3
