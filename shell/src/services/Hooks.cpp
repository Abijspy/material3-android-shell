#include "services/Hooks.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace m3 {

namespace {

// Bigger than this, the log starts afresh (the last one kept as .old)
constexpr std::uintmax_t kLogLimit = 512 * 1024;

std::vector<std::string> pausedList() {
    std::vector<std::string> out;
    const auto j = Config::get().value<nlohmann::json>("/hookSettings/paused", nlohmann::json::array());
    if (j.is_array())
        for (const auto& v : j)
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

} // namespace

Hooks& Hooks::get() {
    static Hooks instance;
    return instance;
}

const std::vector<Hooks::Event>& Hooks::events() {
    // Noctalia's names where they share an event, so hooks carry over
    static const std::vector<Event> all = {
        {"started", "power_settings_new", "Shell started", "Once the shell is up, at login", "Session", {}},
        {"session_locked", "lock", "Screen locked", "The lock screen is up", "Session", {}},
        {"session_unlocked", "lock_open", "Screen unlocked", "Back from the lock screen", "Session", {}},
        {"idle_started", "bedtime", "Away", "No input for a couple of minutes", "Session", {}},
        {"idle_ended", "waving_hand", "Back", "Input again after being away", "Session", {}},
        {"suspending", "dark_mode", "Going to sleep", "Just before the computer sleeps", "Session", {}},
        {"resumed", "wb_sunny", "Woke up", "The computer woke from sleep", "Session", {}},
        {"logging_out", "logout", "Logging out", "Just before (it waits up to 5 seconds for these)", "Session", {}},
        {"rebooting", "restart_alt", "Restarting", "Just before (it waits up to 5 seconds for these)", "Session", {}},
        {"shutting_down", "power_settings_new", "Shutting down", "Just before (it waits up to 5 seconds for these)", "Session", {}},

        {"wallpaper_changed", "wallpaper", "Wallpaper changed", "A new wallpaper is set", "Wallpaper & colours",
         {{"M3_WALLPAPER_PATH", "/usr/share/material3ui/wallpapers/bloom-lavender.jpg"}}},
        {"colors_changed", "palette", "Colours changed", "A new palette is made (and the apps' themes with it)", "Wallpaper & colours",
         {{"M3_THEME_MODE", "dark"}}},
        {"theme_mode_changed", "contrast", "Light or dark changed", "Dark theme turned on or off", "Wallpaper & colours",
         {{"M3_THEME_MODE", "light"}, {"M3_THEME_MODE_PREVIOUS", "dark"}}},

        {"battery_charging", "battery_charging_full", "Charging", "The battery starts charging", "Power",
         {{"M3_BATTERY_PERCENT", "64"}}},
        {"battery_discharging", "battery_5_bar", "On battery", "Unplugged: running on the battery", "Power",
         {{"M3_BATTERY_PERCENT", "64"}}},
        {"battery_full", "battery_full", "Fully charged", "The battery is full", "Power", {{"M3_BATTERY_PERCENT", "100"}}},
        {"battery_low", "battery_alert", "Battery low", "Below 15% on battery (once each time)", "Power",
         {{"M3_BATTERY_PERCENT", "14"}}},
        {"battery_percentage_changed", "battery_horiz_050", "Battery level changed", "Each whole percent up or down", "Power",
         {{"M3_BATTERY_PERCENT", "63"}, {"M3_BATTERY_STATE", "discharging"}}},
        {"power_profile_changed", "speed", "Power mode changed", "Power saver, balanced or performance", "Power",
         {{"M3_POWER_PROFILE", "power-saver"}, {"M3_POWER_PROFILE_PREVIOUS", "balanced"}}},

        {"network_connected", "wifi", "Connected", "Online through Wi-Fi or a cable", "Network",
         {{"M3_NETWORK_NAME", "Home Wi-Fi"}, {"M3_NETWORK_TYPE", "wifi"}}},
        {"network_disconnected", "wifi_off", "Disconnected", "No network any more", "Network", {}},
        {"wifi_enabled", "wifi", "Wi-Fi on", "Wi-Fi turned on", "Network", {}},
        {"wifi_disabled", "wifi_off", "Wi-Fi off", "Wi-Fi turned off", "Network", {}},
        {"bluetooth_enabled", "bluetooth", "Bluetooth on", "Bluetooth turned on", "Network", {}},
        {"bluetooth_disabled", "bluetooth_disabled", "Bluetooth off", "Bluetooth turned off", "Network", {}},
        {"vpn_connected", "vpn_lock", "VPN connected", "A VPN comes up", "Network", {{"M3_VPN_NAME", "Work"}}},
        {"vpn_disconnected", "vpn_key_off", "VPN disconnected", "The VPN goes down", "Network", {}},

        {"notification_received", "notifications_active", "Notification", "One arrives (shown or not)", "Notifications & music",
         {{"M3_NOTIFICATION_APP", "Messages"},
          {"M3_NOTIFICATION_SUMMARY", "Hello"},
          {"M3_NOTIFICATION_BODY", "A test from Material3UI"},
          {"M3_NOTIFICATION_URGENCY", "normal"}}},
        {"dnd_enabled", "do_not_disturb_on", "Do not disturb on", "Do not disturb turned on", "Notifications & music", {}},
        {"dnd_disabled", "do_not_disturb_off", "Do not disturb off", "Do not disturb turned off", "Notifications & music", {}},
        {"track_changed", "music_note", "Song changed", "Another track starts in a player", "Notifications & music",
         {{"M3_MEDIA_TITLE", "Blinding Lights"}, {"M3_MEDIA_ARTIST", "The Weeknd"}, {"M3_MEDIA_ALBUM", "After Hours"}, {"M3_MEDIA_PLAYER", "Spotify"}}},

        {"workspace_changed", "view_carousel", "Workspace changed", "Another workspace comes up", "Desktop",
         {{"M3_WORKSPACE", "2"}, {"M3_WORKSPACE_PREVIOUS", "1"}}},
        {"monitor_added", "desktop_windows", "Screen connected", "A monitor is plugged in", "Desktop", {{"M3_MONITOR", "HDMI-A-1"}}},
        {"monitor_removed", "desktop_access_disabled", "Screen disconnected", "A monitor is unplugged", "Desktop", {{"M3_MONITOR", "HDMI-A-1"}}},
        {"screenshot_taken", "screenshot_region", "Screenshot taken", "One is saved", "Desktop",
         {{"M3_FILE", "~/Pictures/Screenshots/Screenshot.png"}}},
        {"recording_started", "screen_record", "Recording started", "A screen recording begins", "Desktop", {}},
        {"recording_saved", "video_file", "Recording saved", "A screen recording is saved", "Desktop",
         {{"M3_FILE", "~/Videos/Recordings/Recording.mp4"}}},

        {"focus_enabled", "self_improvement", "Focus mode on", "Distracting apps pause", "Digital Wellbeing", {}},
        {"focus_disabled", "self_improvement", "Focus mode off", "Focus mode ends", "Digital Wellbeing", {}},
        {"bedtime_started", "bedtime", "Bedtime starts", "On schedule or by hand", "Digital Wellbeing", {}},
        {"bedtime_ended", "wb_twilight", "Bedtime ends", "Bedtime mode is over", "Digital Wellbeing", {}},

        {"updates_available", "system_update_alt", "Updates available", "An automatic check finds new ones (firmware's too)", "Software",
         {{"M3_UPDATES_COUNT", "12"}, {"M3_UPDATES_NEW", "3"}}},
        {"updates_installed", "deployed_code_update", "Updates installed", "Update now finishes", "Software", {{"M3_UPDATES_COUNT", "12"}}},

        {"alarm_ringing", "alarm", "Alarm ringing", "An alarm goes off (and again after a snooze)", "Clock",
         {{"M3_ALARM_TIME", "07:00"}, {"M3_ALARM_LABEL", "Wake up"}}},
        {"timer_finished", "timer", "Timer finished", "A timer runs out", "Clock", {{"M3_TIMER_LABEL", "Tea"}, {"M3_TIMER_LENGTH", "5:00"}}},
    };
    return all;
}

const Hooks::Event* Hooks::find(const std::string& id) {
    for (const auto& e : events())
        if (id == e.id) return &e;
    return nullptr;
}

std::string Hooks::logFile() {
    return paths::stateDir() + "/hooks.log";
}

std::string Hooks::eventsFile() {
    return paths::runtimeDir() + "/events.jsonl";
}

int Hooks::cli(const std::vector<std::string>& args) {
    const std::string mode = args.empty() ? "follow" : args[0];
    const bool wait = mode == "wait";
    if (!wait && mode != "follow") {
        std::fprintf(stderr, "usage: material3ui events [follow] | wait <event|*> [--since <ms>] [--timeout <s>]\n");
        return 2;
    }
    if (wait && args.size() < 2) {
        std::fprintf(stderr, "events wait needs an event name (or *)\n");
        return 2;
    }
    const std::string want = wait ? args[1] : "*";
    long long since = -1;
    double timeout = wait ? 30 : -1;
    for (size_t i = wait ? 2 : 1; i + 1 < args.size(); i += 2) {
        if (args[i] == "--since") since = std::atoll(args[i + 1].c_str());
        else if (args[i] == "--timeout") timeout = std::atof(args[i + 1].c_str());
    }
    const std::string file = eventsFile();
    std::error_code ec;
    // From the start (what's happened since <ms> counts), else what's new
    std::uintmax_t offset = since >= 0 || !fs::exists(file, ec) ? 0 : fs::file_size(file, ec);
    const auto start = std::chrono::steady_clock::now();
    std::string partial;
    while (timeout < 0 || std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < timeout) {
        const auto size = fs::exists(file, ec) ? fs::file_size(file, ec) : 0;
        if (size < offset) offset = 0; // started afresh
        if (size > offset) {
            std::ifstream in(file, std::ios::binary);
            in.seekg(static_cast<std::streamoff>(offset));
            std::string chunk(size - offset, '\0');
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            offset = size;
            partial += chunk;
            for (size_t nl; (nl = partial.find('\n')) != std::string::npos;) {
                const std::string line = partial.substr(0, nl);
                partial.erase(0, nl + 1);
                const auto j = nlohmann::json::parse(line, nullptr, false);
                if (!j.is_object()) continue;
                if (since >= 0 && j.value("ms", 0LL) < since) continue;
                if (want != "*" && j.value("event", "") != want) continue;
                std::printf("%s\n", line.c_str());
                std::fflush(stdout);
                if (wait) return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return wait ? 1 : 0;
}

void Hooks::record(const std::string& event, const Env& env, bool simulated) {
    if (!Config::get().value<bool>("/hookSettings/record", true)) return;
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
    nlohmann::json line = {{"event", event},
                           {"time", std::format("{:%FT%T}", zoned_time{current_zone(), floor<milliseconds>(now)})},
                           {"ms", ms}};
    nlohmann::json vars = nlohmann::json::object();
    for (const auto& [name, value] : env) vars[name] = value;
    line["vars"] = vars;
    if (simulated) line["simulated"] = true;
    const std::string file = eventsFile();
    std::error_code ec;
    fs::create_directories(paths::runtimeDir(), ec);
    if (fs::exists(file, ec) && fs::file_size(file, ec) > 2 * kLogLimit) fs::rename(file, file + ".old", ec);
    std::ofstream(file, std::ios::app) << line.dump() << '\n';
}

bool Hooks::enabled() const {
    return Config::get().value<bool>("/hookSettings/enabled", true);
}

std::vector<std::string> Hooks::commands(const std::string& event) const {
    std::vector<std::string> out;
    const auto j = Config::get().value<nlohmann::json>("/hooks/" + event, nlohmann::json());
    if (j.is_string()) out.push_back(j.get<std::string>());
    else if (j.is_array())
        for (const auto& c : j)
            if (c.is_string()) out.push_back(c.get<std::string>());
    std::erase_if(out, [](const std::string& c) { return c.find_first_not_of(" \t") == std::string::npos; });
    return out;
}

void Hooks::setCommands(const std::string& event, const std::vector<std::string>& commands) {
    std::vector<std::string> kept = commands;
    std::erase_if(kept, [](const std::string& c) { return c.find_first_not_of(" \t") == std::string::npos; });
    if (kept.empty()) Config::get().set("/hooks/" + event, nullptr);
    else Config::get().set("/hooks/" + event, kept);
}

bool Hooks::paused(const std::string& event) const {
    const auto list = pausedList();
    return std::ranges::find(list, event) != list.end();
}

void Hooks::setPaused(const std::string& event, bool paused) {
    auto list = pausedList();
    std::erase(list, event);
    if (paused) list.push_back(event);
    Config::get().set("/hookSettings/paused", list);
}

void Hooks::fire(const std::string& event, const Env& env) {
    record(event, env, false);
    if (!enabled() || paused(event)) return;
    auto list = commands(event);
    if (!list.empty()) runAll(event, std::move(list), env, false, nullptr);
}

void Hooks::fireThen(const std::string& event, const Env& env, std::function<void()> then, int waitMs) {
    // Whichever comes first: the hooks done, or the wait over
    auto pending = std::make_shared<std::function<void()>>(std::move(then));
    auto go = [pending] {
        if (!*pending) return;
        auto f = std::move(*pending);
        *pending = nullptr;
        f();
    };
    record(event, env, false);
    auto list = enabled() && !paused(event) ? commands(event) : std::vector<std::string>{};
    if (list.empty()) return go();
    const auto timer = EventLoop::get().addTimer(waitMs, go);
    runAll(event, std::move(list), env, false, [go, timer] {
        EventLoop::get().cancel(timer);
        go();
    });
}

void Hooks::test(const std::string& event) {
    const Event* e = find(event);
    auto list = commands(event);
    if (!e || list.empty()) return;
    Env env;
    for (const auto& v : e->vars) env.emplace_back(v.name, v.sample);
    runAll(event, std::move(list), env, true, nullptr);
}

void Hooks::simulate(const std::string& event, const Env& env) {
    record(event, env, true);
    if (!enabled() || paused(event)) return;
    auto list = commands(event);
    if (!list.empty()) runAll(event, std::move(list), env, false, nullptr);
}

const Hooks::Result* Hooks::last(const std::string& event) const {
    const auto it = m_last.find(event);
    return it == m_last.end() ? nullptr : &it->second;
}

void Hooks::runAll(const std::string& event, std::vector<std::string> commands, const Env& env, bool test, std::function<void()> done) {
    if (commands.empty()) {
        if (done) done();
        return;
    }
    const std::string command = commands.front();
    commands.erase(commands.begin());
    const std::string log = logFile();
    std::error_code ec;
    fs::create_directories(paths::stateDir(), ec);
    if (fs::exists(log, ec) && fs::file_size(log, ec) > kLogLimit) fs::rename(log, log + ".old", ec);
    // What happened, as variables: values never become part of the command
    std::vector<std::string> argv = {"env", "M3_EVENT=" + event, "M3_HOOK_LOG=" + log, "M3_HOOK_COMMAND=" + command};
    if (test) argv.push_back("M3_HOOK_TEST=1");
    for (const auto& [name, value] : env) argv.push_back(name + "=" + value);
    // Its output to the log, under a heading, and not to the shell: a program
    // it leaves running can't hold anything up
    argv.insert(argv.end(), {"sh", "-c",
                             "exec >>\"$M3_HOOK_LOG\" 2>&1; printf '\\n== %s %s%s: %s\\n' \"$(date '+%F %T')\" \"$M3_EVENT\" "
                             "\"${M3_HOOK_TEST:+ (test)}\" \"$M3_HOOK_COMMAND\"\n" +
                                 command});
    run(argv, [this, event, commands = std::move(commands), env, test, done](int code, const std::string&) mutable {
        m_last[event] = {std::time(nullptr), code, test};
        ran.emit();
        runAll(event, std::move(commands), env, test, std::move(done));
    });
}

} // namespace m3
