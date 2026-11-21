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
    expect(Lyrics::pick(results, 200.0).synced == "[00:01.00] album version", "the closest length");
    expect(Lyrics::pick(results, 200.0).duration == 201.0, "its length");
    expect(Lyrics::pick(results, 120.0).synced.empty(), "none close enough");
    expect(Lyrics::pick(results, 0).synced.find("live version") != std::string::npos, "no length: the first synced");
    expect(Lyrics::pick(results, 199.0).plain == "words", "plain lyrics kept too");
    expect(Lyrics::pick("[]", 200).synced.empty() && Lyrics::pick("not json", 200).plain.empty(), "nothing found");
    // /api/get answers with the one track; plain only (as many songs in other languages)
    const auto one = Lyrics::pick(R"({"trackName": "பாடல்", "duration": 250, "syncedLyrics": null, "plainLyrics": "முதல் வரி\nஇரண்டாம் வரி"})", 251);
    expect(one.synced.empty() && one.plain == "முதல் வரி\nஇரண்டாம் வரி", "one track, plain, Tamil");

    // Plain lyrics timed: in order, inside the track, longer lines longer
    const auto est = Lyrics::estimate("\n\nA short one\nA much much longer line than the first one\n\n\nNext verse\n\n", 200);
    expect(est.size() == 4, "lines and one break, no leading or trailing breaks");
    if (est.size() == 4) {
        expect(est[0].at >= 10000000 && est.back().at < 190000000, "inside the track, intro left");
        expect(est[2].text.empty() && est[3].text == "Next verse", "a break between verses");
        expect(est[2].at - est[1].at > est[1].at - est[0].at, "a longer line takes longer");
    }
    expect(Lyrics::estimate("", 200).empty(), "no lyrics");
    const auto cjk = Lyrics::estimate("夜に駆ける\nab", 100);
    expect(cjk.size() == 2 && cjk[0].text == "夜に駆ける", "CJK lines kept whole");

    // Titles as players give them
    expect(Lyrics::cleanTitle("Blinding Lights (Official Video)") == "Blinding Lights", "(Official Video)");
    expect(Lyrics::cleanTitle("Song [HD] (Lyrics)") == "Song", "several brackets");
    expect(Lyrics::cleanTitle("Artist - Song (Official Audio)") == "Song", "Artist - Song");
    expect(Lyrics::cleanTitle("Song (feat. Someone)") == "Song (feat. Someone)", "a bracket that's part of it");
    expect(Lyrics::cleanTitle("Plain") == "Plain", "bare");
    // In any language and script
    expect(Lyrics::cleanTitle("Kaavaalaa | Jailer | Superstar Rajinikanth | Anirudh | Tamannaah") == "Kaavaalaa", "Song | Movie | …");
    expect(Lyrics::cleanTitle("Naa Ready - Lyric Video | Leo") == "Naa Ready", "Song - Lyric Video");
    expect(Lyrics::cleanTitle("Hukum (From \"Jailer\") | Lyrical") == "Hukum", "(From \"Movie\")");
    expect(Lyrics::cleanTitle("YOASOBI「アイドル」 Official Music Video") == "アイドル", "the title in 「」");
    expect(Lyrics::cleanTitle("【MV】米津玄師 - Lemon") == "Lemon", "【MV】 and Artist - Song");
    expect(Lyrics::cleanTitle("아이유 (IU) - 밤편지 (Through the Night) [MV]") == "밤편지 (Through the Night)", "Korean, a bracket that's part of it");
    expect(Lyrics::cleanTitle("Despacito (Video Oficial) [Official Video]") == "Despacito (Video Oficial)" ||
               Lyrics::cleanTitle("Despacito (Video Oficial) [Official Video]") == "Despacito", "Spanish");
    expect(Lyrics::cleanTitle("أغنية (Official Lyric Video)") == "أغنية", "Arabic");
    expect(Lyrics::cleanTitle("Shadow (Remix)") == "Shadow (Remix)", "\"hd\" inside a word isn't HD");
    expect(Lyrics::cleanArtist("Anirudh Ravichander - Topic") == "Anirudh Ravichander" && Lyrics::cleanArtist("TaylorSwiftVEVO") == "TaylorSwift",
           "channel names");

    // Shazam, through songrec: a real match, and a stray one (stretched)
    Lyrics::Heard heard;
    const char* match = R"({"matches": [{"id": "1", "offset": 63.4, "timeskew": 0.0002, "frequencyskew": -0.0001}],
        "track": {"title": "Lemon", "subtitle": "Kenshi Yonezu",
                  "sections": [{"type": "SONG"}, {"type": "LYRICS", "text": ["夢ならばどれほどよかったでしょう", "", "未だにあなたのことを夢にみる"]}]}})";
    expect(Lyrics::parseShazam(match, heard) && heard.title == "Lemon" && heard.artist == "Kenshi Yonezu", "a match");
    expect(heard.offset > 63 && heard.offset < 64, "how far into the song");
    expect(heard.lyrics.find("夢ならば") == 0, "its lyrics");
    const char* stray = R"({"matches": [{"offset": 785.8, "timeskew": 0.044, "frequencyskew": 0.046}], "track": {"title": "BATMAN"}})";
    expect(!Lyrics::parseShazam(stray, heard), "a stray match is no match");
    expect(!Lyrics::parseShazam(R"({"matches": [], "tagid": "x"})", heard) && !Lyrics::parseShazam("oops", heard), "none");
    if (failures == 0) std::puts("all passed");
    return failures == 0 ? 0 : 1;
}
