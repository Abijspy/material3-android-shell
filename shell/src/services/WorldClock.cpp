#include "services/WorldClock.h"

#include "core/Config.h"
#include "core/Log.h"
#include "services/TimeZones.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <nlohmann/json.hpp>

namespace m3 {

WorldClock& WorldClock::get() {
    static WorldClock instance;
    return instance;
}

void WorldClock::start() {
    if (m_timer) return;
    // On the minute: the next one, then every 60 s
    const std::time_t now = std::time(nullptr);
    const int toNext = static_cast<int>(60 - now % 60) * 1000 + 50;
    EventLoop::get().addTimer(toNext, [this] {
        changed.emit();
        m_timer = EventLoop::get().addTimer(60 * 1000, [this] { changed.emit(); }, true);
    });
    m_timer = 1; // started
}

std::vector<std::string> WorldClock::zones() const {
    std::vector<std::string> out;
    for (const auto& z : Config::get().value<nlohmann::json>("/clock/zones", nlohmann::json::array()))
        if (z.is_string()) out.push_back(z.get<std::string>());
    return out;
}

void WorldClock::add(const std::string& id) {
    auto z = zones();
    if (std::ranges::find(z, id) != z.end()) return;
    z.push_back(id);
    Config::get().set("/clock/zones", z);
    changed.emit();
}

void WorldClock::remove(const std::string& id) {
    auto z = zones();
    if (std::erase(z, id) == 0) return;
    Config::get().set("/clock/zones", z);
    changed.emit();
}

std::vector<WorldClock::Place> WorldClock::places() const {
    using namespace std::chrono;
    std::vector<Place> out;
    const auto now = system_clock::now();
    // 12-hour as the bar's clock is set
    const bool twelve = Config::get().value<std::string>("/bar/clock/bar/timeFormat", "hh:mm").find("AP") != std::string::npos;
    // Here: the local offset and date
    std::time_t t = system_clock::to_time_t(now);
    std::tm here{};
    localtime_r(&t, &here);
    const long hereOffset = here.tm_gmtoff;
    const auto& all = tz::system();
    for (const auto& id : zones()) {
        const time_zone* zone = nullptr;
        try {
            zone = locate_zone(id);
        } catch (const std::exception&) {
            continue; // not a zone (any more)
        }
        const auto info = zone->get_info(now);
        const long offset = static_cast<long>(info.offset.count());
        const auto local = zone->to_local(now);
        const auto days = floor<std::chrono::days>(local);
        const hh_mm_ss hms{floor<minutes>(local - days)};
        const int h = static_cast<int>(hms.hours().count()), m = static_cast<int>(hms.minutes().count());
        Place p;
        p.id = id;
        p.city = tz::cityName(id);
        if (const int i = tz::find(all, id); i >= 0) p.country = all[static_cast<size_t>(i)].country;
        p.time = twelve ? std::format("{}:{:02} {}", h % 12 == 0 ? 12 : h % 12, m, h < 12 ? "AM" : "PM") : std::format("{:02}:{:02}", h, m);
        // Its date against here's
        const year_month_day theirs{days};
        const year_month_day ours{std::chrono::year(here.tm_year + 1900), std::chrono::month(static_cast<unsigned>(here.tm_mon + 1)),
                                  std::chrono::day(static_cast<unsigned>(here.tm_mday))};
        const auto diff = (sys_days{theirs} - sys_days{ours}).count();
        p.day = diff > 0 ? "Tomorrow" : diff < 0 ? "Yesterday" : "Today";
        const long d = offset - hereOffset;
        if (d == 0) p.offset = "Same time";
        else {
            const long a = std::labs(d) / 60;
            p.offset = std::format("{}{}{} h", d > 0 ? "+" : "−", a / 60, a % 60 ? std::format(":{:02}", a % 60) : "");
        }
        out.push_back(std::move(p));
    }
    return out;
}

} // namespace m3
