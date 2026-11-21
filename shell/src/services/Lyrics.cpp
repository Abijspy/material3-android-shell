#include "services/Lyrics.h"

#include "build_config.h"
#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Audio.h"
#include "services/Mpris.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

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
// empty file: none anywhere, asked again after a few days. Plain lyrics
// carry a first line saying so (and whose they are), and the track's length
// rides along as LRC's own [length:] tag
std::string cacheFile(const std::string& key) {
    return (fs::path(paths::home()) / ".cache/material3ui/lyrics" / std::format("{:016x}.lrc", std::hash<std::string>{}(key))).string();
}
constexpr auto kRetryNone = std::chrono::hours(72);
constexpr std::string_view kPlainTag = "[m3:plain]\n", kShazamTag = "[m3:shazam]\n";

void keep(const std::string& key, const std::string& text) {
    std::error_code ec;
    const std::string file = cacheFile(key);
    fs::create_directories(fs::path(file).parent_path(), ec);
    std::ofstream(file) << text;
}

std::string playerKey(const Mpris::Player* p) {
    if (!p || p->title.empty()) return {};
    // Who and what, and its length (another recording, other times)
    return std::format("{}\x1f{}\x1f{:.0f}", p->artist, p->title, p->length > 0 ? static_cast<double>(p->length) / 1e6 : 0.0);
}

const char* kAgent = "Material3UI/" M3_VERSION " (https://github.com/Abijspy/material3-android-shell)";
constexpr int kListenSeconds = 10, kRate = 16000;

// Raw 16-bit mono PCM as a WAV file, for songrec
bool writeWav(const std::string& file, const std::string& pcm) {
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto size = static_cast<uint32_t>(pcm.size() & ~size_t(1));
    out.write("RIFF", 4), u32(36 + size), out.write("WAVEfmt ", 8), u32(16), u16(1), u16(1), u32(kRate), u32(kRate * 2), u16(2), u16(16);
    out.write("data", 4), u32(size), out.write(pcm.data(), size);
    return static_cast<bool>(out);
}

// How loud the sound was, 0–32767: near silence isn't worth asking about
double loudness(const std::string& pcm) {
    const size_t n = pcm.size() / 2;
    if (n == 0) return 0;
    double sum = 0;
    for (size_t i = 0; i < n; ++i) {
        int16_t s;
        std::memcpy(&s, pcm.data() + i * 2, 2);
        sum += static_cast<double>(s) * s;
    }
    return std::sqrt(sum / static_cast<double>(n));
}

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
        m_heard = false;
        m_heardFor.clear();
        trackChanged();
    });
    // A song with no player saying what it is: looked at as the sound comes
    // and goes, and now and then while it plays
    Audio::get().changed.connectForever([this] { considerListening(); });
    m_timer = EventLoop::get().addTimer(15000, [this] { considerListening(); }, true);
    trackChanged();
}

bool Lyrics::enabled() const {
    return Config::get().value<bool>("/media/lyrics", true);
}

bool Lyrics::recognising() const {
    return enabled() && Config::get().value<bool>("/media/recognize", true) && haveCommand("songrec") && haveCommand("parec");
}

void Lyrics::setState(State s) {
    m_state = s;
}

long long Lyrics::position() const {
    // A heard song: Shazam's clock (a video's position isn't the song's:
    // it may open with a minute of something else), stopped while it's quiet
    if (m_heard) {
        const auto upTo = m_quiet ? m_quietSince : Clock::now();
        return std::llround(m_heardOffset * 1e6) + std::chrono::duration_cast<std::chrono::microseconds>(upTo - m_heardAt).count();
    }
    const auto* p = Mpris::get().active();
    return p ? p->positionNow() : 0;
}

bool Lyrics::playing() const {
    const auto* p = Mpris::get().active();
    if (m_heard) return !m_quiet && (m_heardFor.empty() || (p && p->status == "Playing"));
    return p && p->status == "Playing";
}

void Lyrics::trackChanged() {
    const auto* p = Mpris::get().active();
    const std::string key = playerKey(p);
    // A heard song stands in for its player's track (or for none) until a
    // player plays something else
    if (m_heard) {
        const bool replaced = key.empty() ? !m_heardFor.empty() : key != m_heardFor && p->status == "Playing";
        if (!replaced) return;
        m_heard = false;
        m_heardFor.clear();
        m_heardLyrics.clear();
        m_key.clear();
    }
    if (key == m_key) return;
    show(key, p ? p->artist : "", p ? p->title : "", p && p->length > 0 ? static_cast<double>(p->length) / 1e6 : 0);
}

