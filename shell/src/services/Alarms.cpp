#include "services/Alarms.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Hooks.h"
#include "services/Idle.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <sys/timerfd.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string dataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.local/share") + "/material3ui";
}

const char* kSounds = "/usr/share/sounds/freedesktop/stereo/";

// An alarm this late (the computer was asleep, or off) is said as missed
// instead of ringing: nobody wants 07:00's alarm at 10:30
constexpr int kMissedAfter = 10 * 60;

void notifyMissed(const Alarms::Alarm& a) {
    run({"notify-send", "-a", "Clock", "-i", "alarm", "Missed alarm",
         Alarms::timeText(a.hour, a.minute) + (a.label.empty() ? "" : " · " + a.label)},
        nullptr);
}

} // namespace

// Wakes the loop at a wall-clock time, sleep or not: CLOCK_REALTIME runs on
// while suspended, so what came due meanwhile fires on waking. The clock
// being set (by hand, by NTP) wakes it too, to look again.
class Alarms::Wake : public EventSource {
public:
    explicit Wake(std::function<void()> fire) : m_fire(std::move(fire)) {
        m_fd = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
        if (m_fd >= 0) EventLoop::get().add(this);
    }
    ~Wake() override {
        if (m_fd < 0) return;
        EventLoop::get().remove(this);
        close(m_fd);
    }
    // ms since the epoch; 0 disarms
    void at(int64_t ms) {
        if (m_fd < 0) return;
        itimerspec its{};
        its.it_value.tv_sec = ms / 1000;
        its.it_value.tv_nsec = ms % 1000 * 1000000;
        timerfd_settime(m_fd, ms > 0 ? TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET : 0, &its, nullptr);
    }
    int fd() const override { return m_fd; }
    void dispatch(short) override {
        uint64_t expirations = 0;
        // EAGAIN: not due (the loop dispatches every source); ECANCELED: the
        // clock was set, so look again
        if (read(m_fd, &expirations, sizeof expirations) < 0 && errno == EAGAIN) return;
        EventLoop::get().defer(m_fire);
    }

private:
    int m_fd = -1;
    std::function<void()> m_fire;
};

Alarms& Alarms::get() {
    static Alarms instance;
    return instance;
}

Alarms::Alarms() {
    m_wake = std::make_unique<Wake>([this] { due(); });
    load();
    // Timers that ran out while the shell wasn't running go off now
    EventLoop::get().defer([this] { due(); });
}

int64_t Alarms::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t Alarms::Timer::remaining(int64_t now) const {
    return std::max<int64_t>(0, running() ? end - now : left);
}

std::string Alarms::newId() const {
    static std::mt19937 rng{std::random_device{}()};
    for (;;) {
        std::string id = std::format("{:08x}", rng());
        const bool taken = std::ranges::any_of(m_alarms, [&](const Alarm& a) { return a.id == id; }) ||
                           std::ranges::any_of(m_timers, [&](const Timer& t) { return t.id == id; });
        if (!taken) return id;
    }
}

// ── Storage ──

void Alarms::load() {
    std::ifstream in(dataDir() + "/clock.json");
    if (!in) return;
    try {
        const auto j = nlohmann::json::parse(in);
        for (const auto& a : j.value("alarms", nlohmann::json::array())) {
            Alarm x;
            x.id = a.value("id", "");
            x.hour = std::clamp(a.value("hour", 7), 0, 23);
            x.minute = std::clamp(a.value("minute", 0), 0, 59);
            for (const auto& d : a.value("days", nlohmann::json::array()))
                if (d.is_number_integer() && d.get<int>() >= 0 && d.get<int>() < 7) x.days.push_back(d.get<int>());
            x.label = a.value("label", "");
            x.enabled = a.value("enabled", true);
            x.snoozed = a.value("snoozed", static_cast<std::time_t>(0));
            if (x.id.empty()) x.id = newId();
            m_alarms.push_back(std::move(x));
        }
        for (const auto& t : j.value("timers", nlohmann::json::array())) {
            Timer x;
            x.id = t.value("id", "");
            x.label = t.value("label", "");
            x.length = t.value("length", static_cast<int64_t>(0));
            x.end = t.value("end", static_cast<int64_t>(0));
            x.left = t.value("left", x.length);
            if (x.id.empty()) x.id = newId();
            if (x.length > 0) m_timers.push_back(std::move(x));
        }
    } catch (const std::exception& e) {
        log::warn("clock.json: {}", e.what());
    }
}

