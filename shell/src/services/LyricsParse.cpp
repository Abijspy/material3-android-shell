// Reading LRC, LRCLIB's and Shazam's answers, and players' titles, apart
// from the rest so the unit test can link it alone (no event loop, no
// processes). Text is UTF-8 in any script: only ASCII is ever case-folded,
// and a cut is made at a separator or bracket, never mid-character.
#include "services/Lyrics.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace m3 {

namespace {

bool space(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string trimmed(std::string s) {
    while (!s.empty() && space(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && space(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s)
        if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Runs of spaces as one
std::string squeezed(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (space(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
    return trimmed(out);
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

// How long a line takes to sing, roughly: its characters, a CJK one (a
// syllable to itself) as three Latin letters
double singingWeight(const std::string& text) {
    double w = 0;
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        size_t n = c < 0x80 ? 1 : c >> 5 == 0x6 ? 2 : c >> 4 == 0xE ? 3 : c >> 3 == 0x1E ? 4 : 1;
        if (i + n > text.size()) n = 1;
        unsigned cp = c;
        if (n == 3) cp = (c & 0x0F) << 12 | (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6 | (static_cast<unsigned char>(text[i + 2]) & 0x3F);
        const bool wide = n == 3 && cp >= 0x2E80 && cp <= 0xD7FF; // CJK, kana, Hangul
        if (!(n == 1 && space(c))) w += wide ? 3 : 1;
        i += n;
    }
    return w;
}

// What's in a bracket that says what the upload is, not what the song is
bool noise(const std::string& insideRaw) {
    const std::string inside = lower(trimmed(insideRaw));
    if (inside.empty()) return true;
    if (inside.starts_with("from ") || inside.starts_with("from\"") || inside.find("m/v") != std::string::npos) return true;
    static const std::set<std::string> words = {
        "official", "video", "lyric", "lyrics", "lyrical", "audio", "visualizer", "visualiser", "hd", "hq", "4k", "8k",
        "remaster", "remastered", "mv", "pv", "live", "teaser", "trailer", "explicit", "sub", "subs", "subtitles",
        "subtitulado", "legendado", "karaoke", "romanized", "romanization", "translation", "eng", "english",
    };
    // Its words, split at anything that isn't an ASCII letter or digit
    std::string word;
    for (size_t i = 0; i <= inside.size(); ++i) {
        const auto c = i < inside.size() ? static_cast<unsigned char>(inside[i]) : ' ';
        if (c < 0x80 && std::isalnum(c)) {
            word += static_cast<char>(c);
            continue;
        }
        if (!word.empty() && words.contains(word)) return true;
        word.clear();
    }
    return false;
}

// Brackets by their UTF-8 spelling: ASCII, fullwidth, and the CJK ones
struct Pair {
    const char *open, *close;
};
constexpr Pair kBrackets[] = {{"(", ")"}, {"[", "]"}, {"（", "）"}, {"［", "］"}, {"【", "】"}, {"〔", "〕"}};
constexpr Pair kQuotes[] = {{"「", "」"}, {"『", "』"}};

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

Lyrics::Found Lyrics::pick(const std::string& results, double seconds) {
    auto j = nlohmann::json::parse(results, nullptr, false);
    if (j.is_object()) j = nlohmann::json::array({j}); // /api/get: the one track
    if (!j.is_array()) return {};
    Found best;
    double syncedGap = 1e9, plainGap = 1e9;
    auto text = [](const nlohmann::json& r, const char* key) {
        return r.contains(key) && r[key].is_string() ? trimmed(r[key].get<std::string>()) : std::string();
    };
    for (const auto& r : j) {
        if (!r.is_object()) continue;
        // Without a length to go by, the first (LRCLIB's best match)
        const double length = r.contains("duration") && r["duration"].is_number() ? r["duration"].get<double>() : 0.0;
        const double gap = seconds > 0 && length > 0 ? std::abs(length - seconds) : 0;
        if (seconds > 0 && length > 0 && gap > 4) continue; // another recording
        if (const auto synced = text(r, "syncedLyrics"); !synced.empty() && gap < syncedGap) {
            best.synced = synced;
            best.duration = length;
            syncedGap = gap;
        }
        if (const auto plain = text(r, "plainLyrics"); !plain.empty() && gap < plainGap) {
            best.plain = plain;
            if (best.synced.empty()) best.duration = length;
            plainGap = gap;
        }
    }
    return best;
}

std::vector<Lyrics::Line> Lyrics::estimate(const std::string& plain, double seconds) {
    std::vector<std::string> texts;
    std::stringstream in(plain);
    for (std::string line; std::getline(in, line);) {
        line = trimmed(line);
        // One break between verses, none before the first or after the last
        if (line.empty() && (texts.empty() || texts.back().empty())) continue;
        texts.push_back(line);
    }
    while (!texts.empty() && texts.back().empty()) texts.pop_back();
    if (texts.empty()) return {};
    if (seconds <= 0) seconds = 210; // a song's usual length
    // The intro and the end, left without lines
    const double start = std::min(seconds * 0.08, 20.0), end = seconds - std::min(seconds * 0.06, 15.0);
    std::vector<double> weights;
    double total = 0;
    for (const auto& t : texts) {
        weights.push_back(t.empty() ? 10.0 : std::max(singingWeight(t), 8.0) + 6.0);
        total += weights.back();
    }
    std::vector<Line> out;
    double at = 0;
    for (size_t i = 0; i < texts.size(); ++i) {
        out.push_back({std::llround((start + at / total * (end - start)) * 1e6), texts[i]});
        at += weights[i];
    }
    return out;
}

std::string Lyrics::cleanTitle(const std::string& title) {
    std::string t = title;
    // 「Song」, 『Song』: Japanese uploads quote the title itself
    for (const auto& q : kQuotes) {
        const size_t a = t.find(q.open);
        if (a == std::string::npos) continue;
        const size_t from = a + std::string(q.open).size(), b = t.find(q.close, from);
        if (b != std::string::npos && b > from) return squeezed(t.substr(from, b - from));
    }
    // Brackets that say what the upload is, not what the song is
    for (const auto& br : kBrackets) {
        const std::string open = br.open, close = br.close;
        for (size_t a = t.find(open); a != std::string::npos; a = t.find(open, a)) {
            const size_t b = t.find(close, a + open.size());
            if (b == std::string::npos) break;
            // 【…】 never names the song ("【MV】", "【Official】")
            if (open == "【" || noise(t.substr(a + open.size(), b - a - open.size()))) t.erase(a, b + close.size() - a);
            else a = b + close.size();
        }
    }
    // "Song | Movie | Composer", as Indian and other uploads go: the first
    for (const char* bar : {"｜", "|", " // "})
        if (const size_t at = t.find(bar); at != std::string::npos && at > 0) t = t.substr(0, at);
    // "Artist - Song", as videos are often titled; "Song - Lyric Video": the song
    for (const char* dash : {" - ", " – ", " — ", " ‐ "}) {
        const size_t at = t.find(dash);
        if (at == std::string::npos) continue;
        const std::string before = t.substr(0, at), after = t.substr(at + std::string(dash).size());
        t = noise(after) ? before : after;
        break;
    }
    t = squeezed(t);
    return t.empty() ? trimmed(title) : t;
}

std::string Lyrics::cleanArtist(const std::string& artist) {
    std::string a = trimmed(artist);
    for (const char* tail : {" - Topic", "VEVO", "Vevo", " Official", " official"})
        if (a.size() > std::string(tail).size() + 1 && a.ends_with(tail)) a.erase(a.size() - std::string(tail).size());
    return trimmed(a);
}

bool Lyrics::parseShazam(const std::string& json, Heard& out) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object() || !j.contains("matches") || !j["matches"].is_array() || j["matches"].empty()) return false;
    if (!j.contains("track") || !j["track"].is_object()) return false;
    const auto& m = j["matches"][0];
    auto num = [](const nlohmann::json& o, const char* key) {
        return o.is_object() && o.contains(key) && o[key].is_number() ? o[key].get<double>() : 0.0;
    };
    // A real match plays at the song's own speed and pitch: a stray one
    // (a tone, noise) comes back stretched
    if (std::abs(num(m, "timeskew")) > 0.02 || std::abs(num(m, "frequencyskew")) > 0.02) return false;
    const auto& t = j["track"];
    auto str = [](const nlohmann::json& o, const char* key) {
        return o.contains(key) && o[key].is_string() ? trimmed(o[key].get<std::string>()) : std::string();
    };
    out = {};
    out.title = str(t, "title");
    out.artist = str(t, "subtitle");
    out.offset = std::max(0.0, num(m, "offset"));
    if (t.contains("sections") && t["sections"].is_array())
        for (const auto& s : t["sections"]) {
            if (!s.is_object() || str(s, "type") != "LYRICS" || !s.contains("text") || !s["text"].is_array()) continue;
            for (const auto& l : s["text"])
                if (l.is_string()) out.lyrics += l.get<std::string>() + "\n";
        }
    return !out.title.empty();
}

int Lyrics::lineIn(const std::vector<Line>& lines, long long us) {
    // The last line starting at or before `us`
    const auto it = std::ranges::upper_bound(lines, us, {}, &Line::at);
    return static_cast<int>(it - lines.begin()) - 1;
}

} // namespace m3
