// Rain alerts: when the first wet 15 minutes starts, in the place's own
// time; raining now; nothing within the hour
#include "services/RainParse.h"

#include <cstdio>

using namespace m3;

static int failures = 0;
static void expect(int got, int want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: %d, wanted %d\n", what, got, want);
        ++failures;
    }
}

int main() {
    // Chennai (UTC+5:30); now 19:00 there, 13:30 UTC
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 10;
    tm.tm_mday = 3;
    tm.tm_hour = 13;
    tm.tm_min = 30;
    const std::time_t now = timegm(&tm);
    const long ist = 19800;
    const std::vector<std::string> times = {"2026-11-03T19:15", "2026-11-03T19:30", "2026-11-03T19:45", "2026-11-03T20:00",
                                            "2026-11-03T20:15", "2026-11-03T20:30"};
    expect(rain::minutesUntil(times, {0, 0, 0.4, 1.2, 0, 0}, ist, now), 30, "rain from 19:30");
    expect(rain::minutesUntil(times, {0.3, 0, 0, 0, 0, 0}, ist, now), 0, "raining now");
    expect(rain::minutesUntil(times, {0, 0, 0, 0, 0.5, 0}, ist, now), 60, "rain at the hour's edge");
    expect(rain::minutesUntil(times, {0, 0, 0, 0, 0, 0.9}, ist, now), -1, "rain after the hour");
    expect(rain::minutesUntil(times, {0, 0.05, 0, 0, 0, 0}, ist, now), -1, "a trace isn't rain");
    // The same moment from elsewhere (its own offset): the same answer
    const std::vector<std::string> utc = {"2026-11-03T13:45", "2026-11-03T14:00", "2026-11-03T14:15"};
    expect(rain::minutesUntil(utc, {0, 0, 2.0}, 0, now), 30, "in UTC");
    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
