#include "services/PacmanLog.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace m3::pacmanlog {

bool parseUpgrade(const std::string& line, Upgrade& out) {
    const auto at = line.find("] [ALPM] upgraded ");
    if (line.size() < 26 || line[0] != '[' || at == std::string::npos) return false;
    std::tm tm{};
    if (!strptime(line.c_str() + 1, "%Y-%m-%dT%H:%M:%S%z", &tm)) return false;
    // The offset first: timegm() normalises tm and zeroes tm_gmtoff, and
    // "timegm(&tm) - tm.tm_gmtoff" may read it after (unsequenced)
    const long offset = tm.tm_gmtoff;
    out.when = timegm(&tm) - offset;
    const std::string rest = line.substr(at + 18);
    out.package = rest.substr(0, rest.find(' '));
    return !out.package.empty();
}

std::time_t processStart(const std::string& comm) {
    std::time_t boot = 0;
    {
        std::ifstream st("/proc/stat");
        for (std::string l; std::getline(st, l);)
            if (l.starts_with("btime ")) boot = std::atoll(l.c_str() + 6);
    }
    const long ticks = sysconf(_SC_CLK_TCK);
    if (!boot || ticks <= 0) return 0;
    std::time_t newest = 0;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/proc", ec)) {
        const std::string pid = e.path().filename().string();
        if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
        std::ifstream in(e.path() / "stat");
        std::string stat;
        std::getline(in, stat);
        // pid (comm) state … field 22: start time in clock ticks after boot
        const auto open = stat.find('('), close = stat.rfind(')');
        if (open == std::string::npos || close == std::string::npos || stat.substr(open + 1, close - open - 1) != comm) continue;
        std::istringstream fields(stat.substr(close + 2));
        std::string f;
        for (int i = 3; i <= 22 && fields >> f; ++i)
            if (i == 22) newest = std::max(newest, boot + static_cast<std::time_t>(std::atoll(f.c_str()) / ticks));
    }
    return newest;
}

} // namespace m3::pacmanlog
