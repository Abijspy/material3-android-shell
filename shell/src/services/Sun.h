#pragma once

#include <ctime>

namespace m3::sun {

// Sunrise and sunset, worked out where you are (NOAA's solar equations,
// good to a minute or two): no network
struct Times {
    enum class Kind { Normal, AlwaysUp, AlwaysDown }; // polar day, polar night
    Kind kind = Kind::Normal;
    std::time_t rise = 0, set = 0; // Normal only
};

// On the calendar day year-month-day (the local date) at (lat, lon), in
// degrees north and east
Times on(int year, int month, int day, double lat, double lon);

} // namespace m3::sun
