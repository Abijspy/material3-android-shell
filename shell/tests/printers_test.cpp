// lpstat as CUPS 2.4 writes it (LC_ALL=C): printers, their states and
// details, the default, and the queue
#include "services/Printers.h"

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
    const char* lpstatP =
        "printer HP_LaserJet is idle.  enabled since Sat 25 Oct 2026 10:00:00 AM IST\n"
        "\tForm mounted:\n"
        "\tContent types: any\n"
        "\tDescription: HP LaserJet Pro M404\n"
        "\tAlerts: none\n"
        "\tLocation: Office\n"
        "printer Brother now printing Brother-14.  enabled since Sat 25 Oct 2026 11:02:13 AM IST\n"
        "\tDescription: Brother HL-L2350DW\n"
        "\tLocation:\n"
        "printer Canon disabled since Sat 25 Oct 2026 09:41:00 AM IST -\n"
        "\tPaused by the user\n"
        "\tDescription: Canon PIXMA\n";
    const auto ps = Printers::parsePrinters(lpstatP, "system default destination: Brother\n");
    expect(ps.size() == 3, "three printers");
    if (ps.size() == 3) {
        expect(ps[0].name == "HP_LaserJet" && ps[0].state == "idle" && ps[0].enabled, "idle printer");
        expect(ps[0].description == "HP LaserJet Pro M404" && ps[0].location == "Office", "description and location");
        expect(!ps[0].isDefault && ps[1].isDefault, "the default");
        expect(ps[1].state == "printing" && ps[1].job == "Brother-14", "printing, and which job");
        expect(ps[2].state == "stopped" && !ps[2].enabled && ps[2].message == "Paused by the user", "stopped, with its reason");
    }
    expect(Printers::parsePrinters("", "no system default destination\n").empty(), "no printers");

    const auto jobs = Printers::parseJobs("Brother-14              abishek          1024   Sat 25 Oct 2026 11:02:10 AM IST\n"
                                          "HP_LaserJet-15          abishek        204800   Sat 25 Oct 2026 11:03:00 AM IST\n");
    expect(jobs.size() == 2, "two jobs");
    if (jobs.size() == 2) {
        expect(jobs[0].id == "Brother-14" && jobs[0].printer == "Brother" && jobs[0].user == "abishek" && jobs[0].size == 1024, "a job");
        expect(jobs[1].printer == "HP_LaserJet" && jobs[1].size == 204800, "a printer name with an underscore");
    }
    expect(Printers::parseJobs("").empty(), "an empty queue");

    // Titles, from lpq -a's table (a title may have spaces)
    auto titled = jobs;
    Printers::parseTitles("Rank    Owner   Job     File(s)                         Total Size\n"
                          "1st     abishek 14      Test page                       1024 bytes\n"
                          "2nd     abishek 15      hello.txt                       204800 bytes\n",
                          titled);
    expect(titled.size() == 2 && titled[0].title == "Test page" && titled[1].title == "hello.txt", "job titles");
    Printers::parseTitles("no entries\n", titled);
    expect(titled[0].title == "Test page", "no entries leaves titles be");

    // In a few words: a paused printer is why its jobs wait
    std::vector<Printers::Printer> one(1);
    one[0].name = "HP";
    one[0].state = "idle";
    std::vector<Printers::Job> two(2);
    two[0].printer = two[1].printer = "HP";
    expect(Printers::summary(false, false, {}, {}) == "Not set up", "not set up");
    expect(Printers::summary(true, false, {}, {}) == "Not running", "not running");
    expect(Printers::summary(true, true, {}, {}) == "No printers", "no printers");
    expect(Printers::summary(true, true, one, {}) == "Ready", "ready");
    expect(Printers::summary(true, true, one, two) == "2 jobs waiting", "jobs waiting");
    one[0].state = "printing";
    expect(Printers::summary(true, true, one, two) == "Printing · 2 jobs", "printing");
    one[0].state = "stopped";
    expect(Printers::summary(true, true, one, two) == "Paused · 2 jobs waiting", "paused with jobs");
    expect(Printers::summary(true, true, one, {}) == "Paused", "paused");

    // Addresses typed for printers that don't announce themselves
    expect(Printers::ippAddress("192.168.1.20") == "ipp://192.168.1.20/ipp/print", "a bare IP");
    expect(Printers::ippAddress("  printer.lan:631 ") == "ipp://printer.lan:631/ipp/print", "a host and port, trimmed");
    expect(Printers::ippAddress("ipp://10.0.0.5/ipp/print") == "ipp://10.0.0.5/ipp/print", "a whole ipp address");
    expect(Printers::ippAddress("ipps://hp.local") == "ipps://hp.local/ipp/print", "ipps without a path");
    expect(Printers::ippAddress("10.0.0.5/printers/office") == "ipp://10.0.0.5/printers/office", "a path kept");
    expect(Printers::ippAddress("socket://10.0.0.5:9100").empty(), "not IPP");
    expect(Printers::ippAddress("my printer").empty(), "not an address");
    expect(Printers::ippAddress("").empty(), "nothing typed");
    if (failures == 0) std::puts("all passed");
    return failures == 0 ? 0 : 1;
}