void Alarms::write() const {
    nlohmann::json alarms = nlohmann::json::array(), timers = nlohmann::json::array();
    for (const auto& a : m_alarms) {
        nlohmann::json j = {{"id", a.id}, {"hour", a.hour}, {"minute", a.minute}, {"days", a.days}, {"label", a.label}, {"enabled", a.enabled}};
        if (a.snoozed) j["snoozed"] = a.snoozed;
        alarms.push_back(j);
    }
    for (const auto& t : m_timers)
        timers.push_back({{"id", t.id}, {"label", t.label}, {"length", t.length}, {"end", t.end}, {"left", t.left}});
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    const std::string file = dataDir() + "/clock.json";
    std::ofstream(file + ".tmp") << nlohmann::json{{"alarms", alarms}, {"timers", timers}}.dump(2);
    fs::rename(file + ".tmp", file, ec);
}

// ── Alarms ──

const Alarms::Alarm* Alarms::alarm(const std::string& id) const {
    const auto it = std::ranges::find(m_alarms, id, &Alarm::id);
    return it == m_alarms.end() ? nullptr : &*it;
}

std::string Alarms::save(Alarm a) {
    std::ranges::sort(a.days);
    a.days.erase(std::unique(a.days.begin(), a.days.end()), a.days.end());
    a.snoozed = 0;
    if (a.id.empty()) a.id = newId();
    m_armed.erase(a.id); // its time may have changed
    if (auto it = std::ranges::find(m_alarms, a.id, &Alarm::id); it != m_alarms.end()) *it = a;
    else m_alarms.push_back(a);
    // In time order, as Android lists them
    std::ranges::stable_sort(m_alarms, {}, [](const Alarm& x) { return x.hour * 60 + x.minute; });
    write();
    schedule();
    changed.emit();
    return a.id;
}

void Alarms::remove(const std::string& id) {
    std::erase_if(m_alarms, [&](const Alarm& a) { return a.id == id; });
    std::erase_if(m_ringing, [&](const Ringing& r) { return r.id == id; });
    m_armed.erase(id);
    write();
    schedule();
    settle();
    changed.emit();
}

void Alarms::setEnabled(const std::string& id, bool on) {
    auto it = std::ranges::find(m_alarms, id, &Alarm::id);
    if (it == m_alarms.end() || it->enabled == on) return;
    it->enabled = on;
    it->snoozed = 0;
    m_armed.erase(id);
    write();
    schedule();
    changed.emit();
}

const Alarms::Alarm* Alarms::upcoming(std::time_t* when) const {
    const Alarm* best = nullptr;
    std::time_t soonest = 0;
    const std::time_t now = std::time(nullptr);
    for (const auto& a : m_alarms) {
        std::time_t at = a.snoozed;
        if (a.enabled) {
            const auto it = m_armed.find(a.id);
            const std::time_t armed = it != m_armed.end() && it->second ? it->second : next(a, now);
            if (armed && (!at || armed < at)) at = armed;
        }
        if (at && (!best || at < soonest)) {
            best = &a;
            soonest = at;
        }
    }
    if (when) *when = soonest;
    return best;
}

// ── Timers ──

std::string Alarms::startTimer(int64_t ms, const std::string& label) {
    if (ms <= 0) return {};
    Timer t;
    t.id = newId();
    t.label = label;
    t.length = t.left = ms;
    t.end = nowMs() + ms;
    m_timers.push_back(t);
    write();
    schedule();
    changed.emit();
    return t.id;
}

void Alarms::pause(const std::string& id) {
    auto it = std::ranges::find(m_timers, id, &Timer::id);
    if (it == m_timers.end() || !it->running()) return;
    it->left = it->remaining(nowMs());
    it->end = 0;
    write();
    schedule();
    changed.emit();
}

void Alarms::resume(const std::string& id) {
    auto it = std::ranges::find(m_timers, id, &Timer::id);
    if (it == m_timers.end() || it->running() || it->left <= 0) return;
    it->end = nowMs() + it->left;
    write();
    schedule();
    changed.emit();
}

void Alarms::addTime(const std::string& id, int64_t ms) {
    auto it = std::ranges::find(m_timers, id, &Timer::id);
    if (it == m_timers.end()) return;
    const bool wasRinging = std::erase_if(m_ringing, [&](const Ringing& r) { return r.id == id; }) > 0;
    if (wasRinging) {
        // Up already: another minute from now, as Android's +1:00
        it->end = nowMs() + ms;
    } else if (it->running()) {
        it->end += ms;
    } else {
        it->left += ms;
    }
    it->length += ms;
    write();
    schedule();
    settle();
    changed.emit();
}

void Alarms::reset(const std::string& id) {
    auto it = std::ranges::find(m_timers, id, &Timer::id);
    if (it == m_timers.end()) return;
    it->end = 0;
    it->left = it->length;
    std::erase_if(m_ringing, [&](const Ringing& r) { return r.id == id; });
    write();
    schedule();
    settle();
    changed.emit();
}

