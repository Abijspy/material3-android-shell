#pragma once

#include <optional>
#include <string>

namespace m3::units {

// A unit conversion Spotlight answers: "5 km to mi", "100 f in c",
// "3.5kg lb", "10 gb in mib", or a lone "5 km" (to its usual counterpart:
// metric ↔ imperial). Length, mass, temperature, volume, speed, area, data
// and time
struct Conversion {
    double value = 0, result = 0;
    std::string from, to; // the units' names, "kilometres", "miles"
    std::string text;     // "3.107 mi", for copying
};
std::optional<Conversion> convert(const std::string& query);

} // namespace m3::units