void Lyrics::show(const std::string& key, const std::string& artist, const std::string& title, double seconds) {
    m_key = key;
    m_title = title;
    m_artist = artist;
    m_seconds = seconds;
    m_lines.clear();
    m_synced = true;
    m_source.clear();
    if (key.empty() || !enabled()) {
        setState(State::Idle);
        changed.emit();
        return;
    }
    if (const auto it = m_known.find(key); it != m_known.end()) {
        m_lines = it->second.lines;
        m_synced = it->second.synced;
        m_source = it->second.source;
        if (m_seconds <= 0) m_seconds = it->second.seconds;
        if (m_lines.empty()) return nothing(key);
        setState(State::Found);
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
            return found(key, ss.str(), seconds, "LRCLIB");
        }
    }
    setState(State::Searching);
    changed.emit();
    lookup(key, artist, title, seconds);
}

// LRCLIB, asked in turn until one has synced lyrics (plain ones kept from
// the first that had them): the track exactly, then by title and artist,
// then with what uploads add taken out (the channel's "- Topic", "Song |
// Movie", "【MV】…"), by title alone (when the length can tell recordings
// apart: channel names are often a label's, not the singer's), and last as
// free text, which finds a song titled in another script or language
void Lyrics::lookup(const std::string& key, const std::string& artist, const std::string& title, double seconds) {
    const std::string api = "https://lrclib.net/api/", t = cleanTitle(title), a = cleanArtist(artist);
    std::vector<std::string> urls;
    auto add = [&](const std::string& u) {
        if (std::ranges::find(urls, u) == urls.end()) urls.push_back(u);
    };
    if (!artist.empty() && seconds > 0)
        add(std::format("{}get?track_name={}&artist_name={}&duration={:.0f}", api, urlEncode(title), urlEncode(artist), seconds));
    if (!artist.empty()) add(api + "search?track_name=" + urlEncode(title) + "&artist_name=" + urlEncode(artist));
    if (!a.empty()) add(api + "search?track_name=" + urlEncode(t) + "&artist_name=" + urlEncode(a));
    if (seconds > 0 || artist.empty()) add(api + "search?track_name=" + urlEncode(t));
    add(api + "search?q=" + urlEncode(a.empty() ? t : t + " " + a));
    step(key, std::move(urls), seconds, {});
}

void Lyrics::step(const std::string& key, std::vector<std::string> urls, double seconds, Found best) {
    if (urls.empty() || !best.synced.empty()) {
        std::string text;
        if (best.duration > 0) text = std::format("[length:{:.0f}]\n", best.duration);
        if (!best.synced.empty()) text += best.synced;
        else if (!best.plain.empty()) text += std::string(kPlainTag) + best.plain;
        else text.clear(); // none: the empty file
        keep(key, text);
        return found(key, text, seconds, "LRCLIB");
    }
    // The track moved on: what's found so far waits for another time
    if (key != m_key) return;
    const std::string url = urls.front();
    urls.erase(urls.begin());
    run({"curl", "-fsSL", "--max-time", "12", "-A", kAgent, url}, [this, key, urls, seconds, best](int code, const std::string& out) mutable {
        if (code == 22) return step(key, std::move(urls), seconds, best); // not there (404): the next way
        if (code != 0) {
            log::warn("lyrics: LRCLIB didn't answer ({})", code); // asked again next time
            if (key == m_key) {
                setState(State::None);
                changed.emit();
            }
            return;
        }
        Found f = pick(out, seconds);
        if (best.plain.empty() && !f.plain.empty()) {
            best.plain = f.plain;
            best.duration = f.duration;
        }
        if (!f.synced.empty()) {
            best.synced = f.synced;
            best.duration = f.duration;
        }
        step(key, std::move(urls), seconds, best);
    });
}

void Lyrics::found(const std::string& key, const std::string& cachedText, double seconds, const std::string& source) {
    std::string text = cachedText, src = source;
    // [length:SSS], kept with lyrics found for a track whose length wasn't known
    if (text.starts_with("[length:")) {
        const size_t end = text.find('\n');
        try {
            if (seconds <= 0) seconds = std::stod(text.substr(8, text.find(']') - 8));
        } catch (...) {
        }
        text = end == std::string::npos ? "" : text.substr(end + 1);
    }
    Known k{{}, true, src, seconds};
    if (text.starts_with(kPlainTag) || text.starts_with(kShazamTag)) {
        if (text.starts_with(kShazamTag)) k.source = "Shazam";
        k.lines = estimate(text.substr(text.find('\n') + 1), seconds);
        k.synced = false;
    } else {
        k.lines = parseLrc(text);
    }
    m_known[key] = k;
    if (key != m_key) return; // the track moved on meanwhile
    m_lines = k.lines;
    m_synced = k.synced;
    m_source = m_lines.empty() ? "" : k.source;
    if (m_seconds <= 0) m_seconds = seconds;
    if (m_lines.empty()) return nothing(key);
    setState(State::Found);
    changed.emit();
}

