// Reading LRC and LRCLIB's answers, apart from the rest so the unit test can
// link it alone (no event loop, no processes)
#include "services/Lyrics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace m3 {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "[mm:ss.xx]" at the start of `s` → µs, and its length; -1 if it isn't one
long long stamp(const std::string& s, size_t& length) {
    if (s.size() < 7 || s[0] != '[') return -1;
    const size_t close = s.find(']');
    if (close == std::string::npos) return -1;
    const std::string inner = s.substr(1, close - 1);
    const size_t colon = inner.find(':');
    if (colon == std::string::npos || colon == 0) return -1;
    const std::string mm = inner.substr(0, colon), ss = inner.substr(colon + 1);
    if (!std::ranges::all_of(mm, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) return -1;
    if (ss.empty() || !std::ranges::all_of(ss, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; })) return -1;
    length = close + 1;
    return std::llround((std::stod(mm) * 60 + std::stod(ss)) * 1e6);
}

} // namespace

std::vector<Lyrics::Line> Lyrics::parseLrc(const std::string& lrc) {
    std::vector<Line> out;
    std::stringstream in(lrc);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<long long> times;
        size_t at = 0, length = 0;
        // Every time the line carries, then its words
        for (long long t; (t = stamp(line.substr(at), length)) >= 0; at += length) times.push_back(t);
        if (times.empty()) continue; // [ar:…] and the like
        const std::string text = trimmed(line.substr(at));
        for (long long t : times) out.push_back({t, text});
    }
    std::ranges::stable_sort(out, {}, &Line::at);
    return out;
}

std::string Lyrics::pick(const std::string& results, double seconds) {
    const auto j = nlohmann::json::parse(results, nullptr, false);
    if (!j.is_array()) return {};
    std::string best;
    double bestGap = 1e9;
    for (const auto& r : j) {
        if (!r.is_object() || !r.contains("syncedLyrics") || !r["syncedLyrics"].is_string()) continue;
        const std::string lrc = r["syncedLyrics"].get<std::string>();
        if (lrc.empty()) continue;
        // Without a length to go by, the first (LRCLIB's best match)
        const double length = r.value("duration", 0.0);
        const double gap = seconds > 0 && length > 0 ? std::abs(length - seconds) : 0;
        if (seconds > 0 && length > 0 && gap > 4) continue; // another recording
        if (gap < bestGap) {
            best = lrc;
            bestGap = gap;
        }
    }
    return best;
}

std::string Lyrics::cleanTitle(const std::string& title) {
    std::string t = title;
    // Brackets that say what the upload is, not what the song is
    static const char* noise[] = {"official", "video", "lyric", "audio", "visualizer", "visualiser", "hd", "4k", "remaster", "mv", "live"};
    for (const auto& [open, close] : {std::pair{'(', ')'}, {'[', ']'}}) {
        for (size_t a = t.find(open); a != std::string::npos; a = t.find(open, a)) {
            const size_t b = t.find(close, a);
            if (b == std::string::npos) break;
            const std::string inside = lower(t.substr(a + 1, b - a - 1));
            if (std::ranges::any_of(noise, [&](const char* n) { return inside.find(n) != std::string::npos; })) t.erase(a, b - a + 1);
            else a = b + 1;
        }
    }
    // "Artist - Song", as videos are often titled
    if (const size_t dash = t.find(" - "); dash != std::string::npos) t = t.substr(dash + 3);
    t = trimmed(t);
    return t.empty() ? trimmed(title) : t;
}

int Lyrics::lineIn(const std::vector<Line>& lines, long long us) {
    // The last line starting at or before `us`
    const auto it = std::ranges::upper_bound(lines, us, {}, &Line::at);
    return static_cast<int>(it - lines.begin()) - 1;
}

} // namespace m3
