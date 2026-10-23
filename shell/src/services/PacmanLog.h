#pragma once

#include <ctime>
#include <string>

namespace m3::pacmanlog {

struct Upgrade {
    std::time_t when = 0; // UTC
    std::string package;
};

// "[2026-10-20T08:28:20+0530] [ALPM] upgraded linux (6.17.1-1 -> 6.17.2-1)":
// when, and which package; false for any other line
bool parseUpgrade(const std::string& line, Upgrade& out);

// When a process (by its comm) last started, from /proc/<pid>/stat and the
// boot time; the newest if several; 0 if none
std::time_t processStart(const std::string& comm);

} // namespace m3::pacmanlog
