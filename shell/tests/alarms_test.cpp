// When alarms come round, how their days read, and the timer lengths people
// type
#include "services/Alarms.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static std::time_t utc(int y, int mo, int d, int h, int mi) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    return timegm(&tm);
}

static Alarms::Alarm alarm(int h, int m, std::vector<int> days = {}, bool enabled = true) {
    Alarms::Alarm a;
    a.hour = h;
    a.minute = m;
    a.days = std::move(days);
    a.enabled = enabled;
    return a;
}

int main() {
    setenv("TZ", "UTC", 1);
    tzset();

    // Monday 26 October 2026, 20:50
    const std::time_t now = utc(2026, 10, 26, 20, 50);
    expect(Alarms::next(alarm(21, 0), now) == utc(2026, 10, 26, 21, 0), "once, later today");
    expect(Alarms::next(alarm(20, 50), now) == utc(2026, 10, 27, 20, 50), "once, this very minute: tomorrow");
    expect(Alarms::next(alarm(7, 0), now) == utc(2026, 10, 27, 7, 0), "once, earlier in the day: tomorrow");
    expect(Alarms::next(alarm(7, 0, {1, 2, 3, 4, 5}), now) == utc(2026, 10, 27, 7, 0), "weekdays: Tuesday");
    expect(Alarms::next(alarm(9, 30, {0, 6}), now) == utc(2026, 10, 31, 9, 30), "weekends: Saturday");
    expect(Alarms::next(alarm(20, 0, {1}), now) == utc(2026, 11, 2, 20, 0), "Mondays, gone today: a week on");
    expect(Alarms::next(alarm(7, 0, {}, false), now) == 0, "off: never");

    expect(Alarms::daysText(alarm(7, 0, {1, 2, 3, 4, 5})) == "Weekdays", "weekdays");
    expect(Alarms::daysText(alarm(7, 0, {0, 6})) == "Weekends", "weekends");
    expect(Alarms::daysText(alarm(7, 0, {0, 1, 2, 3, 4, 5, 6})) == "Every day", "every day");
    expect(Alarms::daysText(alarm(7, 0, {1, 3, 5})) == "Mon, Wed, Fri", "some days");
    expect(Alarms::daysText(alarm(7, 0, {0, 1})) == "Mon, Sun", "the week from Monday");

    const std::time_t t = std::time(nullptr);
    expect(Alarms::untilText(t + 10) == "Now", "now");
    expect(Alarms::untilText(t + 3 * 3600 + 12 * 60) == "In 3 h 12 min", "hours and minutes");
    expect(Alarms::untilText(t + 5 * 60) == "In 5 min", "minutes");
    expect(Alarms::untilText(t + 49 * 3600) == "In 2 d 1 h", "days");

    // Lengths: minutes alone, m:ss, h:mm:ss, units
    expect(Alarms::parseLength("25") == 25 * 60000, "minutes alone");
    expect(Alarms::parseLength("1:30") == 90000, "m:ss");
    expect(Alarms::parseLength("1:02:03") == 3723000, "h:mm:ss");
    expect(Alarms::parseLength("1h20m") == 80 * 60000, "h and m");
    expect(Alarms::parseLength("1h 30") == 90 * 60000, "h, then minutes");
    expect(Alarms::parseLength("90s") == 90000, "seconds");
    expect(Alarms::parseLength("25 min") == 25 * 60000, "min");
    expect(Alarms::parseLength("1.5h") == 90 * 60000, "a fraction");
    expect(Alarms::parseLength("") == 0 && Alarms::parseLength("abc") == 0 && Alarms::parseLength("0") == 0, "not lengths");
    expect(Alarms::parseLength("101h") == 0 && Alarms::parseLength("1:2:3:4") == 0, "too long, too many parts");

    expect(Alarms::countdown(0) == "0:00", "done");
    expect(Alarms::countdown(400) == "0:01", "a part second still counts");
    expect(Alarms::countdown(61000) == "1:01", "minutes");
    expect(Alarms::countdown(3723000) == "1:02:03", "hours");

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
