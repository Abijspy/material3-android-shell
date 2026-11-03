#include "services/DataUsage.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>

namespace m3 {

namespace {

std::string file() {
    return paths::stateDir() + "/datausage.json";
}

std::string dateOf(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

std::string bootId() {
    std::string id;
    std::getline(std::ifstream("/proc/sys/kernel/random/boot_id"), id);
    return id;
}

// The interfaces that reach out: not loopback, nor containers' and VMs'
// bridges and pairs (their traffic is counted again on the real one)
bool counted(const std::string& name) {
    for (const char* skip : {"lo", "docker", "veth", "br-", "virbr", "vnet", "tun", "tap", "wg", "tailscale", "zt"})
        if (name.starts_with(skip)) return false;
    return true;
}

} // namespace

DataUsage& DataUsage::get() {
    static DataUsage instance;
    return instance;
}

void DataUsage::start() {
    if (m_started) return;
    m_started = true;
    load();
    sample();
    EventLoop::get().addTimer(60 * 1000, [this] { sample(); }, true);
}

void DataUsage::load() {
    try {
        std::ifstream in(file());
        if (!in) return;
        const auto j = nlohmann::json::parse(in);
        m_boot = j.value("boot", "");
        for (const auto& d : j.value("days", nlohmann::json::array()))
            m_days.push_back({d.value("date", ""), d.value("rx", uint64_t{0}), d.value("tx", uint64_t{0})});
        const auto counters = j.value("counters", nlohmann::json::object()); // kept: items() of a temporary would outlive it
        for (const auto& [name, c] : counters.items())
            if (c.is_array() && c.size() == 2) m_counters.push_back({name, {c[0].get<uint64_t>(), c[1].get<uint64_t>()}});
    } catch (const std::exception& e) {
        log::warn("datausage.json: {}", e.what());
    }
}

void DataUsage::save() const {
    nlohmann::json days = nlohmann::json::array(), counters = nlohmann::json::object();
    for (const auto& d : m_days) days.push_back({{"date", d.date}, {"rx", d.rx}, {"tx", d.tx}});
    for (const auto& [name, c] : m_counters) counters[name] = {c.first, c.second};
    std::error_code ec;
    std::filesystem::create_directories(paths::stateDir(), ec);
    const std::string f = file();
    std::ofstream(f + ".tmp") << nlohmann::json{{"boot", m_boot}, {"days", days}, {"counters", counters}}.dump(1);
    std::filesystem::rename(f + ".tmp", f, ec);
}

void DataUsage::sample() {
    // Each interface's totals: "  wlan0: <rx bytes> … (8 fields) <tx bytes> …"
    std::map<std::string, std::pair<uint64_t, uint64_t>> now;
    std::ifstream in("/proc/net/dev");
    for (std::string line; std::getline(in, line);) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(' '));
        if (!counted(name)) continue;
        std::stringstream ss(line.substr(colon + 1));
        uint64_t f[9] = {};
        for (auto& v : f) ss >> v;
        now[name] = {f[0], f[8]};
    }
    // What moved since the last look; a new boot starts from nothing, an
    // interface that came back (its counters lower) from its own zero
    const std::string boot = bootId();
    const bool sameBoot = boot == m_boot;
    uint64_t rx = 0, tx = 0;
    for (const auto& [name, c] : now) {
        auto it = std::ranges::find(m_counters, name, &std::pair<std::string, std::pair<uint64_t, uint64_t>>::first);
        uint64_t r0 = 0, t0 = 0;
        if (sameBoot && it != m_counters.end()) {
            r0 = it->second.first;
            t0 = it->second.second;
        }
        rx += c.first >= r0 ? c.first - r0 : c.first;
        tx += c.second >= t0 ? c.second - t0 : c.second;
    }
    // The first look ever only sets where counting starts
    const bool first = m_boot.empty();
    m_boot = boot;
    m_counters.assign(now.begin(), now.end());
    if (!first && (rx || tx)) {
        const std::string today = dateOf(std::time(nullptr));
        if (m_days.empty() || m_days.back().date != today) m_days.push_back({today, 0, 0});
        m_days.back().rx += rx;
        m_days.back().tx += tx;
        // Two months is plenty
        if (m_days.size() > 62) m_days.erase(m_days.begin(), m_days.end() - 62);
        changed.emit();
    }
    save();
}

DataUsage::Day DataUsage::today() const {
    const std::string d = dateOf(std::time(nullptr));
    if (!m_days.empty() && m_days.back().date == d) return m_days.back();
    return {d, 0, 0};
}

DataUsage::Day DataUsage::month() const {
    const std::string prefix = dateOf(std::time(nullptr)).substr(0, 7);
    Day m{prefix, 0, 0};
    for (const auto& d : m_days)
        if (d.date.starts_with(prefix)) {
            m.rx += d.rx;
            m.tx += d.tx;
        }
    return m;
}

std::vector<DataUsage::Day> DataUsage::lastDays(int n) const {
    std::vector<Day> out;
    const std::time_t now = std::time(nullptr);
    for (int i = n - 1; i >= 0; --i) {
        const std::string d = dateOf(now - static_cast<std::time_t>(i) * 86400);
        auto it = std::ranges::find(m_days, d, &Day::date);
        out.push_back(it != m_days.end() ? *it : Day{d, 0, 0});
    }
    return out;
}

} // namespace m3
