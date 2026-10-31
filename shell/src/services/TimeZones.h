#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace m3::tz {

// One of tzdata's zones with the place it's named for: zone.tab's line
// (its country, coordinates and note), with the country's name from
// iso3166.tab
struct Zone {
    std::string id;      // Asia/Kolkata
    std::string city;    // Kolkata (America/Argentina/Buenos_Aires: Buenos Aires)
    std::string country; // India; "" when the code is unknown
    std::string note;    // zone.tab's comment: "Eastern (most areas)"
    double lat = 0, lon = 0;
};

// ISO 6709 as zone.tab writes it, ±DDMM±DDDMM or ±DDMMSS±DDDMMSS, in
// degrees (north and east positive); false when malformed
bool parseCoordinates(std::string_view text, double& lat, double& lon);
// The place a zone id names: its last part, underscores as spaces
std::string cityName(std::string_view id);
// zone.tab and iso3166.tab, as text; each zone once, in zone.tab's order
std::vector<Zone> parse(std::string_view zoneTab, std::string_view iso3166);
// The system's (/usr/share/zoneinfo), read once
const std::vector<Zone>& system();

// The zone nearest (lat, lon) as a flat map shows them: the map's edges
// don't meet, so the far right picks Fiji, not Samoa across the date line;
// -1 when there are none
int nearest(const std::vector<Zone>& zones, double lat, double lon);
// The index of `id`, or of a zone that's the same file in zoneinfo (old
// names, links: Asia/Calcutta finds Asia/Kolkata); -1 when none is
int find(const std::vector<Zone>& zones, const std::string& id);

} // namespace m3::tz
