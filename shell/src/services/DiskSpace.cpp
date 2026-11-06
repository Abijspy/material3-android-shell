#include "services/DiskSpace.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Hooks.h"
#include "services/SystemStats.h"
#include "services/Trash.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <sys/statvfs.h>
#include <vector>

namespace m3 {

namespace {

constexpr uint64_t kGiB = 1024ull * 1024 * 1024;

// Under this is low: a twentieth of the disk, at most 20 GiB, at least 1 GiB
uint64_t lowLimit(uint64_t total) {
    return std::max(kGiB, std::min(total / 20, 20 * kGiB));
}

} // namespace

DiskSpace& DiskSpace::get() {
    static DiskSpace d;
    return d;
}

void DiskSpace::start() {
    if (m_started) return;
    m_started = true;
    // A minute in (not while everything starts), then every five
    EventLoop::get().addTimer(60 * 1000, [this] { check(); });
    EventLoop::get().addTimer(5 * 60 * 1000, [this] { check(); }, true);
}

void DiskSpace::check() {
    if (!Config::get().value<bool>("/storage/lowSpaceWarning", true)) return;
    // Your home's disk, and the system's when it's another
    const std::string home = paths::home();
    std::set<unsigned long> seen;
    struct Disk {
        std::string path, name;
        uint64_t total, free;
    };
    std::vector<Disk> disks;
    for (const std::string& path : {home, std::string("/")}) {
        struct statvfs st {};
        if (statvfs(path.c_str(), &st) != 0 || st.f_blocks == 0 || !seen.insert(st.f_fsid).second) continue;
        disks.push_back({path, "", static_cast<uint64_t>(st.f_blocks) * st.f_frsize, static_cast<uint64_t>(st.f_bavail) * st.f_frsize});
    }
    for (auto& d : disks) d.name = disks.size() == 1 ? "Your disk" : d.path == "/" ? "The system disk" : "Your home folder's disk";

    for (const auto& d : disks) {
        Seen& s = m_seen[d.path];
        const uint64_t limit = lowLimit(d.total);
        // Not low again until clearly not (no notice each time it wavers)
        if (d.free > limit + limit / 4) s = {};
        if (d.free >= limit || (s.low && d.free * 2 > s.warnedFree)) continue;
        s.low = true;
        s.warnedFree = d.free;
        const int percent = static_cast<int>(std::lround(100.0 * static_cast<double>(d.free) / static_cast<double>(d.total)));
        const std::string free = formatBytes(d.free);
        Hooks::get().fire("disk_space_low", {{"M3_DISK_PATH", d.path}, {"M3_DISK_FREE", free}, {"M3_DISK_FREE_PERCENT", std::to_string(percent)}});

        std::vector<std::string> cmd = {"notify-send", "-a", "Storage", "-i", "drive-harddisk", "-u", "critical"};
        const int trash = Trash::get().count();
        // The trash, where it's on this disk (yours always is, in your home)
        if (trash > 0 && d.path == home) cmd.push_back(std::format("--action=trash=Empty trash ({} item{})", trash, trash == 1 ? "" : "s"));
        cmd.push_back("--action=storage=Free up space");
        cmd.push_back("Low disk space");
        cmd.push_back(std::format("{} has {} left ({}%). Apps may stop saving your work when it runs out.", d.name, free, percent));
        run(cmd, [this](int, const std::string& out) {
            if (out.starts_with("trash")) Trash::get().empty();
            else if (out.starts_with("storage") && openStorage) openStorage();
        });
    }
}

} // namespace m3
