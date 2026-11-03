#include "services/RainParse.h"

#include <cstdio>

namespace m3::rain {

int minutesUntil(const std::vector<std::string>& times, const std::vector<double>& mm, long offset, std::time_t now, int window,
                 double threshold) {
    for (size_t i = 0; i < times.size() && i < mm.size(); ++i) {
        std::tm tm{};
        if (std::sscanf(times[i].c_str(), "%d-%d-%dT%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min) != 5) continue;
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        // The step covers the 15 minutes up to its time
        const std::time_t end = timegm(&tm) - offset, start = end - 15 * 60;
        if (end <= now) continue;
        const long minutes = start <= now ? 0 : static_cast<long>((start - now + 59) / 60);
        if (minutes > window) break;
        if (mm[i] >= threshold) return static_cast<int>(minutes);
    }
    return -1;
}

} // namespace m3::rain
