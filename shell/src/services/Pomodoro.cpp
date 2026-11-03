#include "services/Pomodoro.h"

#include "core/Config.h"
#include "core/Process.h"
#include "services/Notifications.h"
#include "services/Wellbeing.h"

#include <algorithm>
#include <filesystem>
#include <format>

namespace m3 {

namespace {
int minutes(const char* key, int fallback, int lo, int hi) {
    return std::clamp(Config::get().value<int>(std::string("/pomodoro/") + key, fallback), lo, hi);
}
// The part's end: heard whatever the sound effects say (as a timer is)
void chime() {
    const std::string path = "/usr/share/sounds/freedesktop/stereo/complete.oga";
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) run({"pw-play", "--media-role=event", path}, nullptr);
}
} // namespace

Pomodoro& Pomodoro::get() {
    static Pomodoro instance;
    return instance;
}

int Pomodoro::rounds() const {
    return minutes("rounds", 4, 2, 8);
}

int Pomodoro::length() const {
    switch (m_phase) {
    case Phase::Work: return minutes("work", 25, 5, 120) * 60;
    case Phase::ShortBreak: return minutes("shortBreak", 5, 1, 30) * 60;
    case Phase::LongBreak: return minutes("longBreak", 15, 5, 60) * 60;
    case Phase::Idle: break;
    }
    return 0;
}

int Pomodoro::remaining() const {
    if (m_phase == Phase::Idle) return 0;
    if (m_paused) return m_left;
    return std::max(0, static_cast<int>(m_ends - std::time(nullptr)));
}

std::string Pomodoro::phaseName() const {
    switch (m_phase) {
    case Phase::Work: return "Focus";
    case Phase::ShortBreak: return "Short break";
    case Phase::LongBreak: return "Long break";
    case Phase::Idle: break;
    }
    return "Ready";
}

void Pomodoro::quiet(bool on) {
    auto& w = Wellbeing::get();
    auto& n = Notifications::get();
    if (on && !m_quieted && Config::get().value<bool>("/pomodoro/quiet", true)) {
        m_quieted = true;
        m_wasFocus = w.focus();
        m_wasDnd = n.doNotDisturb();
        w.setFocus(true);
        n.setDoNotDisturb(true);
    } else if (!on && m_quieted) {
        // Back as they were (unless changed by hand meanwhile: then as left)
        m_quieted = false;
        if (w.focus()) w.setFocus(m_wasFocus);
        if (n.doNotDisturb()) n.setDoNotDisturb(m_wasDnd);
    }
}

void Pomodoro::enter(Phase p) {
    m_phase = p;
    m_paused = false;
    m_ends = std::time(nullptr) + length();
    quiet(p == Phase::Work);
    if (!m_timer) m_timer = EventLoop::get().addTimer(1000, [this] { tick(); }, true);
    changed.emit();
}

void Pomodoro::start() {
    m_round = 1;
    enter(Phase::Work);
}

void Pomodoro::tick() {
    if (m_phase == Phase::Idle) return;
    if (!m_paused && remaining() <= 0) {
        // This part's done: on to the next, said with a chime
        chime();
        const Phase was = m_phase;
        if (was == Phase::Work) {
            const bool longOne = m_round >= rounds();
            enter(longOne ? Phase::LongBreak : Phase::ShortBreak);
            run({"notify-send", "-a", "Focus timer", "-i", "alarm-symbolic", longOne ? "Time for a long break" : "Time for a break",
                 std::format("{} minutes: stand up, look away from the screen", length() / 60)},
                nullptr);
        } else {
            m_round = was == Phase::LongBreak ? 1 : m_round + 1;
            enter(Phase::Work);
            run({"notify-send", "-a", "Focus timer", "-i", "alarm-symbolic", "Back to focus",
                 std::format("Round {} of {}: {} minutes", m_round, rounds(), length() / 60)},
                nullptr);
        }
        return;
    }
    if (!m_paused) changed.emit();
}

void Pomodoro::pause() {
    if (m_phase == Phase::Idle || m_paused) return;
    m_left = remaining();
    m_paused = true;
    changed.emit();
}

void Pomodoro::resume() {
    if (!m_paused) return;
    m_paused = false;
    m_ends = std::time(nullptr) + m_left;
    changed.emit();
}

void Pomodoro::skip() {
    if (m_phase == Phase::Idle) return;
    m_paused = false;
    m_ends = std::time(nullptr); // the next tick moves on
    tick();
}

void Pomodoro::stop() {
    if (m_timer) EventLoop::get().cancel(m_timer);
    m_timer = 0;
    m_phase = Phase::Idle;
    m_paused = false;
    m_round = 1;
    quiet(false);
    changed.emit();
}

} // namespace m3
