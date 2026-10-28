#include "core/Version.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace m3 {

namespace {

struct Parsed {
    std::vector<long> numbers; // 6.0.1
    std::string pre;           // "beta"; "" for a release
    long preNumber = 0;        // beta 2
};

Parsed parse(const std::string& v) {
    Parsed p;
    size_t i = v.starts_with('v') ? 1 : 0;
    // The numbers, dot by dot
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = n * 10 + (v[i++] - '0');
        p.numbers.push_back(n);
        if (i + 1 < v.size() && v[i] == '.' && std::isdigit(static_cast<unsigned char>(v[i + 1]))) ++i;
        else break;
    }
    // Then a pre-release: "-beta-2", "beta2", "_rc_1"
    while (i < v.size() && !std::isalnum(static_cast<unsigned char>(v[i]))) ++i;
    while (i < v.size() && std::isalpha(static_cast<unsigned char>(v[i])))
        p.pre += static_cast<char>(std::tolower(static_cast<unsigned char>(v[i++])));
    while (i < v.size() && !std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) p.preNumber = p.preNumber * 10 + (v[i++] - '0');
    return p;
}

} // namespace

bool newerVersion(const std::string& a, const std::string& b) {
    const Parsed x = parse(a), y = parse(b);
    const size_t n = std::max(x.numbers.size(), y.numbers.size());
    for (size_t k = 0; k < n; ++k) {
        const long p = k < x.numbers.size() ? x.numbers[k] : 0, q = k < y.numbers.size() ? y.numbers[k] : 0;
        if (p != q) return p > q;
    }
    // The same numbers: the release comes after its pre-releases
    if (x.pre.empty() != y.pre.empty()) return x.pre.empty();
    if (x.pre != y.pre) return x.pre > y.pre; // alpha < beta < rc
    return x.preNumber > y.preNumber;
}

} // namespace m3
