#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace m3 {

// CPU, memory, disks, temperatures and processes from /proc and /sys.
// Sampling only runs while someone holds a Lease (an open dashboard or
// system monitor), once a second.
class SystemStats {
public:
    struct Disk {
        std::string mount;
        uint64_t total = 0, used = 0;
    };
    struct Process {
        int pid = 0;
        std::string name;
        double cpu = 0;    // % of one core
        uint64_t rss = 0;  // bytes
        char state = '?';  // R running, S sleeping, T stopped, Z zombie…
        bool mine = false; // the user's own (no root needed to signal it)
    };

    static SystemStats& get();

    // Keep sampling while held
    using Lease = std::shared_ptr<void>;
    [[nodiscard]] Lease lease();

    double cpu() const { return m_cpu; } // 0–100, all cores
    const std::deque<double>& cpuHistory() const { return m_history; }
    uint64_t memTotal() const { return m_memTotal; }
    uint64_t memUsed() const { return m_memUsed; }
    uint64_t swapTotal() const { return m_swapTotal; }
    uint64_t swapUsed() const { return m_swapUsed; }
    double temperature() const { return m_temp; } // °C of the CPU package, 0 unknown
    double uptime() const { return m_uptime; }
    int cores() const { return m_cores; }
    const std::vector<Disk>& disks() const { return m_disks; }
    const std::vector<Process>& processes() const { return m_procs; } // by CPU, top 12
    const std::vector<Process>& allProcesses() const { return m_all; } // every one, by CPU

    Signal<> changed;

private:
    SystemStats() = default;
    void sample();
    void sampleCpu();
    void sampleMemory();
    void sampleDisks();
    void sampleTemperature();
    void sampleProcesses();

    int m_leases = 0;
    EventLoop::TimerId m_timer = 0;

    uint64_t m_lastIdle = 0, m_lastTotal = 0;
    double m_cpu = 0, m_temp = 0, m_uptime = 0;
    int m_cores = 1;
    std::deque<double> m_history;
    uint64_t m_memTotal = 0, m_memUsed = 0, m_swapTotal = 0, m_swapUsed = 0;
    std::vector<Disk> m_disks;
    std::vector<Process> m_procs, m_all;
    std::map<int, uint64_t> m_lastProcTicks;
    uint64_t m_lastProcTotal = 0;
};

std::string formatBytes(uint64_t bytes);

} // namespace m3
