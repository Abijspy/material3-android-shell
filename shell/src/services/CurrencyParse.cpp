#include "services/Currency.h"


#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <vector>

namespace m3::currency {

namespace {

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

// Names and symbols people type, to codes
const std::map<std::string, std::string>& aliases() {
    static const std::map<std::string, std::string> a = {
        {"$", "USD"},        {"dollar", "USD"},  {"dollars", "USD"},  {"bucks", "USD"},   {"€", "EUR"},       {"euro", "EUR"},
        {"euros", "EUR"},    {"£", "GBP"},       {"pound", "GBP"},    {"pounds", "GBP"},  {"quid", "GBP"},    {"₹", "INR"},
        {"rupee", "INR"},    {"rupees", "INR"},  {"rs", "INR"},       {"¥", "JPY"},       {"yen", "JPY"},     {"yuan", "CNY"},
        {"rmb", "CNY"},      {"won", "KRW"},     {"₩", "KRW"},        {"franc", "CHF"},   {"francs", "CHF"},  {"real", "BRL"},
        {"reais", "BRL"},    {"lira", "TRY"},    {"₺", "TRY"},        {"zloty", "PLN"},   {"baht", "THB"},    {"ringgit", "MYR"},
        {"peso", "MXN"},     {"pesos", "MXN"},   {"rand", "ZAR"},     {"shekel", "ILS"},  {"₪", "ILS"},       {"krona", "SEK"},
        {"krone", "NOK"},    {"forint", "HUF"},  {"koruna", "CZK"},   {"leu", "RON"},     {"rupiah", "IDR"},  {"₱", "PHP"},
    };
    return a;
}

// The codes the ECB publishes (and the euro itself)
bool known(const std::string& code) {
    static const std::vector<std::string> codes = {"EUR", "USD", "JPY", "BGN", "CZK", "DKK", "GBP", "HUF", "PLN", "RON", "SEK", "CHF",
                                                   "ISK", "NOK", "TRY", "AUD", "BRL", "CAD", "CNY", "HKD", "IDR", "ILS", "INR", "KRW",
                                                   "MXN", "MYR", "NZD", "PHP", "SGD", "THB", "ZAR"};
    return std::ranges::find(codes, code) != codes.end();
}

// A word as a currency code ("usd", "dollars", "€") or ""
std::string codeOf(const std::string& word) {
    const std::string w = lower(trim(word));
    if (w.empty()) return {};
    if (auto it = aliases().find(w); it != aliases().end()) return it->second;
    std::string up = w;
    std::ranges::transform(up, up.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return up.size() == 3 && known(up) ? up : "";
}

// "8,350.25": two decimals (none for the yen's and won's kind), thousands grouped
std::string grouped(double v, int decimals) {
    std::string s = std::format("{:.{}f}", std::abs(v), decimals);
    const auto dot = s.find('.');
    std::string whole = s.substr(0, dot), frac = dot == std::string::npos ? "" : s.substr(dot);
    std::string out;
    for (size_t i = 0; i < whole.size(); ++i) {
        if (i && (whole.size() - i) % 3 == 0) out += ',';
        out += whole[i];
    }
    return (v < 0 ? "-" : "") + out + frac;
}

struct Parsed {
    double value = 0;
    std::string from, to;
};

// The shape alone: a number with a currency before or after it, then maybe "to|in <currency>"
std::optional<Parsed> parse(const std::string& query) {
    std::string q = trim(query);
    Parsed p;
    // A symbol or code in front: "$100", "usd 100"
    size_t i = 0;
    while (i < q.size() && !std::isdigit(static_cast<unsigned char>(q[i])) && q[i] != '.' && q[i] != '-') ++i;
    std::string front = trim(q.substr(0, i));
    if (!front.empty()) {
        p.from = codeOf(front);
        if (p.from.empty()) return std::nullopt;
    }
    std::string rest = q.substr(i);
    // The number, with thousands commas allowed
    std::string num;
    size_t j = 0;
    while (j < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[j])) || rest[j] == '.' || rest[j] == ',' || (j == 0 && rest[j] == '-'))) {
        if (rest[j] != ',') num += rest[j];
        ++j;
    }
    if (num.empty() || num == "-" || num == ".") return std::nullopt;
    char* end = nullptr;
    p.value = std::strtod(num.c_str(), &end);
    if (!end || *end) return std::nullopt;
    rest = trim(rest.substr(j));
    // "<from> to|in|= <to>", "<from> <to>", "<from>", or (with a symbol in front) "to <to>"
    std::string fromWord = rest, toWord;
    for (const char* sep : {" to ", " in ", " = ", " -> ", " → "}) {
        const std::string spaced = " " + rest;
        if (const auto at = spaced.find(sep); at != std::string::npos) {
            fromWord = trim(spaced.substr(0, at));
            toWord = trim(spaced.substr(at + std::string(sep).size()));
            break;
        }
    }
    if (toWord.empty() && !fromWord.empty() && fromWord.find(' ') != std::string::npos) {
        const auto sp = fromWord.rfind(' ');
        const std::string a = codeOf(fromWord.substr(0, sp)), b = codeOf(fromWord.substr(sp + 1));
        if (!a.empty() && !b.empty()) {
            fromWord = fromWord.substr(0, sp);
            toWord = rest.substr(sp + 1);
        }
    }
    if (!fromWord.empty()) {
        const std::string c = codeOf(fromWord);
        if (c.empty() || (!p.from.empty() && c != p.from)) return std::nullopt;
        p.from = c;
    }
    if (p.from.empty()) return std::nullopt;
    if (!toWord.empty()) {
        p.to = codeOf(toWord);
        if (p.to.empty()) return std::nullopt;
    }
    return p;
}

} // namespace

