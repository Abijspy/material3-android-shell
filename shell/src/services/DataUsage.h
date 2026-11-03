#pragma once

#include "core/Signal.h"

#include <cstdint>
#include <string>
#include <vector>

namespace m3 {

// How much the network has carried, kept by day across restarts and
// reboots (Settings → Network → Data usage). /proc/net/dev each minute: what
// each interface moved since the last look, added to today. The counters
// are remembered with the boot they belong to, so what moved while the
// shell wasn't running still counts; after a reboot they start again.
class DataUsage {
public:
    struct Day {
        std::string date; // YYYY-MM-DD
        uint64_t rx = 0, tx = 0;
    };
    static DataUsage& get();
    void start();
    Day today() const;
    Day month() const;                       // this calendar month, summed
    std::vector<Day> lastDays(int n) const;  // oldest first, today last (empty days included)
    Signal<> changed;

private:
    DataUsage() = default;
    void sample();
    void load();
    void save() const;
    std::vector<Day> m_days;                              // oldest first
    std::vector<std::pair<std::string, std::pair<uint64_t, uint64_t>>> m_counters; // interface → rx, tx last seen
    std::string m_boot;
    bool m_started = false;
};

} // namespace m3
