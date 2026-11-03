#include "services/DarkSchedule.h"

#include "core/Config.h"
#include "core/Log.h"
#include "services/Sun.h"
#include "services/TimeZones.h"
#include "services/Wallpaper.h"

#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>

namespace m3 {

namespace {

int minutesOf(const std::string& hhmm, int fallback) {
    int h = 0, m = 0;
    if (std::sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return fallback;
    return h * 60 + m;
}

std::string clock(int minutes) {
    minutes = ((minutes % 1440) + 1440) % 1440;
    return std::format("{:02}:{:02}", minutes / 60, minutes % 60);
}

int localMinutes(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

// Within [from, to), across midnight when it ends before it starts
bool within(int now, int from, int to) {
    return from <= to ? now >= from && now < to : now >= from || now < to;
}

} // namespace

DarkSchedule& DarkSchedule::get() {
    static DarkSchedule instance;
    return instance;
}

void DarkSchedule::start() {
    if (m_timer) return;
    // Once a minute (a suspend or a clock change is caught up with), and
    // at once when the schedule itself changes
    m_timer = EventLoop::get().addTimer(60 * 1000, [this] { check(false); }, true);
    Config::get().changed.connectForever([this] {
        const auto& c = Config::get();
        const std::string shape = c.value<std::string>("/background/darkAuto", "off") + "|" + c.value<std::string>("/background/darkFrom", "19:00") +
                                  "|" + c.value<std::string>("/background/darkTo", "07:00") + "|" +
                                  c.value<nlohmann::json>("/weather/location", nlohmann::json()).dump();
        if (shape == m_shape) return;
        m_shape = shape;
        changed.emit();
        check(true);
    });
    check(true);
}

std::string DarkSchedule::mode() const {
    const auto m = Config::get().value<std::string>("/background/darkAuto", "off");
    return m == "sun" || m == "custom" ? m : "off";
}

bool DarkSchedule::where(double& lat, double& lon, std::string& place) const {
    // The weather's place, picked or found
    const auto loc = Config::get().value<nlohmann::json>("/weather/location", nlohmann::json());
    if (loc.is_object() && loc.contains("lat") && loc.contains("lon") && loc["lat"].is_number() && loc["lon"].is_number()) {
        lat = loc["lat"].get<double>();
        lon = loc["lon"].get<double>();
        place = loc.value("name", "");
        if (const auto comma = place.find(','); comma != std::string::npos) place.resize(comma);
        return true;
    }
    // Else the time zone's city
    std::error_code ec;
    std::string id = std::filesystem::read_symlink("/etc/localtime", ec).string();
    if (const auto at = id.find("zoneinfo/"); at != std::string::npos) id = id.substr(at + 9);
    const auto& zones = tz::system();
    const int i = tz::find(zones, id);
    if (i < 0) return false;
    lat = zones[static_cast<size_t>(i)].lat;
    lon = zones[static_cast<size_t>(i)].lon;
    place = zones[static_cast<size_t>(i)].city;
    return true;
}

bool DarkSchedule::sunMinutes(std::time_t now, int& darkFrom, int& darkTo, bool& dark) const {
    double lat = 0, lon = 0;
    std::string place;
    dark = false;
    if (!where(lat, lon, place)) {
        // Nowhere known: an evening and a morning
        darkFrom = 19 * 60;
        darkTo = 7 * 60;
        return true;
    }
    std::tm tm{};
    localtime_r(&now, &tm);
    const auto t = sun::on(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, lat, lon);
    if (t.kind != sun::Times::Kind::Normal) {
        dark = t.kind == sun::Times::Kind::AlwaysDown;
        return false;
    }
    darkFrom = localMinutes(t.set);
    darkTo = localMinutes(t.rise);
    return true;
}

bool DarkSchedule::wantsDark(std::time_t now) const {
    const std::string m = mode();
    if (m == "custom") {
        const auto& c = Config::get();
        return within(localMinutes(now), minutesOf(c.value<std::string>("/background/darkFrom", "19:00"), 19 * 60),
                      minutesOf(c.value<std::string>("/background/darkTo", "07:00"), 7 * 60));
    }
    if (m != "sun") return false;
    int from = 0, to = 0;
    bool dark = false;
    if (!sunMinutes(now, from, to, dark)) return dark;
    return within(localMinutes(now), from, to);
}

std::string DarkSchedule::summary() const {
    const std::string m = mode();
    if (m == "custom") {
        const auto& c = Config::get();
        return std::format("Dark from {} to {}", clock(minutesOf(c.value<std::string>("/background/darkFrom", "19:00"), 19 * 60)),
                           clock(minutesOf(c.value<std::string>("/background/darkTo", "07:00"), 7 * 60)));
    }
    if (m != "sun") return {};
    double lat = 0, lon = 0;
    std::string place;
    const bool known = where(lat, lon, place);
    int from = 0, to = 0;
    bool dark = false;
    if (!sunMinutes(std::time(nullptr), from, to, dark)) return dark ? "Dark all day: the sun doesn't rise today" : "Light all day: the sun doesn't set today";
    return std::format("Dark from sunset at {} to sunrise at {}{}", clock(from), clock(to),
                       known && !place.empty() ? " (" + place + ")" : "");
}

void DarkSchedule::check(bool force) {
    if (mode() == "off") {
        m_last = -1;
        return;
    }
    // Only when the schedule's answer changes (or it was just set up): a
    // turn by hand holds until the next switch
    const int want = wantsDark(std::time(nullptr)) ? 1 : 0;
    if (!force && want == m_last) return;
    m_last = want;
    auto& c = Config::get();
    if (c.value<bool>("/background/darkmode", true) == (want == 1)) return;
    log::info("dark theme: {} by the schedule", want ? "on" : "off");
    c.set("/background/darkmode", want == 1);
    Wallpaper::get().recolour();
}

} // namespace m3
