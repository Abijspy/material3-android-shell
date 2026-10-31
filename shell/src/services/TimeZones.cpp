#include "services/TimeZones.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace m3::tz {

namespace {

constexpr const char* kZoneInfo = "/usr/share/zoneinfo";

// ±D…D[MM[SS]] with `degreeDigits` digits of degrees, from the front of `t`
bool angle(std::string_view& t, size_t degreeDigits, double& out) {
    if (t.empty() || (t[0] != '+' && t[0] != '-')) return false;
    const double sign = t[0] == '-' ? -1 : 1;
    size_t n = 1;
    while (n < t.size() && t[n] >= '0' && t[n] <= '9') ++n;
    const size_t digits = n - 1;
    // Degrees, then minutes, then seconds, two digits each
    if (digits != degreeDigits + 2 && digits != degreeDigits + 4) return false;
    auto num = [&](size_t at, size_t len) {
        int v = 0;
        for (size_t i = 0; i < len; ++i) v = v * 10 + (t[1 + at + i] - '0');
        return v;
    };
    const int deg = num(0, degreeDigits), min = num(degreeDigits, 2), sec = digits > degreeDigits + 2 ? num(degreeDigits + 2, 2) : 0;
    if (min >= 60 || sec >= 60) return false;
    out = sign * (deg + min / 60.0 + sec / 3600.0);
    t.remove_prefix(n);
    return true;
}

std::string readAll(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

bool parseCoordinates(std::string_view text, double& lat, double& lon) {
    std::string_view t = text;
    double a = 0, b = 0;
    if (!angle(t, 2, a) || !angle(t, 3, b) || !t.empty()) return false;
    if (std::abs(a) > 90 || std::abs(b) > 180) return false;
    lat = a;
    lon = b;
    return true;
}

std::string cityName(std::string_view id) {
    if (const auto slash = id.rfind('/'); slash != std::string_view::npos) id.remove_prefix(slash + 1);
    std::string city(id);
    for (auto& c : city)
        if (c == '_') c = ' ';
    return city;
}

std::vector<Zone> parse(std::string_view zoneTab, std::string_view iso3166) {
    auto lines = [](std::string_view text, auto&& each) {
        while (!text.empty()) {
            const size_t end = text.find('\n');
            std::string_view line = text.substr(0, end);
            text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string_view> fields;
            while (true) {
                const size_t tab = line.find('\t');
                fields.push_back(line.substr(0, tab));
                if (tab == std::string_view::npos) break;
                line.remove_prefix(tab + 1);
            }
            each(fields);
        }
    };
    std::map<std::string, std::string, std::less<>> countries;
    lines(iso3166, [&](const auto& f) {
        if (f.size() >= 2) countries[std::string(f[0])] = std::string(f[1]);
    });
    std::vector<Zone> zones;
    std::set<std::string, std::less<>> seen;
    lines(zoneTab, [&](const auto& f) {
        if (f.size() < 3 || f[2].empty() || seen.contains(f[2])) return;
        Zone z;
        if (!parseCoordinates(f[1], z.lat, z.lon)) return;
        z.id = std::string(f[2]);
        z.city = cityName(z.id);
        if (const auto it = countries.find(f[0]); it != countries.end()) z.country = it->second;
        if (f.size() > 3) z.note = std::string(f[3]);
        seen.insert(z.id);
        zones.push_back(std::move(z));
    });
    return zones;
}

const std::vector<Zone>& system() {
    static const std::vector<Zone> zones = [] {
        const std::string dir = kZoneInfo;
        return parse(readAll(dir + "/zone.tab"), readAll(dir + "/iso3166.tab"));
    }();
    return zones;
}

int nearest(const std::vector<Zone>& zones, double lat, double lon) {
    int best = -1;
    double bestD = 0;
    for (size_t i = 0; i < zones.size(); ++i) {
        const double dx = zones[i].lon - lon, dy = zones[i].lat - lat, d = dx * dx + dy * dy;
        if (best < 0 || d < bestD) {
            best = static_cast<int>(i);
            bestD = d;
        }
    }
    return best;
}

int find(const std::vector<Zone>& zones, const std::string& id) {
    for (size_t i = 0; i < zones.size(); ++i)
        if (zones[i].id == id) return static_cast<int>(i);
    // A link or an old name: the same zoneinfo file as one of the zones
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path file = fs::path(kZoneInfo) / id;
    if (id.empty() || id.find("..") != std::string::npos || !fs::exists(file, ec)) return -1;
    for (size_t i = 0; i < zones.size(); ++i)
        if (fs::equivalent(file, fs::path(kZoneInfo) / zones[i].id, ec)) return static_cast<int>(i);
    return -1;
}

} // namespace m3::tz
