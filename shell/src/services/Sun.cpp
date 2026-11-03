#include "services/Sun.h"

#include <cmath>
#include <numbers>

namespace m3::sun {

Times on(int year, int month, int day, double lat, double lon) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    const std::time_t noon = timegm(&tm);
    std::tm norm{};
    gmtime_r(&noon, &norm);
    constexpr double pi = std::numbers::pi, rad = pi / 180;
    // The year's fraction at noon, then the equation of time (minutes) and
    // the sun's declination
    const double g = 2 * pi / 365 * norm.tm_yday;
    const double eqTime = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) - 0.014615 * std::cos(2 * g) -
                                    0.040849 * std::sin(2 * g));
    const double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) - 0.006758 * std::cos(2 * g) +
                        0.000907 * std::sin(2 * g) - 0.002697 * std::cos(3 * g) + 0.00148 * std::sin(3 * g);
    // The hour angle where the sun's top meets the horizon (refraction
    // included: 90.833°)
    const double cosHa = std::cos(90.833 * rad) / (std::cos(lat * rad) * std::cos(decl)) - std::tan(lat * rad) * std::tan(decl);
    Times t;
    if (cosHa > 1) {
        t.kind = Times::Kind::AlwaysDown;
        return t;
    }
    if (cosHa < -1) {
        t.kind = Times::Kind::AlwaysUp;
        return t;
    }
    const double ha = std::acos(cosHa) / rad;
    const std::time_t midnight = noon - 12 * 3600;
    t.rise = midnight + static_cast<std::time_t>(std::lround((720 - 4 * (lon + ha) - eqTime) * 60));
    t.set = midnight + static_cast<std::time_t>(std::lround((720 - 4 * (lon - ha) - eqTime) * 60));
    return t;
}

} // namespace m3::sun
