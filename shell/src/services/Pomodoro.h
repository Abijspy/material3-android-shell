#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <string>

namespace m3 {

// A focus timer (Pomodoro): work, a short break, again, and a long break
// every few rounds (/pomodoro/work, /shortBreak, /longBreak minutes,
// /rounds). While working, Focus mode and Do not disturb are on (with
// /pomodoro/quiet), and as each part ends a chime and a notification say
// what's next. In the clock panel's Focus tab and `msg pomodoro`
class Pomodoro {
public:
    enum class Phase { Idle, Work, ShortBreak, LongBreak };
    static Pomodoro& get();
    Phase phase() const { return m_phase; }
    bool running() const { return m_phase != Phase::Idle && !m_paused; }
    bool paused() const { return m_paused; }
    int remaining() const;        // seconds left of this part
    int length() const;           // this part's, in seconds
    int round() const { return m_round; } // 1…rounds
    int rounds() const;
    std::string phaseName() const; // "Focus", "Short break", "Long break"

    void start();  // a work part, from round 1
    void pause();
    void resume();
    void skip();   // on to the next part
    void stop();

    Signal<> changed; // each second while it runs, and on every change

private:
    Pomodoro() = default;
    void enter(Phase p);
    void tick();
    void quiet(bool on);
    Phase m_phase = Phase::Idle;
    bool m_paused = false;
    int m_round = 1;
    std::time_t m_ends = 0; // running: when this part ends
    int m_left = 0;         // paused: seconds left
    EventLoop::TimerId m_timer = 0;
    // What Focus mode and Do not disturb were before work began
    bool m_quieted = false, m_wasFocus = false, m_wasDnd = false;
};

} // namespace m3
