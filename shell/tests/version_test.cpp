// Release order, pre-releases included, as the update check and What's new
// compare them
#include "core/Version.h"

#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    expect(newerVersion("5.8.4", "5.8.3") && !newerVersion("5.8.3", "5.8.4"), "patch");
    expect(newerVersion("5.10.0", "5.9.9"), "numbers, not text");
    expect(newerVersion("6.0.1", "6.0") && !newerVersion("6.0", "6.0.0") && !newerVersion("6.0.0", "6.0"), "missing parts are 0");
    expect(newerVersion("6.0-beta-1", "5.8.4"), "a 6.0 beta after 5.8");
    expect(newerVersion("6.0-beta-2", "6.0-beta-1") && !newerVersion("6.0-beta-1", "6.0-beta-2"), "beta after beta");
    expect(newerVersion("6.0-rc-1", "6.0-beta-9"), "rc after beta");
    expect(newerVersion("6.0", "6.0-beta-3") && !newerVersion("6.0-beta-3", "6.0"), "the release after its betas");
    expect(newerVersion("6.0.1", "6.0-rc-2"), "a patch after the release");
    expect(!newerVersion("6.0-beta-1", "6.0-beta-1"), "the same isn't newer");
    expect(!newerVersion("6.0beta1", "6.0-beta-1") && newerVersion("6.0beta2", "6.0-beta-1"), "pacman's spelling reads the same");
    expect(newerVersion("v6.0", "5.8.4"), "a tag's v");
    expect(newerVersion("6.0-beta-3.1", "6.0-beta-3") && !newerVersion("6.0-beta-3", "6.0-beta-3.1"), "a point release of a beta");
    expect(newerVersion("6.0-beta-4", "6.0-beta-3.1") && newerVersion("6.0-beta-3.2", "6.0-beta-3.1"), "and after it");
    expect(!newerVersion("6.0-beta-3.0", "6.0-beta-3") && !newerVersion("6.0-beta-3", "6.0-beta-3.0"), "beta 3.0 is beta 3");
    expect(newerVersion("6.0beta3.1", "6.0-beta-3") && !newerVersion("6.0beta3.r2.gabc123", "6.0-beta-3"),
           "pacman's spelling, and a build between releases stops at .r");

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
