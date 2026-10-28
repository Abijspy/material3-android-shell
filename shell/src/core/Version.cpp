#include "core/Version.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace m3 {

namespace {

struct Parsed {
    std::vector<long> numbers; // 6.0.1
    std::string pre;              // "beta"; "" for a release
    std::vector<long> preNumbers; // beta 2, beta 3.1
};

// a < b, dot by dot, missing parts 0
int compare(const std::vector<long>& a, const std::vector<long>& b) {
    for (size_t k = 0; k < std::max(a.size(), b.size()); ++k) {
        const long p = k < a.size() ? a[k] : 0, q = k < b.size() ? b[k] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

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
    // Its number, dot by dot ("beta-3.1"); a build between releases
    // ("6.0beta3.r2.gabc") stops at the .r
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = n * 10 + (v[i++] - '0');
        p.preNumbers.push_back(n);
        if (i + 1 < v.size() && v[i] == '.' && std::isdigit(static_cast<unsigned char>(v[i + 1]))) ++i;
        else break;
    }
    return p;
}

} // namespace

bool newerVersion(const std::string& a, const std::string& b) {
    const Parsed x = parse(a), y = parse(b);
    if (const int c = compare(x.numbers, y.numbers); c != 0) return c > 0;
    // The same numbers: the release comes after its pre-releases
    if (x.pre.empty() != y.pre.empty()) return x.pre.empty();
    if (x.pre != y.pre) return x.pre > y.pre; // alpha < beta < rc
    return compare(x.preNumbers, y.preNumbers) > 0;
}

} // namespace m3
