#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace m3::rain {

// When rain starts, from Open-Meteo's 15-minute precipitation: `times` as
// it writes them ("2026-11-03T19:15", the place's local time, `offset`
// seconds from UTC) and the millimetres in each step. Minutes from `now`
// to the first step of at least `threshold` mm within `window` minutes
// (0: it's raining now), or -1 when none
int minutesUntil(const std::vector<std::string>& times, const std::vector<double>& mm, long offset, std::time_t now, int window = 60,
                 double threshold = 0.1);

} // namespace m3::rain
