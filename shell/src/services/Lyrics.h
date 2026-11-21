#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace m3 {

// Lyrics for what's playing, in any language, from LRCLIB (lrclib.net, a
// free and open lyrics library): looked up once a track, kept on disk after
// that. Synced lyrics where LRCLIB has them; else its plain ones, each line
// given a time spread over the track by how long it is.
//
// What's playing comes from the media player (MPRIS). When LRCLIB has
// nothing for that, or a song plays without any player saying what it is,
// the song is recognised from the sound: ten seconds of what the speakers
// play (never the microphone) go to songrec, which asks Shazam with a
// fingerprint of them. Shazam's answer names the song, says how far into it
// the sound was (a clock for the lines when no player gives one), and may
// carry lyrics of its own. Never while the microphone is in use (a call).
//
// Settings → Sound: "Synced lyrics" turns it all off (then nothing is
// sent), "Recognise songs" the listening.
class Lyrics {
public:
    struct Line {
        long long at = 0; // µs into the track
        std::string text; // "" for an instrumental break
        bool operator==(const Line&) const = default;
    };
    // What LRCLIB's answers hold for a track
    struct Found {
        std::string synced, plain;
        double duration = 0; // the match's length, seconds (0: not said)
    };
    // What Shazam (songrec) says the sound is
    struct Heard {
        std::string title, artist, lyrics; // lyrics: plain, one line each
        double offset = 0;                 // seconds into the song where the sound began
    };
    enum class State { Idle, Searching, Listening, Found, None };

    static Lyrics& get();

    bool enabled() const;
    bool recognising() const; // Settings → Sound → Recognise songs (and songrec there)

    // The track's lines, in order: empty while they're looked up, and for
    // tracks with no lyrics anywhere
    const std::vector<Line>& lines() const { return m_lines; }
    // False when the lines came without times (each one's is a guess)
    bool synced() const { return m_synced; }
    // The track the lines are for: the player's, or what was heard
    const std::string& title() const { return m_title; }
    const std::string& artist() const { return m_artist; }
    bool heard() const { return m_heard; } // recognised from the sound
    const std::string& source() const { return m_source; } // "LRCLIB", "Shazam" or ""
    State state() const { return m_state; }
    bool hasTrack() const { return !m_title.empty(); }

    // Where the track is now, µs: the player's position, or (for a song
    // heard with no player giving one) Shazam's, moved on since
    long long position() const;
    bool playing() const;
    // The line sung at `us` into the track (-1: before the first)
    int lineAt(long long us) const { return lineIn(m_lines, us); }

    // Listens now and recognises the song (Spotlight, the lyrics panel, IPC)
    void identify();

    Signal<> changed; // another track, its lines, or the state

    // Parsers, apart for the unit test
    // "[01:02.34] words" (a line may carry several times; [ar:…] tags skip)
    static std::vector<Line> parseLrc(const std::string& lrc);
    // From LRCLIB's answers (JSON: a search's array, or one track): the
    // best match's lyrics, the closest in length (within 4 s when `seconds`
    // is known), synced and plain
    static Found pick(const std::string& results, double seconds);
    // Plain lyrics, timed: the lines spread over the track (its first and
    // last stretches left for the intro and the end), each given time by
    // its length in characters, a blank line a short break
    static std::vector<Line> estimate(const std::string& plain, double seconds);
    // What players and uploads add to a title, in any language: "Song
    // (Official Video) [HD]", "Artist - Song", "Song | Movie | Composer",
    // "【MV】Artist「Song」"… → "Song"; unchanged when it's bare
    static std::string cleanTitle(const std::string& title);
    // "Artist - Topic", "ArtistVEVO" → "Artist"
    static std::string cleanArtist(const std::string& artist);
    // songrec's JSON → what was heard; false when it isn't a real match
    // (none, or one so stretched in time or pitch it's a stray hit)
    static bool parseShazam(const std::string& json, Heard& out);
    static int lineIn(const std::vector<Line>& lines, long long us);

private:
    Lyrics();
    void trackChanged();
    void show(const std::string& key, const std::string& artist, const std::string& title, double seconds);
    void lookup(const std::string& key, const std::string& artist, const std::string& title, double seconds);
    void step(const std::string& key, std::vector<std::string> urls, double seconds, Found found);
    void found(const std::string& key, const std::string& cached, double seconds, const std::string& source);
    void nothing(const std::string& key);
    void setState(State s);
    // Listening on its own: a song playing with no player saying what it is
    void considerListening();
    void listen(bool byHand);
    void recognised(const Heard& h, std::chrono::steady_clock::time_point began, bool byHand);

    std::string m_key; // the track whose lines these are
    std::string m_title, m_artist, m_source;
    double m_seconds = 0;
    bool m_on = true;  // enabled(), as last seen
    bool m_synced = true;
    State m_state = State::Idle;
    std::vector<Line> m_lines;
    struct Known {
        std::vector<Line> lines;
        bool synced = true;
        std::string source;
        double seconds = 0;
    };
    std::map<std::string, Known> m_known; // this session's tracks

    // The player track the heard one stands in for ("" with no player)
    bool m_heard = false;
    std::string m_heardFor;
    std::string m_heardLyrics; // Shazam's, for when LRCLIB has none
    double m_heardOffset = 0;  // seconds into the song at m_heardAt
    std::chrono::steady_clock::time_point m_heardAt;
    std::set<std::string> m_listenedFor; // player tracks already listened to
    bool m_listening = false;
    int m_misses = 0; // listened on its own and not recognised, since the sound began
    bool m_quiet = false;
    std::chrono::steady_clock::time_point m_lastListen, m_quietSince;
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3
