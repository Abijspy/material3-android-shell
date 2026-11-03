#include "services/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <vector>

namespace m3::units {

namespace {

enum class Kind { Length, Mass, Temperature, Volume, Speed, Area, Data, Time };

struct Unit {
    const char* symbol;           // what's shown after the number
    const char* name;             // plural, for the subtitle
    std::vector<const char*> say; // what's typed (lower case)
    Kind kind;
    double factor;                // to the kind's base unit (temperature: see below)
    const char* counterpart;      // a lone "5 km" converts to this
};

const std::vector<Unit>& all() {
    static const std::vector<Unit> u = {
        // Length, in metres
        {"mm", "millimetres", {"mm", "millimetre", "millimetres", "millimeter", "millimeters"}, Kind::Length, 0.001, "in"},
        {"cm", "centimetres", {"cm", "centimetre", "centimetres", "centimeter", "centimeters"}, Kind::Length, 0.01, "in"},
        {"m", "metres", {"m", "metre", "metres", "meter", "meters"}, Kind::Length, 1, "ft"},
        {"km", "kilometres", {"km", "kilometre", "kilometres", "kilometer", "kilometers"}, Kind::Length, 1000, "mi"},
        {"in", "inches", {"in", "inch", "inches", "\""}, Kind::Length, 0.0254, "cm"},
        {"ft", "feet", {"ft", "foot", "feet", "'"}, Kind::Length, 0.3048, "m"},
        {"yd", "yards", {"yd", "yard", "yards"}, Kind::Length, 0.9144, "m"},
        {"mi", "miles", {"mi", "mile", "miles"}, Kind::Length, 1609.344, "km"},
        {"nmi", "nautical miles", {"nmi", "nautical mile", "nautical miles"}, Kind::Length, 1852, "km"},
        // Mass, in kilograms
        {"mg", "milligrams", {"mg", "milligram", "milligrams"}, Kind::Mass, 1e-6, "g"},
        {"g", "grams", {"g", "gram", "grams"}, Kind::Mass, 0.001, "oz"},
        {"kg", "kilograms", {"kg", "kilo", "kilos", "kilogram", "kilograms"}, Kind::Mass, 1, "lb"},
        {"t", "tonnes", {"t", "tonne", "tonnes", "ton", "tons"}, Kind::Mass, 1000, "lb"},
        {"oz", "ounces", {"oz", "ounce", "ounces"}, Kind::Mass, 0.028349523125, "g"},
        {"lb", "pounds", {"lb", "lbs", "pound", "pounds"}, Kind::Mass, 0.45359237, "kg"},
        {"st", "stone", {"st", "stone", "stones"}, Kind::Mass, 6.35029318, "kg"},
        // Temperature (factor unused)
        {"°C", "degrees Celsius", {"c", "°c", "celsius", "centigrade"}, Kind::Temperature, 0, "°F"},
        {"°F", "degrees Fahrenheit", {"f", "°f", "fahrenheit"}, Kind::Temperature, 0, "°C"},
        {"K", "kelvin", {"k", "kelvin"}, Kind::Temperature, 0, "°C"},
        // Volume, in litres
        {"ml", "millilitres", {"ml", "millilitre", "millilitres", "milliliter", "milliliters"}, Kind::Volume, 0.001, "fl oz"},
        {"l", "litres", {"l", "litre", "litres", "liter", "liters"}, Kind::Volume, 1, "gal"},
        {"tsp", "teaspoons", {"tsp", "teaspoon", "teaspoons"}, Kind::Volume, 0.00492892159375, "ml"},
        {"tbsp", "tablespoons", {"tbsp", "tablespoon", "tablespoons"}, Kind::Volume, 0.01478676478125, "ml"},
        {"cup", "cups", {"cup", "cups"}, Kind::Volume, 0.2365882365, "ml"},
        {"fl oz", "fluid ounces", {"floz", "fl oz", "fluid ounce", "fluid ounces"}, Kind::Volume, 0.0295735295625, "ml"},
        {"pt", "pints", {"pt", "pint", "pints"}, Kind::Volume, 0.473176473, "l"},
        {"gal", "gallons", {"gal", "gallon", "gallons"}, Kind::Volume, 3.785411784, "l"},
        // Speed, in metres per second
        {"m/s", "metres per second", {"m/s", "mps"}, Kind::Speed, 1, "km/h"},
        {"km/h", "kilometres per hour", {"km/h", "kmh", "kph", "kmph"}, Kind::Speed, 1 / 3.6, "mph"},
        {"mph", "miles per hour", {"mph", "mi/h"}, Kind::Speed, 0.44704, "km/h"},
        {"kn", "knots", {"kn", "kt", "knot", "knots"}, Kind::Speed, 1852.0 / 3600, "km/h"},
        // Area, in square metres
        {"m²", "square metres", {"m2", "m²", "sqm", "square metre", "square metres", "square meter", "square meters"}, Kind::Area, 1, "ft²"},
        {"km²", "square kilometres", {"km2", "km²", "square kilometre", "square kilometres"}, Kind::Area, 1e6, "mi²"},
        {"ha", "hectares", {"ha", "hectare", "hectares"}, Kind::Area, 1e4, "acre"},
        {"acre", "acres", {"acre", "acres", "ac"}, Kind::Area, 4046.8564224, "ha"},
        {"ft²", "square feet", {"ft2", "ft²", "sqft", "square foot", "square feet"}, Kind::Area, 0.09290304, "m²"},
        {"mi²", "square miles", {"mi2", "mi²", "square mile", "square miles"}, Kind::Area, 2589988.110336, "km²"},
        // Data, in bytes (kB, MB… decimal; KiB, MiB… binary)
        {"B", "bytes", {"b", "byte", "bytes"}, Kind::Data, 1, "KiB"},
        {"kB", "kilobytes", {"kb", "kilobyte", "kilobytes"}, Kind::Data, 1e3, "KiB"},
        {"MB", "megabytes", {"mb", "megabyte", "megabytes"}, Kind::Data, 1e6, "MiB"},
        {"GB", "gigabytes", {"gb", "gigabyte", "gigabytes"}, Kind::Data, 1e9, "GiB"},
        {"TB", "terabytes", {"tb", "terabyte", "terabytes"}, Kind::Data, 1e12, "TiB"},
        {"KiB", "kibibytes", {"kib", "kibibyte", "kibibytes"}, Kind::Data, 1024.0, "kB"},
        {"MiB", "mebibytes", {"mib", "mebibyte", "mebibytes"}, Kind::Data, 1048576.0, "MB"},
        {"GiB", "gibibytes", {"gib", "gibibyte", "gibibytes"}, Kind::Data, 1073741824.0, "GB"},
        {"TiB", "tebibytes", {"tib", "tebibyte", "tebibytes"}, Kind::Data, 1099511627776.0, "TB"},
        // Time, in seconds
        {"s", "seconds", {"s", "sec", "secs", "second", "seconds"}, Kind::Time, 1, "min"},
        {"min", "minutes", {"min", "mins", "minute", "minutes"}, Kind::Time, 60, "h"},
        {"h", "hours", {"h", "hr", "hrs", "hour", "hours"}, Kind::Time, 3600, "min"},
        {"d", "days", {"d", "day", "days"}, Kind::Time, 86400, "h"},
        {"wk", "weeks", {"wk", "week", "weeks"}, Kind::Time, 604800, "d"},
    };
    return u;
}

const Unit* find(std::string word) {
    std::ranges::transform(word, word.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& u : all())
        if (std::ranges::any_of(u.say, [&](const char* s) { return word == s; })) return &u;
    return nullptr;
}

const Unit* bySymbol(const std::string& symbol) {
    for (const auto& u : all())
        if (symbol == u.symbol) return &u;
    return nullptr;
}

double toBase(const Unit& u, double v) {
    if (u.kind != Kind::Temperature) return v * u.factor;
    if (u.symbol[0] == 'K') return v - 273.15;                    // to °C
    if (std::string(u.symbol) == "°F") return (v - 32) * 5 / 9;   // to °C
    return v;
}

double fromBase(const Unit& u, double v) {
    if (u.kind != Kind::Temperature) return v / u.factor;
    if (u.symbol[0] == 'K') return v + 273.15;
    if (std::string(u.symbol) == "°F") return v * 9 / 5 + 32;
    return v;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// Four significant figures, without trailing zeros; thousands as they are
std::string number(double v) {
    if (v == 0) return "0";
    const int digits = static_cast<int>(std::floor(std::log10(std::abs(v)))) + 1;
    const int decimals = std::clamp(4 - digits, 0, 6);
    std::string s = std::format("{:.{}f}", v, decimals);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s == "-0" ? "0" : s;
}

} // namespace

std::optional<Conversion> convert(const std::string& query) {
    std::string q = trim(query);
    // The number, then the units
    char* end = nullptr;
    const double value = std::strtod(q.c_str(), &end);
    if (end == q.c_str()) return std::nullopt;
    std::string rest = trim(std::string(end));
    if (rest.empty()) return std::nullopt;
    // "<unit> to|in|= <unit>", or "<unit> <unit>", or "<unit>"
    std::string fromWord = rest, toWord;
    for (const char* sep : {" to ", " in ", " = ", " -> ", " → "}) {
        if (const auto at = rest.find(sep); at != std::string::npos) {
            fromWord = trim(rest.substr(0, at));
            toWord = trim(rest.substr(at + std::string(sep).size()));
            break;
        }
    }
    const Unit* from = find(fromWord);
    const Unit* to = toWord.empty() ? nullptr : find(toWord);
    if (!from && toWord.empty()) {
        // "3kg lb": the last word is the target
        if (const auto space = rest.rfind(' '); space != std::string::npos) {
            from = find(trim(rest.substr(0, space)));
            to = find(trim(rest.substr(space + 1)));
            if (!from || !to) return std::nullopt;
        }
    }
    if (!from) return std::nullopt;
    if (!toWord.empty() && !to) return std::nullopt;
    if (!to) to = bySymbol(from->counterpart);
    if (!to || to->kind != from->kind || to == from) return std::nullopt;
    Conversion c;
    c.value = value;
    c.result = fromBase(*to, toBase(*from, value));
    c.from = from->name;
    c.to = to->name;
    c.text = number(c.result) + (std::string(to->symbol).starts_with("°") ? "" : " ") + to->symbol;
    return c;
}

} // namespace m3::units
