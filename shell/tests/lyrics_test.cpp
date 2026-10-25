// LRC as LRCLIB serves it, its search results, and the titles players give
#include "services/Lyrics.h"

#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    // Tags skip; a line may carry several times; an empty one is a break;
    // out-of-order times are sorted; CRLF ends are fine
    const auto lines = Lyrics::parseLrc("[ar:Someone]\r\n"
                                        "[ti:A Song]\n"
                                        "[00:12.50] First line\n"
                                        "[00:20.00][01:05.25] Chorus\n"
                                        "[00:30.10]\n"
                                        "[00:15.00] Second line\r\n"
                                        "not a lyric\n");
    expect(lines.size() == 5, "five lines");
    if (lines.size() == 5) {
        expect(lines[0].at == 12500000 && lines[0].text == "First line", "a line and its time");
        expect(lines[1].at == 15000000 && lines[1].text == "Second line", "sorted, CR dropped");
        expect(lines[2].text == "Chorus" && lines[4].text == "Chorus" && lines[4].at == 65250000, "a line at two times");
        expect(lines[3].text.empty() && lines[3].at == 30100000, "a break");
    }
    expect(Lyrics::parseLrc("").empty() && Lyrics::parseLrc("plain words only\n").empty(), "nothing synced");

    // The line at a time
    expect(Lyrics::lineIn(lines, 0) == -1, "before the first");
    expect(Lyrics::lineIn(lines, 12500000) == 0, "on a line's start");
    expect(Lyrics::lineIn(lines, 16000000) == 1, "between lines");
    expect(Lyrics::lineIn(lines, 999000000) == 4, "after the last");

    // LRCLIB's search: the closest length wins; far off is another recording
    const char* results = R"([
        {"trackName": "Song", "duration": 240.0, "syncedLyrics": "[00:01.00] live version"},
        {"trackName": "Song", "duration": 201.0, "syncedLyrics": "[00:01.00] album version"},
        {"trackName": "Song", "duration": 199.0, "syncedLyrics": null, "plainLyrics": "words"},
        {"trackName": "Song", "duration": 203.5, "syncedLyrics": "[00:01.00] close too"}
    ])";
    expect(Lyrics::pick(results, 200.0) == "[00:01.00] album version", "the closest length");
    expect(Lyrics::pick(results, 120.0).empty(), "none close enough");
    expect(Lyrics::pick(results, 0).find("live version") != std::string::npos, "no length: the first synced");
    expect(Lyrics::pick("[]", 200).empty() && Lyrics::pick("not json", 200).empty(), "nothing found");

    // Titles as players give them
    expect(Lyrics::cleanTitle("Blinding Lights (Official Video)") == "Blinding Lights", "(Official Video)");
    expect(Lyrics::cleanTitle("Song [HD] (Lyrics)") == "Song", "several brackets");
    expect(Lyrics::cleanTitle("Artist - Song (Official Audio)") == "Song", "Artist - Song");
    expect(Lyrics::cleanTitle("Song (feat. Someone)") == "Song (feat. Someone)", "a bracket that's part of it");
    expect(Lyrics::cleanTitle("Plain") == "Plain", "bare");
    if (failures == 0) std::puts("all passed");
    return failures == 0 ? 0 : 1;
}