void Alarms::removeTimer(const std::string& id) {
    std::erase_if(m_timers, [&](const Timer& t) { return t.id == id; });
    std::erase_if(m_ringing, [&](const Ringing& r) { return r.id == id; });
    write();
    schedule();
    settle();
    changed.emit();
}

bool Alarms::anyRunning() const {
    return std::ranges::any_of(m_timers, &Timer::running);
}

// ── Going off ──

void Alarms::schedule() {
    const std::time_t now = std::time(nullptr);
    int64_t soonest = 0;
    auto consider = [&](int64_t ms) {
        if (ms > 0 && (!soonest || ms < soonest)) soonest = ms;
    };
    std::map<std::string, std::time_t> armed;
    for (const auto& a : m_alarms) {
        if (a.snoozed) consider(a.snoozed * 1000);
        if (!a.enabled) continue;
        const auto it = m_armed.find(a.id);
        const std::time_t at = it != m_armed.end() && it->second ? it->second : next(a, now);
        armed[a.id] = at;
        consider(at * 1000);
    }
    m_armed = std::move(armed);
    for (const auto& t : m_timers)
        if (t.running()) consider(t.end);
    m_wake->at(soonest);
}

void Alarms::due() {
    const int64_t now = nowMs();
    const std::time_t secs = now / 1000;
    bool dirty = false;
    for (auto& t : m_timers)
        if (t.running() && t.end <= now + 20) {
            t.end = 0;
            t.left = 0;
            dirty = true;
            ring(t.id, true, t.label);
        }
    for (auto& a : m_alarms) {
        if (a.snoozed && a.snoozed <= secs) {
            a.snoozed = 0;
            dirty = true;
            ring(a.id, false, a.label);
            continue;
        }
        auto it = m_armed.find(a.id);
        if (it == m_armed.end() || !it->second || it->second > secs) continue;
        const std::time_t at = it->second;
        it->second = 0; // armed afresh, for the next time round
        // Once: off once it's gone off
        if (a.days.empty()) {
            a.enabled = false;
            dirty = true;
        }
        if (secs - at > kMissedAfter) notifyMissed(a);
        else ring(a.id, false, a.label);
    }
    if (dirty) write();
    schedule();
    changed.emit();
}

void Alarms::ring(const std::string& id, bool timer, const std::string& label) {
    std::erase_if(m_ringing, [&](const Ringing& r) { return r.id == id; });
    m_ringing.push_back({id, timer, label, std::time(nullptr)});
    Idle::get().wake();
    if (timer) {
        const auto it = std::ranges::find(m_timers, id, &Timer::id);
        Hooks::get().fire("timer_finished", {{"M3_TIMER_LABEL", label}, {"M3_TIMER_LENGTH", countdown(it != m_timers.end() ? it->length : 0)}});
    } else if (const auto* a = alarm(id)) {
        Hooks::get().fire("alarm_ringing", {{"M3_ALARM_LABEL", label}, {"M3_ALARM_TIME", std::format("{:02}:{:02}", a->hour, a->minute)}});
    }
    settle();
    rang.emit();
}

void Alarms::stop(const std::string& id) {
    const auto it = std::ranges::find(m_ringing, id, &Ringing::id);
    if (it == m_ringing.end()) {
        // Not ringing: a snoozed alarm's snooze ends
        auto a = std::ranges::find(m_alarms, id, &Alarm::id);
        if (a == m_alarms.end() || !a->snoozed) return;
        a->snoozed = 0;
    } else if (it->timer) {
        m_ringing.erase(it);
        return reset(id);
    } else {
        m_ringing.erase(it);
    }
    write();
    schedule();
    settle();
    changed.emit();
}

void Alarms::snooze(const std::string& id) {
    const auto it = std::ranges::find(m_ringing, id, &Ringing::id);
    auto a = std::ranges::find(m_alarms, id, &Alarm::id);
    if (it == m_ringing.end() || it->timer || a == m_alarms.end()) return;
    m_ringing.erase(it);
    a->snoozed = std::time(nullptr) + std::max(1, Config::get().value<int>("/clock/snooze", 10)) * 60;
    write();
    schedule();
    settle();
    changed.emit();
}

void Alarms::stopAll() {
    std::vector<std::string> ids;
    for (const auto& r : m_ringing) ids.push_back(r.id);
    for (const auto& id : ids) stop(id);
}