// No lyrics anywhere for the track: what Shazam had for a song it heard;
// else, for a player's track, listen to it once, since what the player
// calls it may not be the song's name
void Lyrics::nothing(const std::string& key) {
    if (key != m_key) return;
    if (m_heard && !m_heardLyrics.empty()) {
        const std::string text = std::string(kShazamTag) + m_heardLyrics;
        m_heardLyrics.clear();
        keep(key, text);
        return found(key, text, m_seconds, "Shazam");
    }
    setState(State::None);
    changed.emit();
    const auto* p = Mpris::get().active();
    if (!m_heard && p && p->status == "Playing" && playerKey(p) == key && !m_listenedFor.contains(key) && recognising()) {
        m_listenedFor.insert(key);
        listen(false);
    }
}

void Lyrics::considerListening() {
    if (!enabled()) return;
    const auto* p = Mpris::get().active();
    const bool playerPlaying = p && p->status == "Playing";
    bool sound = false;
    for (const auto& s : Audio::get().playing())
        if (!s.paused && s.binary != "material3ui") sound = true;
    const auto now = Clock::now();
    // Quiet: a heard song's clock stops, and after a while it's over
    if (!sound) {
        if (!m_quiet) m_quietSince = now;
        m_quiet = true;
        m_misses = 0;
        if (m_heard && m_heardFor.empty() && now - m_quietSince > std::chrono::seconds(30)) {
            m_heard = false;
            m_key.clear();
            trackChanged();
        }
        return;
    }
    if (m_quiet && m_heard) m_heardAt += now - m_quietSince;
    m_quiet = false;
    // Listening on its own: only a song no player is telling about, never
    // during a call (the microphone in use)
    if (playerPlaying || m_listening || !recognising() || !Audio::get().recording().empty()) return;
    // A heard song still playing: listened to again once it should be over
    if (m_heard && m_heardFor.empty() && position() / 1e6 < (m_seconds > 0 ? m_seconds : 240) + 3) return;
    // Not a song (speech, a film): asked less often, then not until the sound stops
    if (m_misses >= 3 || now - m_lastListen < std::chrono::seconds(m_misses ? 180 : 60)) return;
    listen(false);
}

void Lyrics::identify() {
    listen(true);
}

// Ten seconds of what the default output plays (its monitor: never the
// microphone), as raw PCM, then songrec: a fingerprint of it to Shazam
void Lyrics::listen(bool byHand) {
    if (m_listening || !enabled()) return;
    if (!byHand && !recognising()) return;
    if (!haveCommand("songrec") || !haveCommand("parec")) {
        log::warn("lyrics: recognising songs needs songrec and parec (libpulse)");
        return;
    }
    m_listening = true;
    m_lastListen = Clock::now();
    const State before = m_state;
    setState(State::Listening);
    changed.emit();
    const auto began = Clock::now();
    auto giveUp = [this, before](bool miss) {
        m_listening = false;
        if (miss) ++m_misses;
        if (m_state == State::Listening) setState(before == State::Listening ? State::None : before);
        changed.emit();
    };
    run({"timeout", std::to_string(kListenSeconds), "parec", "--device=@DEFAULT_MONITOR@", "--channels=1", "--rate=" + std::to_string(kRate),
         "--format=s16le", "--raw", "--latency-msec=200"},
        [this, began, byHand, giveUp](int, const std::string& pcm) {
            // Silence (paused, muted): nothing to ask about
            if (pcm.size() < kRate * 2 * 3 || loudness(pcm) < 150) {
                log::info("lyrics: too quiet to recognise");
                return giveUp(false);
            }
            const std::string file = paths::runtimeDir() + "/listen.wav";
            std::error_code ec;
            fs::create_directories(paths::runtimeDir(), ec);
            if (!writeWav(file, pcm)) return giveUp(false);
            run({"timeout", "30", "songrec", "audio-file-to-recognized-song", file}, [this, file, began, byHand, giveUp](int code, const std::string& out) {
                std::error_code ec2;
                fs::remove(file, ec2);
                Heard h;
                if (code != 0 || !parseShazam(out, h)) {
                    log::info("lyrics: Shazam didn't know the song{}", code == 0 ? "" : std::format(" (songrec {})", code));
                    return giveUp(true);
                }
                m_listening = false;
                m_misses = 0;
                recognised(h, began, byHand);
            });
        });
}

void Lyrics::recognised(const Heard& h, Clock::time_point began, bool byHand) {
    const auto* p = Mpris::get().active();
    const bool playerPlaying = p && p->status == "Playing" && !p->title.empty();
    log::info("lyrics: heard \"{}\" by {}, {:.0f} s in{}", h.title, h.artist, h.offset, byHand ? " (asked)" : "");
    m_heard = true;
    m_heardFor = playerPlaying ? playerKey(p) : "";
    m_heardLyrics = h.lyrics;
    m_heardOffset = h.offset;
    m_heardAt = began;
    m_quiet = false;
    // No length to go by: the player's may be a video's, longer than the song
    m_key.clear();
    show(std::format("heard\x1f{}\x1f{}", h.artist, h.title), h.artist, h.title, 0);
}

} // namespace m3
