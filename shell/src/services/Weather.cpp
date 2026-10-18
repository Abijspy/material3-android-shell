#include "services/Weather.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

constexpr int kRefreshMs = 15 * 60 * 1000;

std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

void fetch(const std::string& url, std::function<void(bool ok, const std::string& body)> done) {
    run({"curl", "-fsSL", "--max-time", "15", url}, [done = std::move(done)](int code, const std::string& out) { done(code == 0, out); });
}

} // namespace

Weather& Weather::get() {
    static Weather instance;
    return instance;
}

Weather::Weather() {
    // Cached data first: something to show before the network answers
    std::ifstream in(cacheFile());
    std::stringstream ss;
    ss << in.rdbuf();
    if (!ss.str().empty()) load(ss.str(), true);
    Config::get().changed.connectForever([this] {
        // A new place or unit system: fetch again
        const auto loc = Config::get().value<nlohmann::json>("/weather/location", nlohmann::json()).dump() +
                         Config::get().value<std::string>("/weather/units", "metric");
        if (enabled() && loc != m_lastQuery) refresh();
    });
    if (enabled()) EventLoop::get().defer([this] { refresh(); });
}

bool Weather::enabled() const {
    return Config::get().value<bool>("/weather/enabled", true);
}

bool Weather::imperial() const {
    return Config::get().value<std::string>("/weather/units", "metric") == "imperial";
}

std::string Weather::cacheFile() const {
    // The greeter reads the copy synced for it
    if (const char* f = std::getenv("M3_WEATHER_FILE"); f && *f) return f;
    return paths::stateDir() + "/weather.json";
}

std::string Weather::temp(double t) const {
    return std::format("{:.0f}°", t);
}

std::string Weather::windText() const {
    return std::format("{:.0f} {}", m_wind, imperial() ? "mph" : "km/h");
}

void Weather::refresh() {
    if (m_fetching) return;
    const auto loc = Config::get().value<nlohmann::json>("/weather/location", nlohmann::json());
    m_lastQuery = loc.dump() + Config::get().value<std::string>("/weather/units", "metric");
    if (!loc.is_object() || !loc.contains("lat")) {
        // No place chosen yet: find one from the IP address, then come back
        detect();
        return;
    }
    const double lat = loc.value("lat", 0.0), lon = loc.value("lon", 0.0);
    const std::string name = loc.value("name", "");
    std::string url = std::format(
        "https://api.open-meteo.com/v1/forecast?latitude={:.4f}&longitude={:.4f}"
        "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=7",
        lat, lon);
    if (imperial()) url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";
    m_fetching = true;
    fetch(url, [this, name](bool ok, const std::string& body) {
        m_fetching = false;
        if (!ok) {
            m_error = "Weather is unavailable (offline?)";
            changed.emit();
        } else {
            // Keep the place's name with the data, for the greeter's copy
            try {
                auto j = nlohmann::json::parse(body);
                j["place"] = name;
                j["fetchedAt"] = static_cast<long long>(std::time(nullptr));
                const std::string text = j.dump();
                load(text, false);
                std::error_code ec;
                fs::create_directories(fs::path(cacheFile()).parent_path(), ec);
                std::ofstream(cacheFile()) << text;
            } catch (const std::exception& e) {
                m_error = "Unexpected weather reply";
                log::warn("weather: {}", e.what());
                changed.emit();
            }
        }
        schedule();
    });
}

void Weather::schedule() {
    if (m_timer) EventLoop::get().cancel(m_timer);
    m_timer = EventLoop::get().addTimer(kRefreshMs, [this] {
        m_timer = 0;
        if (enabled()) refresh();
    });
}

void Weather::load(const std::string& text, bool fromCache) {
    try {
        const auto j = nlohmann::json::parse(text);
        const auto& c = j.at("current");
        m_temp = c.value("temperature_2m", 0.0);
        m_feels = c.value("apparent_temperature", m_temp);
        m_humidity = c.value("relative_humidity_2m", 0);
        m_wind = c.value("wind_speed_10m", 0.0);
        m_code = c.value("weather_code", 0);
        m_isDay = c.value("is_day", 1) != 0;
        m_place = j.value("place", "");
        m_updated = j.value("fetchedAt", 0LL);
        m_days.clear();
        if (j.contains("daily")) {
            const auto& d = j["daily"];
            const auto& dates = d.at("time");
            for (size_t i = 0; i < dates.size(); ++i)
                m_days.push_back({dates[i].get<std::string>(), d["temperature_2m_max"][i].get<double>(),
                                  d["temperature_2m_min"][i].get<double>(), d["weather_code"][i].get<int>()});
        }
        m_ready = true;
        if (!fromCache) m_error.clear();
        changed.emit();
    } catch (const std::exception& e) {
        if (!fromCache) throw;
    }
}

void Weather::search(const std::string& query, std::function<void(std::vector<Place>)> done) {
    if (query.size() < 2) return done({});
    fetch("https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=" + urlEncode(query),
          [done](bool ok, const std::string& body) {
              std::vector<Place> out;
              if (ok) try {
                  const auto j = nlohmann::json::parse(body);
                  if (j.contains("results"))
                      for (const auto& r : j["results"]) {
                          std::string name = r.value("name", "");
                          for (const char* k : {"admin1", "country"})
                              if (auto v = r.value(k, std::string()); !v.empty() && v != name) name += ", " + v;
                          out.push_back({name, r.value("latitude", 0.0), r.value("longitude", 0.0)});
                      }
              } catch (...) {
              }
              done(out);
          });
}

void Weather::detect() {
    fetch("https://ipapi.co/json/", [this](bool ok, const std::string& body) {
        try {
            if (!ok) throw std::runtime_error("no reply");
            const auto j = nlohmann::json::parse(body);
            if (!j.contains("latitude")) throw std::runtime_error("no location");
            std::string name = j.value("city", "Here");
            if (auto r = j.value("region", std::string()); !r.empty()) name += ", " + r;
            if (auto c = j.value("country_name", std::string()); !c.empty()) name += ", " + c;
            setPlace({name, j["latitude"].get<double>(), j["longitude"].get<double>()});
        } catch (...) {
            m_error = "Couldn't find your location; search for a city in Settings → Weather";
            changed.emit();
        }
    });
}

void Weather::setPlace(const Place& p) {
    Config::get().set("/weather/location", {{"name", p.name}, {"lat", p.lat}, {"lon", p.lon}});
    // The config watcher refreshes; until then, show the new name
    m_place = p.name;
    changed.emit();
}

std::string Weather::icon(int code, bool day) {
    // WMO weather interpretation codes
    if (code == 0) return day ? "clear_day" : "clear_night";
    if (code <= 2) return day ? "partly_cloudy_day" : "partly_cloudy_night";
    if (code == 3) return "cloud";
    if (code == 45 || code == 48) return "foggy";
    if (code >= 51 && code <= 57) return "rainy_light";
    if (code >= 61 && code <= 67) return "rainy";
    if (code >= 71 && code <= 77) return "weather_snowy";
    if (code >= 80 && code <= 82) return "rainy_heavy";
    if (code == 85 || code == 86) return "snowing_heavy";
    if (code >= 95) return "thunderstorm";
    return "cloud";
}

std::string Weather::describe(int code) {
    if (code == 0) return "Clear";
    if (code == 1) return "Mostly clear";
    if (code == 2) return "Partly cloudy";
    if (code == 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code == 85 || code == 86) return "Snow showers";
    if (code >= 95) return "Thunderstorm";
    return "Unknown";
}

} // namespace m3
