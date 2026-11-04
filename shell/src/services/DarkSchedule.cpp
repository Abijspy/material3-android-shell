#include "services/DarkSchedule.h"

#include "core/Config.h"
#include "core/Log.h"
#include "services/NightLight.h"
#include "services/Sun.h"
#include "services/TimeZones.h"
#include "services/Wallpaper.h"

#include <cstdio>
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

bool scheduleLocation(double& lat, double& lon, std::string& place) {
    const auto loc = Config::get().value<nlohmann::json>("/weather/location", nlohmann::json());
    if (loc.is_object() && loc.contains("lat") && loc.contains("lon") && loc["lat"].is_number() && loc["lon"].is_number()) {
        lat = loc["lat"].get<double>();
        lon = loc["lon"].get<double>();
        place = loc.value("name", "");
        if (const auto comma = place.find(','); comma != std::string::npos) place.resize(comma);
        return true;
    }
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

SunSchedule& darkSchedule() {
    static SunSchedule s({"/background/dark", "19:00", "07:00", "Dark",
                          [] { return Config::get().value<bool>("/background/darkmode", true); },
                          [](bool on) {
                              Config::get().set("/background/darkmode", on);
                              Wallpaper::get().recolour();
                          },
                          "dark theme"});
    return s;
}

SunSchedule& nightSchedule() {
    static SunSchedule s({"/nightLight/schedule", "21:00", "07:00", "On", [] { return NightLight::get().on(); },
                          [](bool on) { NightLight::get().set(on); }, "night light"});
    return s;
}

void SunSchedule::start() {
    if (m_timer) return;
    // Once a minute (a suspend or a clock change is caught up with), and
    // at once when the schedule itself changes
    m_timer = EventLoop::get().addTimer(60 * 1000, [this] { check(false); }, true);
    Config::get().changed.connectForever([this] {
        const auto& c = Config::get();
        const std::string shape = c.value<std::string>(m_spec.key + "Auto", "off") + "|" + c.value<std::string>(m_spec.key + "From", m_spec.from) +
                                  "|" + c.value<std::string>(m_spec.key + "To", m_spec.to) + "|" +
                                  c.value<nlohmann::json>("/weather/location", nlohmann::json()).dump();
        if (shape == m_shape) return;
        m_shape = shape;
        changed.emit();
        check(true);
    });
    check(true);
}

std::string SunSchedule::mode() const {
    const auto m = Config::get().value<std::string>(m_spec.key + "Auto", "off");
    return m == "sun" || m == "custom" ? m : "off";
}

bool SunSchedule::sunMinutes(std::time_t now, int& from, int& to, bool& on) const {
    double lat = 0, lon = 0;
    std::string place;
    on = false;
    if (!scheduleLocation(lat, lon, place)) {
        // Nowhere known: an evening and a morning
        from = 19 * 60;
        to = 7 * 60;
        return true;
    }
    std::tm tm{};
    localtime_r(&now, &tm);
    const auto t = sun::on(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, lat, lon);
    if (t.kind != sun::Times::Kind::Normal) {
        on = t.kind == sun::Times::Kind::AlwaysDown;
        return false;
    }
    from = localMinutes(t.set);
    to = localMinutes(t.rise);
    return true;
}

bool SunSchedule::wants(std::time_t now) const {
    const std::string m = mode();
    if (m == "custom") {
        const auto& c = Config::get();
        return within(localMinutes(now), minutesOf(c.value<std::string>(m_spec.key + "From", m_spec.from), 19 * 60),
                      minutesOf(c.value<std::string>(m_spec.key + "To", m_spec.to), 7 * 60));
    }
    if (m != "sun") return false;
    int from = 0, to = 0;
    bool on = false;
    if (!sunMinutes(now, from, to, on)) return on;
    return within(localMinutes(now), from, to);
}

std::string SunSchedule::summary() const {
    const std::string m = mode();
    if (m == "custom") {
        const auto& c = Config::get();
        return std::format("{} from {} to {}", m_spec.what, clock(minutesOf(c.value<std::string>(m_spec.key + "From", m_spec.from), 19 * 60)),
                           clock(minutesOf(c.value<std::string>(m_spec.key + "To", m_spec.to), 7 * 60)));
    }
    if (m != "sun") return {};
    double lat = 0, lon = 0;
    std::string place;
    const bool known = scheduleLocation(lat, lon, place);
    int from = 0, to = 0;
    bool on = false;
    if (!sunMinutes(std::time(nullptr), from, to, on))
        return on ? std::format("{} all day: the sun doesn't rise today", m_spec.what) : std::string("Off all day: the sun doesn't set today");
    return std::format("{} from sunset at {} to sunrise at {}{}", m_spec.what, clock(from), clock(to), known && !place.empty() ? " (" + place + ")" : "");
}

void SunSchedule::check(bool force) {
    if (mode() == "off") {
        m_last = -1;
        return;
    }
    // Only when the schedule's answer changes (or it was just set up): a
    // turn by hand holds until the next switch
    const int want = wants(std::time(nullptr)) ? 1 : 0;
    if (!force && want == m_last) return;
    m_last = want;
    if (m_spec.current() == (want == 1)) return;
    log::info("{}: {} by the schedule", m_spec.name, want ? "on" : "off");
    m_spec.apply(want == 1);
}

} // namespace m3
