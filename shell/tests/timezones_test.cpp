// The time zone map's places: zone.tab's coordinates (both ISO 6709
// lengths), city and country names, and the nearest zone to a click on a
// flat map
#include "services/TimeZones.h"

#include <cmath>
#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

int main() {
    double lat = 0, lon = 0;
    expect(tz::parseCoordinates("+1345+10031", lat, lon) && near(lat, 13.75) && near(lon, 100 + 31 / 60.0), "DDMM+DDDMM");
    expect(tz::parseCoordinates("+404251-0740023", lat, lon) && near(lat, 40 + 42 / 60.0 + 51 / 3600.0) &&
               near(lon, -(74 + 0 / 60.0 + 23 / 3600.0)),
           "DDMMSS-DDDMMSS");
    expect(tz::parseCoordinates("-3352+15113", lat, lon) && lat < 0 && lon > 0, "south and east");
    expect(!tz::parseCoordinates("+1345", lat, lon), "no longitude");
    expect(!tz::parseCoordinates("1345+10031", lat, lon), "no sign");
    expect(!tz::parseCoordinates("+1375+10031", lat, lon), "75 minutes");
    expect(!tz::parseCoordinates("+134+10031", lat, lon), "odd digits");
    expect(!tz::parseCoordinates("+1345+10031x", lat, lon), "trailing junk");

    expect(tz::cityName("America/Argentina/Buenos_Aires") == "Buenos Aires", "city of a three-part id");
    expect(tz::cityName("UTC") == "UTC", "city of a one-part id");

    const auto zones = tz::parse("# comment\n"
                                 "IN\t+2232+08822\tAsia/Kolkata\n"
                                 "US\t+404251-0740023\tAmerica/New_York\tEastern (most areas)\n"
                                 "FJ\t-1808+17825\tPacific/Fiji\n"
                                 "WS\t-1350-17144\tPacific/Apia\n"
                                 "XX\tbad\tNowhere/Bad\n"
                                 "IN\t+2232+08822\tAsia/Kolkata\n",
                                 "#code\tname\nIN\tIndia\nUS\tUnited States\nFJ\tFiji\n");
    expect(zones.size() == 4, "four zones: a malformed line and a repeat left out");
    if (zones.size() == 4) {
        expect(zones[0].id == "Asia/Kolkata" && zones[0].city == "Kolkata" && zones[0].country == "India", "Kolkata, India");
        expect(zones[1].city == "New York" && zones[1].note == "Eastern (most areas)", "New York with its note");
        expect(zones[3].country.empty(), "a country missing from iso3166.tab");
        expect(tz::nearest(zones, 28.6, 77.2) == 0, "Delhi is nearest Kolkata");
        expect(tz::nearest(zones, 40, -100) == 1, "Kansas is nearest New York");
        // A flat map's edges don't meet: each picks the places on its side
        expect(tz::nearest(zones, -17, 179.9) == 2, "the right edge picks Fiji");
        expect(tz::nearest(zones, -17, -179.9) == 3, "the left edge picks Apia");
        expect(tz::find(zones, "America/New_York") == 1, "find by id");
        expect(tz::find(zones, "Nowhere/Else") == -1, "an unknown id");
        expect(tz::find(zones, "../../etc/passwd") == -1, "no paths out of zoneinfo");
    }
    expect(tz::nearest({}, 0, 0) == -1, "no zones, none nearest");

    if (failures) return 1;
    std::puts("timezones: ok");
    return 0;
}