bool looksLike(const std::string& query) {
    return parse(query).has_value();
}

std::optional<Conversion> convert(const std::string& query, const Rates& rates, const std::string& local) {
    auto p = parse(query);
    if (!p) return std::nullopt;
    if (p->to.empty()) p->to = local;
    if (p->to == p->from) return std::nullopt;
    auto rate = [&rates](const std::string& c) -> double {
        if (c == "EUR") return 1;
        auto it = rates.perEuro.find(c);
        return it == rates.perEuro.end() ? 0 : it->second;
    };
    const double a = rate(p->from), b = rate(p->to);
    if (a <= 0 || b <= 0) return std::nullopt;
    Conversion c;
    c.value = p->value;
    c.result = p->value / a * b;
    c.from = p->from;
    c.to = p->to;
    const int decimals = (c.to == "JPY" || c.to == "KRW" || c.to == "IDR" || c.to == "HUF" || c.to == "ISK") ? 0 : 2;
    c.text = grouped(c.result, decimals) + " " + c.to;
    return c;
}

std::string forZone(const std::string& zone) {
    static const std::map<std::string, std::string> exact = {
        {"Asia/Kolkata", "INR"},    {"Asia/Calcutta", "INR"},     {"Europe/London", "GBP"},  {"Asia/Tokyo", "JPY"},
        {"Asia/Shanghai", "CNY"},   {"Asia/Seoul", "KRW"},        {"Asia/Singapore", "SGD"}, {"Asia/Hong_Kong", "HKD"},
        {"Asia/Jakarta", "IDR"},    {"Asia/Kuala_Lumpur", "MYR"}, {"Asia/Manila", "PHP"},    {"Asia/Bangkok", "THB"},
        {"Asia/Jerusalem", "ILS"},  {"Europe/Zurich", "CHF"},     {"Europe/Stockholm", "SEK"}, {"Europe/Oslo", "NOK"},
        {"Europe/Copenhagen", "DKK"}, {"Europe/Warsaw", "PLN"},   {"Europe/Prague", "CZK"},  {"Europe/Budapest", "HUF"},
        {"Europe/Bucharest", "RON"}, {"Europe/Sofia", "BGN"},     {"Europe/Istanbul", "TRY"}, {"Atlantic/Reykjavik", "ISK"},
        {"America/Sao_Paulo", "BRL"}, {"America/Mexico_City", "MXN"}, {"America/Toronto", "CAD"}, {"America/Vancouver", "CAD"},
        {"Africa/Johannesburg", "ZAR"}, {"Pacific/Auckland", "NZD"},
    };
    if (auto it = exact.find(zone); it != exact.end()) return it->second;
    if (zone.starts_with("Australia/")) return "AUD";
    if (zone.starts_with("Europe/")) return "EUR";
    return "USD";
}

} // namespace m3::currency
