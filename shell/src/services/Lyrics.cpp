#include "services/Lyrics.h"

#include "build_config.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Mpris.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

// Where each track's lyrics are kept: its own file, named for the track. An
// empty file: LRCLIB had none, asked again after a few days
std::string cacheFile(const std::string& key) {
    return (fs::path(paths::home()) / ".cache/material3ui/lyrics" / std::format("{:016x}.lrc", std::hash<std::string>{}(key))).string();
}

constexpr auto kRetryNone = std::chrono::hours(72);

} // namespace

Lyrics& Lyrics::get() {
    static Lyrics instance;
    return instance;
}

Lyrics::Lyrics() {
    Mpris::get().changed.connectForever([this] { trackChanged(); });
    // Switched in Settings: the track playing now, or nothing
    m_on = enabled();
    Config::get().changed.connectForever([this] {
        if (enabled() == m_on) return;
        m_on = enabled();
        m_key.clear();
        trackChanged();
    });
    trackChanged();
}

bool Lyrics::enabled() const {
    return Config::get().value<bool>("/media/lyrics", true);
}

void Lyrics::trackChanged() {
    const auto* p = Mpris::get().active();
    const std::string artist = p ? p->artist : "", title = p ? p->title : "";
    const double seconds = p && p->length > 0 ? static_cast<double>(p->length) / 1e6 : 0;
    // The track: who and what, and its length (another recording, other times)
    const std::string key = title.empty() ? "" : std::format("{}\x1f{}\x1f{:.0f}", artist, title, seconds);
    if (key == m_key) return;
    m_key = key;
    m_lines.clear();
    changed.emit();
    if (key.empty() || !enabled()) return;
    if (const auto it = m_known.find(key); it != m_known.end()) {
        m_lines = it->second;
        changed.emit();
        return;
    }
    // Kept from before: found, or not found lately
    std::error_code ec;
    const std::string file = cacheFile(key);
    if (fs::exists(file, ec)) {
        const bool none = fs::file_size(file, ec) == 0;
        const auto age = fs::file_time_type::clock::now() - fs::last_write_time(file, ec);
        if (!none || age < kRetryNone) {
            std::stringstream ss;
            ss << std::ifstream(file).rdbuf();
            return found(key, ss.str());
        }
    }
    search(key, artist, title, seconds, true);
}

void Lyrics::search(const std::string& key, const std::string& artist, const std::string& title, double seconds, bool retry) {
    std::string url = "https://lrclib.net/api/search?track_name=" + urlEncode(title);
    if (!artist.empty()) url += "&artist_name=" + urlEncode(artist);
    run({"curl", "-fsSL", "--max-time", "12", "-A", "Material3UI/" M3_VERSION " (https://github.com/Abijspy/material3-android-shell)", url},
        [this, key, artist, title, seconds, retry](int code, const std::string& out) {
            if (code != 0) return log::warn("lyrics: LRCLIB didn't answer ({})", code); // asked again next time
            std::string lrc = pick(out, seconds);
            // Not found as the player titles it: without what videos add
            if (lrc.empty() && retry && cleanTitle(title) != title) return search(key, artist, cleanTitle(title), seconds, false);
            std::error_code ec;
            const std::string file = cacheFile(key);
            fs::create_directories(fs::path(file).parent_path(), ec);
            std::ofstream(file) << lrc;
            found(key, lrc);
        });
}

void Lyrics::found(const std::string& key, const std::string& lrc) {
    auto lines = parseLrc(lrc);
    m_known[key] = lines;
    if (key != m_key) return; // the track moved on meanwhile
    m_lines = std::move(lines);
    changed.emit();
}

} // namespace m3
