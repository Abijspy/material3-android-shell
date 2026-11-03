// Spotlight's unit conversions: the phrasings, the arithmetic, temperature's
// offsets, and what isn't a conversion
#include "services/Units.h"

#include <cmath>
#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(const std::string& q, const std::string& text) {
    auto c = units::convert(q);
    const std::string got = c ? c->text : "(none)";
    if (got != text) {
        std::fprintf(stderr, "FAIL: \"%s\" → %s, wanted %s\n", q.c_str(), got.c_str(), text.c_str());
        ++failures;
    }
}

int main() {
    expect("5 km to mi", "3.107 mi");
    expect("5km in miles", "3.107 mi");
    expect("5 km", "3.107 mi");                 // to its counterpart
    expect("100 f in c", "37.78°C");
    expect("0 c to f", "32°F");
    expect("300 K to C", "26.85°C");
    expect("3.5kg lb", "7.716 lb");
    expect("10 gb in gib", "9.313 GiB");
    expect("60 mph to km/h", "96.56 km/h");
    expect("2 cups to ml", "473.2 ml");
    expect("1 acre to m2", "4047 m²");
    expect("90 min to h", "1.5 h");
    expect("6 ft 2", "(none)");                 // not a unit after the number
    expect("5 km to kg", "(none)");             // different kinds
    expect("hello", "(none)");
    expect("12", "(none)");
    expect("2+2", "(none)");
    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
