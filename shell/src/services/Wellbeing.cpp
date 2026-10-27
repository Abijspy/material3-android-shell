#include "services/Wellbeing.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Effects.h"
#include "services/Compositor.h"
#include "services/Idle.h"
#include "services/Notifications.h"
#include "services/Search.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace m3 {

namespace {

constexpr int kKeepDays = 30;

std::string dir() {
    return paths::stateDir() + "/wellbeing";
}

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "22:30" → minutes after midnight
int minutesOf(const std::string& hhmm, int fallback) {
    int h = 0, m = 0;
    if (std::sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return fallback;
    return h * 60 + m;
}

void notify(const std::string& title, const std::string& body) {
    run({"notify-send", "-a", "Digital Wellbeing", "-i", "preferences-desktop", title, body}, nullptr);
}

// A day's file. The parts are named before they're walked: items() of a
// temporary (j.value(...)) would outlive it
Wellbeing::Day parseDay(const nlohmann::json& j) {
    Wellbeing::Day d;
    const auto seconds = j.value("seconds", nlohmann::json::object());
    for (const auto& [k, v] : seconds.items())
        if (v.is_number()) d.seconds[k] = v.get<int>();
    const auto notifications = j.value("notifications", nlohmann::json::object());
    for (const auto& [k, v] : notifications.items())
        if (v.is_number()) d.notifications[k] = v.get<int>();
    d.unlocks = j.value("unlocks", 0);
    return d;
}

} // namespace

int Wellbeing::Day::total() const {
    int t = 0;
    for (const auto& [_, s] : seconds) t += s;
    return t;
}

Wellbeing& Wellbeing::get() {
    static Wellbeing instance;
    return instance;
}

std::string Wellbeing::dateKey(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

void Wellbeing::start() {
    load();
    m_since = std::time(nullptr);
    // Past the goal already (a restart): said earlier today
    m_goalSaid = goal() > 0 && m_today.total() >= goal() * 60;
    auto& c = Compositor::get();
    m_current = c.activeAppId();
    // Counted up to each change of focus, then for the new window
    c.activeWindowChanged.connectForever([this] {
        flush(m_current);
        m_current = Compositor::get().activeAppId();
        enforce();
    });
    Notifications::get().posted.connectForever([this](uint32_t id) {
        // Its own reminders aren't interruptions to count
        if (const auto* n = Notifications::get().find(id); n && n->app != "Digital Wellbeing") {
            ++m_today.notifications[n->app.empty() ? "Other" : n->app];
            save();
        }
    });
    // Stepping away (or back): what came before counts, the gap doesn't
    Idle::get().changed.connectForever([this] { flush(m_current); });
    m_timer = EventLoop::get().addTimer(10000, [this] { tick(); }, true);
    // Settings changing (the schedule, greyscale, Focus): applied at once,
    // not at the next tick
    Config::get().changed.connectForever([this] {
        applyBedtime();
        enforce();
        changed.emit();
    });
    applyBedtime();
}

void Wellbeing::flush(const std::string& cls) {
    const std::time_t now = std::time(nullptr);
    const bool counting = !(locked && locked()) && !Idle::get().away();
    // A suspend shows as a long gap: never more than a tick and a bit
    const std::time_t span = std::min<std::time_t>(now - m_since, 30);
    if (counting && !cls.empty() && span > 0) {
        m_today.seconds[cls] += static_cast<int>(span);
        m_session += static_cast<int>(span);
    }
    // Away or locked: that was a break
    if (!counting) m_session = 0;
    m_since = now;
}

void Wellbeing::tick() {
    // A new day starts afresh (yesterday's is already on disk)
    const std::string key = dateKey(std::time(nullptr));
    if (key != m_key) {
        flush(m_current);
        save();
        m_key = key;
        m_today = {};
        m_warned.clear();
        m_closedAt.clear();
        m_goalSaid = false;
    }
    flush(m_current);
    remind();
    enforce();
    applyBedtime();
    save();
    changed.emit();
}

void Wellbeing::unlocked() {
    ++m_today.unlocks;
    m_since = std::time(nullptr); // the locked stretch doesn't count
    save();
    changed.emit();
}

// ── Timers and Focus mode ──

int Wellbeing::limit(const std::string& cls) const {
    return Config::get().value<int>("/wellbeing/limits/" + cls, 0);
}

void Wellbeing::setLimit(const std::string& cls, int minutes) {
    auto limits = Config::get().value<nlohmann::json>("/wellbeing/limits", nlohmann::json::object());
    if (!limits.is_object()) limits = nlohmann::json::object();
    if (minutes > 0) limits[cls] = minutes;
    else limits.erase(cls);
    m_warned.erase(cls);
    Config::get().set("/wellbeing/limits", limits);
    changed.emit();
}

bool Wellbeing::timeUp(const std::string& cls) const {
    const int l = limit(cls);
    const auto it = m_today.seconds.find(cls);
    return l > 0 && it != m_today.seconds.end() && it->second >= l * 60;
}

bool Wellbeing::focus() const {
    return Config::get().value<bool>("/wellbeing/focus", false);
}

void Wellbeing::setFocus(bool on) {
    Config::get().set("/wellbeing/focus", on);
    if (on) enforce();
    changed.emit();
}

std::vector<std::string> Wellbeing::focusApps() const {
    std::vector<std::string> out;
    const auto j = Config::get().value<nlohmann::json>("/wellbeing/focusApps", nlohmann::json::array());
    if (j.is_array())
        for (const auto& x : j)
            if (x.is_string()) out.push_back(x);
    return out;
}

void Wellbeing::setFocusApp(const std::string& cls, bool distracting) {
    auto list = focusApps();
    std::erase(list, cls);
    if (distracting) list.push_back(cls);
    Config::get().set("/wellbeing/focusApps", list);
    if (distracting && focus()) enforce();
    changed.emit();
}

void Wellbeing::enforce() {
    // A minute's warning before a timer runs out, once a day
    for (const auto& [cls, secs] : m_today.seconds) {
        const int l = limit(cls);
        if (l <= 0 || m_warned[cls] || secs < l * 60 - 60 || secs >= l * 60) continue;
        m_warned[cls] = true;
        notify(std::format("1 minute left in {}", nameOf(cls)), "Your timer for today is almost up");
    }
    const bool focusing = focus();
    const auto distracting = focusApps();
    auto closes = [&](const std::string& cls) {
        return timeUp(cls) || (focusing && std::ranges::find(distracting, cls) != distracting.end());
    };
    // Only ask the compositor for its windows when something may be closed
    bool any = false;
    for (const auto& [cls, _] : m_today.seconds) any = any || timeUp(cls);
    if (!any && !(focusing && !distracting.empty())) return;
    auto& comp = Compositor::get();
    for (const auto& c : comp.windows(true)) {
        if (!closes(c.appId)) continue;
        comp.closeWindow(c.id);
        // Said once per app per day (focus: once per opening, at most a minute apart)
        const std::time_t now = std::time(nullptr);
        if (now - m_closedAt[c.appId] < 60) continue;
        m_closedAt[c.appId] = now;
        if (timeUp(c.appId)) notify(std::format("{} is paused", nameOf(c.appId)), "Its timer for today is up. It's back tomorrow.");
        else notify(std::format("{} is paused", nameOf(c.appId)), "Focus mode is on");
    }
}

// ── Reminders ──

int Wellbeing::breakEvery() const {
    return Config::get().value<int>("/wellbeing/breakEvery", 0);
}

int Wellbeing::goal() const {
    return Config::get().value<int>("/wellbeing/goal", 0);
}

void Wellbeing::remind() {
    const int every = breakEvery();
    if (every > 0 && m_session >= every * 60) {
        m_session = 0; // the next one, another stretch on
        notify("Time for a break", std::format("You've been at the screen for {} min: look away, stretch, have some water", every));
    }
    const int g = goal();
    if (g > 0 && !m_goalSaid && m_today.total() >= g * 60) {
        m_goalSaid = true;
        notify("You've reached today's screen time goal",
               g % 60 ? std::format("{} h {} min", g / 60, g % 60) : std::format("{} h", g / 60));
    }
}

// ── Bedtime ──

bool Wellbeing::scheduledBedtime() const {
    const auto& c = Config::get();
    if (!c.value<bool>("/wellbeing/bedtime/enabled", false)) return false;
    const int start = minutesOf(c.value<std::string>("/wellbeing/bedtime/start", "22:00"), 22 * 60);
    const int end = minutesOf(c.value<std::string>("/wellbeing/bedtime/end", "07:00"), 7 * 60);
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    const int now = tm.tm_hour * 60 + tm.tm_min;
    return start <= end ? (now >= start && now < end) : (now >= start || now < end);
}

void Wellbeing::setBedtimeNow(bool on) {
    m_bedtimeByHand = true;
    m_handOn = on;
    applyBedtime();
    changed.emit();
}

void Wellbeing::applyBedtime() {
    static bool lastScheduled = false;
    const bool scheduled = scheduledBedtime();
    // The schedule changing (it starts or ends) takes over from the hand
    if (scheduled != lastScheduled) m_bedtimeByHand = false;
    lastScheduled = scheduled;
    const bool on = m_bedtimeByHand ? m_handOn : scheduled;
    const auto& c = Config::get();
    const bool grey = c.value<bool>("/wellbeing/bedtime/grayscale", true), dnd = c.value<bool>("/wellbeing/bedtime/dnd", true);
    Effects::get().setTemporaryFilter(on && grey ? "grayscale" : "");
    auto& n = Notifications::get();
    if (on && dnd && !n.doNotDisturb()) {
        n.setDoNotDisturb(true);
        m_ourDnd = true;
    } else if (!on && m_ourDnd) {
        n.setDoNotDisturb(false);
        m_ourDnd = false;
    }
    if (on != m_bedtime) {
        m_bedtime = on;
        changed.emit();
    }
}

// ── Names ──

std::string Wellbeing::nameOf(const std::string& cls) {
    const std::string l = lower(cls);
    for (const auto& a : Apps::get().all()) {
        const std::string id = lower(a.id.ends_with(".desktop") ? a.id.substr(0, a.id.size() - 8) : a.id);
        if (lower(a.wmClass) == l || id == l || id.ends_with("." + l) || lower(a.name) == l) return a.name;
    }
    std::string name = cls.empty() ? "Desktop" : cls;
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

std::string Wellbeing::iconOf(const std::string& cls) {
    const std::string l = lower(cls);
    for (const auto& a : Apps::get().all()) {
        const std::string id = lower(a.id.ends_with(".desktop") ? a.id.substr(0, a.id.size() - 8) : a.id);
        if (lower(a.wmClass) == l || id == l || id.ends_with("." + l) || lower(a.name) == l) return a.icon;
    }
    return l;
}

// ── Storage: a file a day ──

Wellbeing::Day Wellbeing::day(int daysAgo) const {
    if (daysAgo == 0) return m_today;
    Day d;
    const std::string file = dir() + "/" + dateKey(std::time(nullptr) - static_cast<std::time_t>(daysAgo) * 86400) + ".json";
    std::ifstream in(file);
    if (!in) return d;
    try {
        d = parseDay(nlohmann::json::parse(in));
    } catch (const std::exception&) {
    }
    return d;
}

void Wellbeing::load() {
    m_key = dateKey(std::time(nullptr));
    m_today = {};
    // Today's so far (a restart carries on), and anything past a month goes
    std::error_code ec;
    fs::create_directories(dir(), ec);
    const std::string oldest = dateKey(std::time(nullptr) - static_cast<std::time_t>(kKeepDays) * 86400);
    for (const auto& e : fs::directory_iterator(dir(), ec))
        if (e.path().extension() == ".json" && e.path().stem().string() < oldest) fs::remove(e.path(), ec);
    std::ifstream in(dir() + "/" + m_key + ".json");
    if (!in) return;
    try {
        m_today = parseDay(nlohmann::json::parse(in));
    } catch (const std::exception& e) {
        log::warn("wellbeing: {}", e.what());
    }
}

void Wellbeing::save() {
    EventLoop::get().debounce(m_saveTimer, 2000, [this] { write(); });
}

Wellbeing::~Wellbeing() {
    if (m_saveTimer) write();
}

void Wellbeing::write() {
    if (m_key.empty()) return; // never started
    const nlohmann::json j = {{"seconds", m_today.seconds}, {"notifications", m_today.notifications}, {"unlocks", m_today.unlocks}};
    std::error_code ec;
    fs::create_directories(dir(), ec);
    const std::string file = dir() + "/" + m_key + ".json", tmp = file + ".tmp";
    {
        std::ofstream out(tmp);
        out << j.dump();
    }
    fs::rename(tmp, file, ec);
}

} // namespace m3
