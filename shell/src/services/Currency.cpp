#include "services/Currency.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace m3::currency {

// ── Service ──

namespace {
std::string cacheFile() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.cache") + "/material3ui/currency.json";
}
} // namespace

Service& Service::get() {
    static Service instance;
    return instance;
}

Service::Service() {
    try {
        std::ifstream in(cacheFile());
        if (!in) return;
        const auto j = nlohmann::json::parse(in);
        m_fetched = j.value("fetched", static_cast<std::time_t>(0));
        m_rates.date = j.value("date", "");
        const auto rates = j.value("rates", nlohmann::json::object()); // kept: items() of a temporary would outlive it
        for (const auto& [code, v] : rates.items())
            if (v.is_number()) m_rates.perEuro[code] = v.get<double>();
    } catch (const std::exception& e) {
        log::warn("currency.json: {}", e.what());
    }
}

const Rates* Service::rates() {
    // A day's rates (the ECB's change once a working day): looked for again after 12 hours
    if (!m_fetching && std::time(nullptr) - m_fetched > 12 * 3600) fetch();
    return m_rates.perEuro.empty() ? nullptr : &m_rates;
}

std::string Service::local() const {
    const std::string set = Config::get().value<std::string>("/spotlight/localCurrency", "");
    if (!set.empty()) return set;
    std::error_code ec;
    std::string zone = std::filesystem::read_symlink("/etc/localtime", ec).string();
    if (const auto at = zone.find("zoneinfo/"); at != std::string::npos) zone = zone.substr(at + 9);
    return forZone(zone);
}

void Service::fetch() {
    m_fetching = true;
    // Redirects followed: the service has moved its address before
    run({"curl", "-fsSL", "--max-time", "10", "https://api.frankfurter.dev/v1/latest"}, [this](int code, const std::string& out) {
        m_fetching = false;
        m_fetched = std::time(nullptr); // tried: not again for a while, worked or not
        if (code != 0) {
            log::warn("currency: couldn't fetch rates");
            return;
        }
        try {
            const auto j = nlohmann::json::parse(out);
            Rates r;
            r.date = j.value("date", "");
            const auto rates = j.value("rates", nlohmann::json::object());
            for (const auto& [c, v] : rates.items())
                if (v.is_number()) r.perEuro[c] = v.get<double>();
            if (r.perEuro.empty()) return;
            m_rates = std::move(r);
            nlohmann::json save = {{"fetched", m_fetched}, {"date", m_rates.date}, {"rates", m_rates.perEuro}};
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(cacheFile()).parent_path(), ec);
            std::ofstream(cacheFile()) << save.dump();
            ready.emit();
        } catch (const std::exception& e) {
            log::warn("currency: {}", e.what());
        }
    });
}

} // namespace m3::currency
