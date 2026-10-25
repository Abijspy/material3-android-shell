#pragma once

#include "core/Signal.h"

#include <map>
#include <string>
#include <vector>

namespace m3 {

// Synced lyrics for what's playing, from LRCLIB (lrclib.net, a free and
// open lyrics library): looked up once a track, kept on disk after that.
// Settings → Sound → Synced lyrics turns it off, and then nothing is sent.
class Lyrics {
public:
    struct Line {
        long long at = 0; // µs into the track
        std::string text; // "" for an instrumental break
        bool operator==(const Line&) const = default;
    };

    static Lyrics& get();

    bool enabled() const;
    // The playing track's lines, in order: empty while it's looked up, and
    // for tracks LRCLIB has no synced lyrics for
    const std::vector<Line>& lines() const { return m_lines; }
    // The line sung at `us` into the track (-1: before the first)
    int lineAt(long long us) const { return lineIn(m_lines, us); }

    Signal<> changed; // another track's lines (or none)

    // Parsers, apart for the unit test
    // "[01:02.34] words" (a line may carry several times; [ar:…] tags skip)
    static std::vector<Line> parseLrc(const std::string& lrc);
    // From LRCLIB's search results (JSON): the best match's synced lyrics,
    // the closest in length (within 4 s when `seconds` is known); "" if none
    static std::string pick(const std::string& results, double seconds);
    // What players add to a title: "Song (Official Video) [HD]" → "Song",
    // "Artist - Song" → "Song" (videos titled so); unchanged when it's bare
    static std::string cleanTitle(const std::string& title);
    static int lineIn(const std::vector<Line>& lines, long long us);

private:
    Lyrics();
    void trackChanged();
    void search(const std::string& key, const std::string& artist, const std::string& title, double seconds, bool retry);
    void found(const std::string& key, const std::string& lrc);

    std::string m_key; // the track whose lines these are
    bool m_on = true;  // enabled(), as last seen
    std::vector<Line> m_lines;
    std::map<std::string, std::vector<Line>> m_known; // this session's tracks
};

} // namespace m3
