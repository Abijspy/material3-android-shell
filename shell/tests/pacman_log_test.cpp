// The restart banner's clock: pacman log times, any time zone, against
// process start times. "Log out to finish updating" stayed after logging
// out when an offset was lost.
#include "services/PacmanLog.h"

#include <cstdio>
#include <unistd.h>

using namespace m3::pacmanlog;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    Upgrade u;
    // 20:09:51 at +05:30 is 14:39:51 UTC
    expect(parseUpgrade("[2026-10-23T20:09:51+0530] [ALPM] upgraded hyprutils (0.14.2-1 -> 0.14.3-1)", u), "parses an upgrade");
    expect(u.package == "hyprutils", "package name");
    expect(u.when == 1792766391, "+0530 converted to UTC");
    expect(parseUpgrade("[2026-10-23T09:09:51-0500] [ALPM] upgraded linux (6.17.1-1 -> 6.17.2-1)", u) && u.when == 1792764591,
           "-0500 converted to UTC");
    expect(parseUpgrade("[2026-10-23T14:39:51+0000] [ALPM] upgraded mesa (1 -> 2)", u) && u.when == 1792766391, "UTC");
    expect(!parseUpgrade("[2026-10-23T20:09:51+0530] [ALPM] installed hyprutils (0.14.3-1)", u), "installs are not upgrades");
    expect(!parseUpgrade("[2026-10-23T20:09:51+0530] [PACMAN] Running 'pacman -Syu'", u), "other lines");
    expect(!parseUpgrade("", u), "empty line");

    // This test's own start: now, give or take a few seconds
    char comm[64];
    std::snprintf(comm, sizeof comm, "pacman_log_test");
    const std::time_t started = processStart(comm), now = std::time(nullptr);
    expect(started > 0 && started <= now + 1 && now - started < 60, "process start from /proc/<pid>/stat");
    expect(processStart("no-such-process-here") == 0, "absent process");

    if (failures == 0) std::puts("all passed");
    return failures == 0 ? 0 : 1;
}
