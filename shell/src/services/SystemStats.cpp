#include "services/SystemStats.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sys/stat.h>
#include <sstream>
#include <sys/statvfs.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3 {

std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024 && u < 4) {
        v /= 1024;
        ++u;
    }
    return u == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", v, units[u]);
}

SystemStats& SystemStats::get() {
    static SystemStats instance;
    return instance;
}

SystemStats::Lease SystemStats::lease() {
    if (m_leases++ == 0) {
        sample();
        m_timer = EventLoop::get().addTimer(1000, [this] { sample(); }, true);
    }
    return Lease(nullptr, [this](void*) {
        if (--m_leases == 0 && m_timer) {
            EventLoop::get().cancel(m_timer);
            m_timer = 0;
        }
    });
}

void SystemStats::sample() {
    sampleCpu();
    sampleMemory();
    sampleDisks();
    sampleTemperature();
    sampleProcesses();
    std::ifstream("/proc/uptime") >> m_uptime;
    changed.emit();
}

void SystemStats::sampleCpu() {
    std::ifstream in("/proc/stat");
    std::string label;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    in >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    const uint64_t idleAll = idle + iowait;
    const uint64_t total = user + nice + system + idleAll + irq + softirq + steal;
    if (m_lastTotal && total > m_lastTotal)
        m_cpu = 100.0 * (1.0 - static_cast<double>(idleAll - m_lastIdle) / static_cast<double>(total - m_lastTotal));
    m_lastIdle = idleAll;
    m_lastTotal = total;
    m_history.push_back(m_cpu);
    while (m_history.size() > 60) m_history.pop_front();
    m_cores = std::max(1, static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN)));
}

void SystemStats::sampleMemory() {
    std::ifstream in("/proc/meminfo");
    std::string key;
    uint64_t value;
    std::string unit;
    uint64_t total = 0, available = 0, swapTotal = 0, swapFree = 0;
    while (in >> key >> value) {
        std::getline(in, unit);
        if (key == "MemTotal:") total = value;
        else if (key == "MemAvailable:") available = value;
        else if (key == "SwapTotal:") swapTotal = value;
        else if (key == "SwapFree:") swapFree = value;
    }
    m_memTotal = total * 1024;
    m_memUsed = (total - available) * 1024;
    m_swapTotal = swapTotal * 1024;
    m_swapUsed = (swapTotal - swapFree) * 1024;
}

void SystemStats::sampleDisks() {
    m_disks.clear();
    std::ifstream in("/proc/mounts");
    std::string dev, mount, type, rest;
    while (in >> dev >> mount >> type) {
        std::getline(in, rest);
        // Real filesystems only
        if (!dev.starts_with("/dev/") || type == "squashfs") continue;
        if (std::ranges::find(m_disks, mount, &Disk::mount) != m_disks.end()) continue;
        struct statvfs st{};
        if (statvfs(mount.c_str(), &st) != 0 || st.f_blocks == 0) continue;
        const uint64_t total = static_cast<uint64_t>(st.f_blocks) * st.f_frsize;
        const uint64_t avail = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
        m_disks.push_back({mount, total, total - avail});
    }
    std::ranges::sort(m_disks, {}, &Disk::mount);
}

void SystemStats::sampleTemperature() {
    // hwmon sensors named after the CPU driver; else the first thermal zone
    std::error_code ec;
    for (const auto& h : fs::directory_iterator("/sys/class/hwmon", ec)) {
        std::string name;
        std::ifstream(h.path() / "name") >> name;
        if (name != "coretemp" && name != "k10temp" && name != "zenpower" && name != "cpu_thermal") continue;
        long milli = 0;
        std::ifstream(h.path() / "temp1_input") >> milli;
        if (milli > 0) {
            m_temp = static_cast<double>(milli) / 1000.0;
            return;
        }
    }
    long milli = 0;
    std::ifstream("/sys/class/thermal/thermal_zone0/temp") >> milli;
    m_temp = milli > 0 ? static_cast<double>(milli) / 1000.0 : 0;
}

void SystemStats::sampleProcesses() {
    std::vector<Process> procs;
    std::map<int, uint64_t> ticks;
    const long page = sysconf(_SC_PAGESIZE);
    const uint64_t total = m_lastTotal;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/proc", ec)) {
        const std::string pidStr = e.path().filename();
        if (!std::ranges::all_of(pidStr, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) continue;
        std::ifstream in(e.path() / "stat");
        std::string line;
        if (!std::getline(in, line)) continue;
        // comm may contain spaces: it sits between the first '(' and last ')'
        const auto open = line.find('('), close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos) continue;
        Process p;
        try {
        p.pid = std::stoi(pidStr);
        p.name = line.substr(open + 1, close - open - 1);
        if (close + 2 < line.size()) p.state = line[close + 2];
        struct stat st {};
        p.mine = ::stat(e.path().c_str(), &st) == 0 && st.st_uid == getuid();
        std::istringstream rest(line.substr(close + 2));
        std::string field;
        uint64_t utime = 0, stime = 0;
        long rss = 0;
        for (int i = 3; rest >> field; ++i) {
            if (i == 14) utime = std::stoull(field);
            else if (i == 15) stime = std::stoull(field);
            else if (i == 24) {
                rss = std::stol(field);
                break;
            }
        }
        ticks[p.pid] = utime + stime;
        if (auto prev = m_lastProcTicks.find(p.pid); prev != m_lastProcTicks.end() && total > m_lastProcTotal)
            // Share of all CPU time, scaled to "% of one core" like top
            p.cpu = 100.0 * static_cast<double>(utime + stime - prev->second) / static_cast<double>(total - m_lastProcTotal) * m_cores;
        p.rss = static_cast<uint64_t>(rss) * static_cast<uint64_t>(page);
        } catch (const std::exception&) {
            continue; // the process exited mid-read
        }
        procs.push_back(std::move(p));
    }
    m_lastProcTicks = std::move(ticks);
    m_lastProcTotal = total;
    std::ranges::sort(procs, [](const Process& a, const Process& b) { return a.cpu != b.cpu ? a.cpu > b.cpu : a.rss > b.rss; });
    m_all = procs;
    if (procs.size() > 12) procs.resize(12);
    m_procs = std::move(procs);
}

} // namespace m3
