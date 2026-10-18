#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <functional>
#include <string>
#include <vector>

namespace m3 {

// Live weather from Open-Meteo (no API key), fetched with curl every 15
// minutes for the location in config.json (weather.location), or the one
// found from the IP address. The last reply is cached, so the bar, the lock
// screen and the greeter show something before the network is up.
class Weather {
public:
    struct Day {
        std::string date; // YYYY-MM-DD
        double max = 0, min = 0;
        int code = 0;
    };
    struct Place {
        std::string name; // "Chennai, Tamil Nadu, India"
        double lat = 0, lon = 0;
    };

    static Weather& get();

    bool enabled() const;
    bool ready() const { return m_ready; }
    const std::string& error() const { return m_error; }
    const std::string& place() const { return m_place; }
    double temperature() const { return m_temp; }
    double feelsLike() const { return m_feels; }
    int humidity() const { return m_humidity; }
    double wind() const { return m_wind; }
    int code() const { return m_code; }
    bool isDay() const { return m_isDay; }
    const std::vector<Day>& days() const { return m_days; }
    long long updated() const { return m_updated; } // unix time of the data

    bool imperial() const;
    std::string temp(double t) const;   // "24°"
    std::string windText() const;       // "12 km/h"
    static std::string icon(int code, bool day);  // Material Symbol
    static std::string describe(int code);        // "Partly cloudy"
    std::string icon() const { return icon(m_code, m_isDay); }

    void refresh();
    // Places matching a name (Open-Meteo geocoding)
    void search(const std::string& query, std::function<void(std::vector<Place>)> done);
    // Location from the IP address; stored like a picked place
    void detect();
    void setPlace(const Place& p); // writes weather.location and refreshes

    Signal<> changed;

private:
    Weather();
    void load(const std::string& json, bool fromCache);
    void schedule();
    std::string cacheFile() const;

    bool m_ready = false, m_isDay = true, m_fetching = false;
    std::string m_error, m_place;
    double m_temp = 0, m_feels = 0, m_wind = 0;
    int m_humidity = 0, m_code = 0;
    long long m_updated = 0;
    std::vector<Day> m_days;
    EventLoop::TimerId m_timer = 0;
    std::string m_lastQuery; // the location the data is for
};

} // namespace m3