void Alarms::settle() {
    if (m_silenceTimer) EventLoop::get().cancel(m_silenceTimer);
    m_silenceTimer = 0;
    if (m_ringing.empty()) return silence();
    if (m_player < 0 && !m_gap) {
        m_round = m_failures = 0;
        play();
    }
    // Nobody about: quiet after a while (an alarm then counts as missed)
    const int after = Config::get().value<int>("/clock/silenceAfter", 10);
    if (after <= 0) return;
    std::time_t oldest = m_ringing.front().since;
    for (const auto& r : m_ringing) oldest = std::min(oldest, r.since);
    const int64_t wait = std::max<int64_t>(1000, (oldest + after * 60) * 1000LL - nowMs());
    m_silenceTimer = EventLoop::get().addTimer(static_cast<int>(wait), [this, after] {
        m_silenceTimer = 0;
        const std::time_t now = std::time(nullptr);
        std::vector<Ringing> gone;
        for (const auto& r : m_ringing)
            if (r.since + after * 60 <= now + 1) gone.push_back(r);
        for (const auto& r : gone) {
            if (const auto* a = r.timer ? nullptr : alarm(r.id)) notifyMissed(*a);
            stop(r.id);
        }
        settle();
    });
}

const std::vector<std::pair<std::string, std::string>>& Alarms::sounds() {
    static const std::vector<std::pair<std::string, std::string>> all = [] {
        std::vector<std::pair<std::string, std::string>> out;
        const std::pair<const char*, const char*> known[] = {{"alarm-clock-elapsed.oga", "Alarm clock"},
                                                             {"phone-incoming-call.oga", "Ringing phone"},
                                                             {"bell.oga", "Bell"},
                                                             {"complete.oga", "Chime"},
                                                             {"message-new-instant.oga", "Pop"}};
        std::error_code ec;
        for (const auto& [file, name] : known)
            if (fs::exists(std::string(kSounds) + file, ec)) out.emplace_back(std::string(kSounds) + file, name);
        return out;
    }();
    return all;
}

namespace {

// PipeWire's player, else PulseAudio's; volume 0–1
std::vector<std::string> player(const std::string& file, double volume) {
    static const bool pipewire = [] {
        const char* path = std::getenv("PATH");
        std::string dirs = path ? path : "/usr/bin";
        for (size_t start = 0;;) {
            const size_t colon = dirs.find(':', start);
            if (access((dirs.substr(start, colon - start) + "/pw-play").c_str(), X_OK) == 0) return true;
            if (colon == std::string::npos) return false;
            start = colon + 1;
        }
    }();
    if (pipewire) return {"pw-play", "--volume", std::format("{:.2f}", volume), file};
    return {"paplay", std::format("--volume={}", static_cast<int>(volume * 65536)), file};
}

std::string chosenSound() {
    const std::string file = Config::get().value<std::string>("/clock/sound", "");
    std::error_code ec;
    if (!file.empty() && fs::exists(file, ec)) return file;
    const auto& all = Alarms::sounds();
    return all.empty() ? std::string() : all.front().first;
}

} // namespace

void Alarms::play() {
    const std::string file = chosenSound();
    if (m_ringing.empty() || file.empty()) return;
    // Louder each round, from a third (Settings → Gradually louder)
    const bool rise = Config::get().value<bool>("/clock/rise", true);
    const double volume = rise ? std::min(1.0, 0.35 + 0.15 * m_round) : 1.0;
    ++m_round;
    m_started = nowMs();
    const unsigned generation = m_generation;
    m_player = run(player(file, volume), [this, generation](int code, const std::string&) {
        if (generation != m_generation) return; // silenced meanwhile
        m_player = -1;
        // A player that can't play: a few tries, then just the card
        m_failures = code != 0 && nowMs() - m_started < 500 ? m_failures + 1 : 0;
        if (m_ringing.empty() || m_failures >= 3) return;
        m_gap = EventLoop::get().addTimer(600, [this, generation] {
            m_gap = 0;
            if (generation == m_generation) play();
        });
    });
}

void Alarms::silence() {
    ++m_generation;
    if (m_player > 0) kill(m_player, SIGTERM);
    m_player = -1;
    if (m_gap) EventLoop::get().cancel(m_gap);
    m_gap = 0;
}

void Alarms::preview(bool on) {
    if (m_preview > 0) kill(m_preview, SIGTERM);
    m_preview = -1;
    if (!on) return;
    const std::string file = chosenSound();
    if (file.empty()) return;
    m_preview = run(player(file, 0.6), [this](int, const std::string&) { m_preview = -1; });
}

// ── Formats ──

std::string Alarms::timeText(int hour, int minute) {
    const auto& c = Config::get();
    const std::string fmt = c.value<std::string>("/bar/clock/island/timeFormat", c.value<std::string>("/time/format", "hh:mm"));
    const bool twelve = fmt.find("AP") != std::string::npos || fmt.find("ap") != std::string::npos;
    if (!twelve) return std::format("{:02}:{:02}", hour, minute);
    return std::format("{}:{:02} {}", hour % 12 == 0 ? 12 : hour % 12, minute, hour < 12 ? "AM" : "PM");
}

} // namespace m3
