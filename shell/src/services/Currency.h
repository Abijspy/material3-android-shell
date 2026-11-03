#pragma once

#include "core/Signal.h"

#include <ctime>
#include <map>
#include <optional>
#include <string>

namespace m3::currency {

// Exchange rates, as the European Central Bank publishes them each working
// day (through frankfurter.dev, which needs no key): how much of each
// currency one euro buys
struct Rates {
    std::string date; // "2026-11-02"
    std::map<std::string, double> perEuro;
};

struct Conversion {
    double value = 0, result = 0;
    std::string from, to; // "USD", "INR"
    std::string text;     // "8,350.25 INR", for copying: "8350.25"
};

// "100 usd to inr", "$100 in eur", "50 euros to rupees", or a lone "100 usd"
// (to `local`). Pure: with these rates; nullopt when it isn't a conversion
std::optional<Conversion> convert(const std::string& query, const Rates& rates, const std::string& local);
// Shaped like one (a number and a currency), whatever the rates
bool looksLike(const std::string& query);
// A currency for a time zone ("Asia/Kolkata" → "INR"); "USD" when unknown
std::string forZone(const std::string& zone);

// The rates, cached a day in ~/.cache/material3ui/currency.json and fetched
// only when a conversion is asked for (Settings → Spotlight)
class Service {
public:
    static Service& get();
    const Rates* rates(); // the cached ones (null until fetched once); fetches again when stale
    std::string local() const; // Settings → Spotlight → Your currency, else the time zone's
    Signal<> ready;           // new rates arrived
private:
    Service();
    void fetch();
    Rates m_rates;
    std::time_t m_fetched = 0;
    bool m_fetching = false;
};

} // namespace m3::currency
