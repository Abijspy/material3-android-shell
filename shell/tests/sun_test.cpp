// Sunrise and sunset against published times (timeanddate.com), within a
// few minutes; polar day and night
#include "services/Sun.h"

#include <cstdio>
#include <cstdlib>

using namespace m3;

static int failures = 0;

// Minutes past UTC midnight of that day
static void expectNear(std::time_t t, int y, int mo, int d, int hh, int mm, const char* what) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    const std::time_t want = timegm(&tm);
    if (std::llabs(static_cast<long long>(t - want)) > 4 * 60) {
        std::fprintf(stderr, "FAIL: %s: off by %lld s\n", what, static_cast<long long>(t - want));
        ++failures;
    }
}

int main() {
    // Chennai, 3 Nov 2026: 06:03 and 17:43 IST (UTC+5:30)
    auto c = sun::on(2026, 11, 3, 13.08, 80.27);
    expectNear(c.rise, 2026, 11, 3, 0, 33, "Chennai sunrise");
    expectNear(c.set, 2026, 11, 3, 12, 13, "Chennai sunset");
    // London, 21 Jun 2026: 04:43 and 21:21 BST (UTC+1)
    auto l = sun::on(2026, 6, 21, 51.51, -0.13);
    expectNear(l.rise, 2026, 6, 21, 3, 43, "London sunrise");
    expectNear(l.set, 2026, 6, 21, 20, 21, "London sunset");
    // New York, 1 Jan 2026: 07:20 and 16:39 EST (UTC−5)
    auto n = sun::on(2026, 1, 1, 40.71, -74.01);
    expectNear(n.rise, 2026, 1, 1, 12, 20, "New York sunrise");
    expectNear(n.set, 2026, 1, 1, 21, 39, "New York sunset");
    // Tromsø: polar night in December, midnight sun in June
    if (sun::on(2026, 12, 21, 69.65, 18.96).kind != sun::Times::Kind::AlwaysDown) {
        std::fprintf(stderr, "FAIL: Tromsø polar night\n");
        ++failures;
    }
    if (sun::on(2026, 6, 21, 69.65, 18.96).kind != sun::Times::Kind::AlwaysUp) {
        std::fprintf(stderr, "FAIL: Tromsø midnight sun\n");
        ++failures;
    }
    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
