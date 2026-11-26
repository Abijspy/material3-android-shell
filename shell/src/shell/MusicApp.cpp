// Music (material3ui-music): your music library, played through libmpv.
//
// The Music folder (and any others you add) is read for songs; their tags
// come from ffprobe, read once and kept (~/.cache/material3ui/music). Albums
// with their covers (a cover.jpg beside the songs, or the art inside them),
// artists, every song, your playlists, and the queue. The queue is mpv's
// playlist, so songs play gaplessly; with mpv-mpris installed the bar, the
// lock screen and the lyrics follow along as with any player.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <mpv/client.h>
#include <nlohmann/json.hpp>
#include <glib.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> musicGo;

namespace {

struct Track {
    std::string path, title, artist, album, albumArtist, genre;
    int track = 0, disc = 0, year = 0;
    double duration = 0;
    std::time_t mtime = 0;
    bool read = false; // tags read
};

struct Album {
    std::string key, name, artist, cover;
    int year = 0;
    std::vector<int> tracks; // in order
    double duration = 0;
};

std::string cacheDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.cache") + "/material3ui/music";
}
std::string dataDir() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/material3ui/music";
}
std::string md5(const std::string& s) {
    gchar* h = g_compute_checksum_for_string(G_CHECKSUM_MD5, s.c_str(), static_cast<gssize>(s.size()));
    std::string out = h;
    g_free(h);
    return out;
}
std::string clock(double s) {
    if (!std::isfinite(s) || s < 0) s = 0;
    const long t = std::lround(s);
    return t >= 3600 ? std::format("{}:{:02}:{:02}", t / 3600, t % 3600 / 60, t % 60) : std::format("{}:{:02}", t / 60, t % 60);
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> musicFolders() {
    const char* x = std::getenv("XDG_MUSIC_DIR");
    std::vector<std::string> out = {x && *x ? std::string(x) : paths::home() + "/Music"};
    std::string extra = Config::get().value<std::string>("/music/folders", "");
    std::stringstream ss(extra);
    for (std::string f; std::getline(ss, f, ':');) {
        if (f.starts_with("~/")) f = paths::home() + f.substr(1);
        if (!f.empty()) out.push_back(f);
    }
    return out;
}

// The player: libmpv with no picture
struct Player {
    mpv_handle* h = nullptr;
    std::weak_ptr<Player> self;
    std::function<void()> onEvents;
    ~Player() {
        if (h) {
            mpv_set_wakeup_callback(h, nullptr, nullptr);
            mpv_terminate_destroy(h);
        }
    }
    static void wakeup(void* ctx) {
        std::weak_ptr<Player> w = static_cast<Player*>(ctx)->self;
        EventLoop::get().post([w] {
            if (auto p = w.lock(); p && p->onEvents) p->onEvents();
        });
    }
    static std::shared_ptr<Player> create() {
        auto p = std::make_shared<Player>();
        p->self = p;
        p->h = mpv_create();
        if (!p->h) return nullptr;
        for (const auto& [k, v] : std::initializer_list<std::pair<const char*, const char*>>{
                 {"vid", "no"}, {"vo", "null"}, {"audio-display", "no"}, {"idle", "yes"}, {"terminal", "no"}, {"input-default-bindings", "no"},
                 {"audio-client-name", "Material3UI Music"}, {"load-scripts", "no"}, {"ytdl", "no"}})
            mpv_set_option_string(p->h, k, v);
        mpv_set_option_string(p->h, "gapless-audio", Config::get().value<bool>("/music/gapless", true) ? "weak" : "no");
        mpv_set_option_string(p->h, "replaygain", Config::get().value<bool>("/music/replaygain", true) ? "track" : "no");
        mpv_set_option_string(p->h, "volume", std::to_string(static_cast<int>(Config::get().value<double>("/music/volume", 80))).c_str());
        // MPRIS through the mpv-mpris plugin: the bar, the lock screen and lyrics follow
        for (const char* so : {"/usr/lib/mpv-mpris/mpris.so", "/etc/mpv/scripts/mpris.so", "/usr/share/mpv/scripts/mpris.so"})
            if (fs::exists(so)) {
                mpv_set_option_string(p->h, "scripts", so);
                break;
            }
        if (mpv_initialize(p->h) < 0) return nullptr;
        mpv_set_wakeup_callback(p->h, &Player::wakeup, p.get());
        for (const auto& [n, f] : std::initializer_list<std::pair<const char*, mpv_format>>{
                 {"playlist-pos", MPV_FORMAT_INT64}, {"time-pos", MPV_FORMAT_DOUBLE}, {"duration", MPV_FORMAT_DOUBLE}, {"pause", MPV_FORMAT_FLAG},
                 {"volume", MPV_FORMAT_DOUBLE}, {"core-idle", MPV_FORMAT_FLAG}, {"metadata/by-key/icy-title", MPV_FORMAT_STRING}})
            mpv_observe_property(p->h, 0, n, f);
        return p;
    }
    void cmd(std::initializer_list<const char*> a) {
        std::vector<const char*> v(a);
        v.push_back(nullptr);
        mpv_command_async(h, 0, v.data());
    }
    void flag(const char* n, bool on) {
        int f = on;
        mpv_set_property(h, n, MPV_FORMAT_FLAG, &f);
    }
    void num(const char* n, double v) { mpv_set_property(h, n, MPV_FORMAT_DOUBLE, &v); }
    void str(const char* n, const std::string& v) { mpv_set_property_string(h, n, v.c_str()); }
};

enum class View { Albums, Album, Artists, Artist, Songs, Playlist, Queue, Radio };

// An internet radio station, from radio-browser.info (a free directory)
struct Station {
    std::string name, url, country, tags, uuid;
    int bitrate = 0;
    json toJson() const { return {{"name", name}, {"url", url}, {"country", country}, {"tags", tags}, {"uuid", uuid}, {"bitrate", bitrate}}; }
    static Station from(const json& j) {
        Station s;
        auto str = [&](const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
        s.name = str("name");
        s.url = str("url_resolved");
        if (s.url.empty()) s.url = str("url");
        s.country = str("country");
        s.tags = str("tags");
        s.uuid = str("stationuuid");
        if (s.uuid.empty()) s.uuid = str("uuid");
        if (j.contains("bitrate") && j["bitrate"].is_number()) s.bitrate = j["bitrate"].get<int>();
        // Names come padded and tags long
        s.name.erase(0, s.name.find_first_not_of(" \t\n"));
        s.name.erase(s.name.find_last_not_of(" \t\n") + 1);
        if (s.tags.size() > 60) s.tags = s.tags.substr(0, s.tags.rfind(',', 60));
        return s;
    }
};

struct State {
    // A page navigated to: its sections arrive in turn, a grid's covers after them
    void arrive() {
        if (!content) return;
        int i = 0;
        for (const auto& child : content->children()) {
            if (auto* flow = dynamic_cast<kit::Flow*>(child.get())) {
                kit::stagger(flow, i);
                i += static_cast<int>(flow->children().size());
            } else {
                kit::enter(child.get(), i++);
            }
        }
    }
    std::vector<Track> tracks;
    std::map<std::string, int> byPath;
    std::vector<Album> albums;
    std::map<std::string, std::vector<int>> playlists; // name → track paths' indices (kept as paths on disk)
    std::vector<std::string> playlistOrder;
    std::shared_ptr<Player> player;
    std::vector<int> queue;
    int pos = -1;
    double time = 0, length = 0, volume = 80;
    bool paused = true, shuffle = false;
    int repeat = 0; // 0 off, 1 all, 2 one
    View view = View::Albums;
    std::string current; // the album key, artist name or playlist name shown
    std::string query;
    int pending = 0, running = 0, done = 0;
    std::deque<int> toRead;
    bool seeking = false;
    Flex *side = nullptr, *content = nullptr, *bar = nullptr;
    ScrollView* scroll = nullptr;
    Slider *seek = nullptr, *vol = nullptr;
    Label *nowTitle = nullptr, *nowArtist = nullptr, *tPos = nullptr, *tLen = nullptr;
    Icon *playIcon = nullptr, *shuffleIcon = nullptr, *repeatIcon = nullptr;
    ImageView* nowCover = nullptr;
    std::string nowCoverPath;
    std::function<void()> show, sidebar, refreshBar;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId saveTimer = 0, uiTimer = 0, sleepTimer = 0;
    bool sleepAfterSong = false; // the sleep timer: when this song ends
    std::time_t sleepAt = 0;
    // Radio: what's found, and what plays (its url empty: none)
    std::vector<Station> radioFound;
    std::string radioHeading = "Popular now", radioQuery, radioError;
    bool radioLoading = false, radioAsked = false;
    int radioGen = 0;
    Station radio;
    std::string onAir; // the song the station says is playing
};

// ── The library ──

void saveCache(const std::shared_ptr<State>& st) {
    json j = json::object();
    for (const auto& t : st->tracks) {
        if (!t.read) continue;
        j[t.path] = {{"mtime", t.mtime}, {"title", t.title}, {"artist", t.artist}, {"album", t.album}, {"albumArtist", t.albumArtist}, {"genre", t.genre},
                     {"track", t.track}, {"disc", t.disc}, {"year", t.year}, {"duration", t.duration}};
    }
    std::error_code ec;
    fs::create_directories(cacheDir(), ec);
    paths::writeFile(cacheDir() + "/library.json.tmp", j.dump());
    fs::rename(cacheDir() + "/library.json.tmp", cacheDir() + "/library.json", ec);
}

void savePlaylists(const std::shared_ptr<State>& st) {
    json j = json::array();
    for (const auto& name : st->playlistOrder) {
        json items = json::array();
        for (int i : st->playlists[name])
            if (i >= 0 && i < static_cast<int>(st->tracks.size())) items.push_back(st->tracks[static_cast<size_t>(i)].path);
        j.push_back({{"name", name}, {"tracks", items}});
    }
    std::error_code ec;
    fs::create_directories(dataDir(), ec);
    paths::writeFile(dataDir() + "/playlists.json", j.dump(1));
}

void groupAlbums(const std::shared_ptr<State>& st) {
    std::map<std::string, Album> by;
    for (int i = 0; i < static_cast<int>(st->tracks.size()); ++i) {
        const auto& t = st->tracks[static_cast<size_t>(i)];
        const std::string artist = !t.albumArtist.empty() ? t.albumArtist : t.artist;
        const std::string key = lower(artist) + "\x1f" + lower(t.album);
        auto& a = by[key];
        if (a.key.empty()) a.key = key, a.name = t.album, a.artist = artist;
        a.year = std::max(a.year, t.year);
        a.tracks.push_back(i);
        a.duration += t.duration;
    }
    std::vector<Album> out;
    for (auto& [k, a] : by) {
        std::ranges::sort(a.tracks, [&](int x, int y) {
            const auto &p = st->tracks[static_cast<size_t>(x)], &q = st->tracks[static_cast<size_t>(y)];
            if (p.disc != q.disc) return p.disc < q.disc;
            if (p.track != q.track) return p.track < q.track;
            return strverscmp(fs::path(p.path).filename().c_str(), fs::path(q.path).filename().c_str()) < 0;
        });
        out.push_back(std::move(a));
    }
    std::ranges::sort(out, [](const Album& a, const Album& b) {
        if (lower(a.artist) != lower(b.artist)) return lower(a.artist) < lower(b.artist);
        return a.year != b.year ? a.year < b.year : lower(a.name) < lower(b.name);
    });
    // Keep the covers already found
    for (auto& a : out)
        for (const auto& old : st->albums)
            if (old.key == a.key) a.cover = old.cover;
    st->albums = std::move(out);
}

void refreshSoon(const std::shared_ptr<State>& st) {
    EventLoop::get().debounce(st->uiTimer, 400, [st] {
        if (!*st->alive) return;
        groupAlbums(st);
        if (st->show) st->show();
        if (st->sidebar) st->sidebar();
    });
    EventLoop::get().debounce(st->saveTimer, 2000, [st] {
        if (*st->alive) saveCache(st);
    });
}

void readNext(const std::shared_ptr<State>& st) {
    while (st->running < 4 && !st->toRead.empty()) {
        const int i = st->toRead.front();
        st->toRead.pop_front();
        ++st->running;
        const std::string path = st->tracks[static_cast<size_t>(i)].path;
        m3::run({"ffprobe", "-v", "quiet", "-print_format", "json", "-show_format", "-show_streams", path}, [st, i, path](int code, const std::string& out) {
            if (!*st->alive) return;
            --st->running;
            ++st->done;
            if (i < static_cast<int>(st->tracks.size()) && st->tracks[static_cast<size_t>(i)].path == path) {
                auto& t = st->tracks[static_cast<size_t>(i)];
                t.read = true;
                if (code == 0) {
                    try {
                        const json j = json::parse(out);
                        const json fmt = j.value("format", json::object());
                        t.duration = std::atof(fmt.value("duration", "0").c_str());
                        json tags = json::object();
                        const json raw = fmt.value("tags", json::object());
                        for (auto& [k, v] : raw.items()) tags[lower(k)] = v;
                        // Ogg, Opus and FLAC keep theirs on the stream
                        const json streams = j.value("streams", json::array());
                        for (const auto& stream : streams) {
                            const json st2 = stream.value("tags", json::object());
                            for (auto& [k, v] : st2.items())
                                if (!tags.contains(lower(k))) tags[lower(k)] = v;
                        }
                        auto tag = [&](const char* k) { return tags.contains(k) && tags[k].is_string() ? tags[k].get<std::string>() : std::string(); };
                        if (!tag("title").empty()) t.title = tag("title");
                        if (!tag("artist").empty()) t.artist = tag("artist");
                        if (!tag("album").empty()) t.album = tag("album");
                        t.albumArtist = !tag("album_artist").empty() ? tag("album_artist") : tag("albumartist");
                        t.genre = tag("genre");
                        t.track = std::atoi(tag("track").c_str());
                        t.disc = std::atoi(tag("disc").c_str());
                        t.year = std::atoi((tag("date").empty() ? tag("year") : tag("date")).substr(0, 4).c_str());
                    } catch (...) {
                    }
                }
            }
            refreshSoon(st);
            readNext(st);
        });
    }
}

void scan(const std::shared_ptr<State>& st) {
    // What was read before
    json cache = json::object();
    {
        std::ifstream in(cacheDir() + "/library.json");
        try {
            if (in) cache = json::parse(in);
        } catch (...) {
        }
    }
    std::vector<Track> found;
    std::error_code ec;
    for (const auto& dir : musicFolders()) {
        if (!fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || fileops::kindOf(it->path().string()) != fileops::Kind::Audio) continue;
            Track t;
            t.path = it->path().string();
            t.mtime = static_cast<std::time_t>(std::chrono::duration_cast<std::chrono::seconds>(it->last_write_time(ec).time_since_epoch()).count());
            t.title = it->path().stem().string();
            t.artist = "Unknown artist";
            t.album = it->path().parent_path().filename().string();
            if (cache.contains(t.path) && cache[t.path].value("mtime", std::time_t{0}) == t.mtime) {
                const auto& c = cache[t.path];
                t.title = c.value("title", t.title), t.artist = c.value("artist", t.artist), t.album = c.value("album", t.album);
                t.albumArtist = c.value("albumArtist", ""), t.genre = c.value("genre", "");
                t.track = c.value("track", 0), t.disc = c.value("disc", 0), t.year = c.value("year", 0), t.duration = c.value("duration", 0.0);
                t.read = true;
            }
            found.push_back(std::move(t));
        }
    }
    std::ranges::sort(found, [](const Track& a, const Track& b) { return a.path < b.path; });
    st->tracks = std::move(found);
    st->byPath.clear();
    st->toRead.clear();
    for (int i = 0; i < static_cast<int>(st->tracks.size()); ++i) {
        st->byPath[st->tracks[static_cast<size_t>(i)].path] = i;
        if (!st->tracks[static_cast<size_t>(i)].read) st->toRead.push_back(i);
    }
    st->pending = static_cast<int>(st->toRead.size());
    st->done = 0;
    // Playlists, by path
    st->playlists.clear(), st->playlistOrder.clear();
    std::ifstream in(dataDir() + "/playlists.json");
    try {
        if (in)
            for (const auto& p : json::parse(in)) {
                const std::string name = p.value("name", "");
                if (name.empty()) continue;
                st->playlistOrder.push_back(name);
                auto& list = st->playlists[name];
                for (const auto& path : p.value("tracks", json::array()))
                    if (auto it = st->byPath.find(path.get<std::string>()); it != st->byPath.end()) list.push_back(it->second);
            }
    } catch (...) {
    }
    groupAlbums(st);
    readNext(st);
}

// An album's cover: a picture beside its songs, else the art inside the first
std::string coverOf(const std::shared_ptr<State>& st, Album& a) {
    if (!a.cover.empty()) return a.cover == "-" ? "" : a.cover;
    if (a.tracks.empty()) return "";
    const fs::path dir = fs::path(st->tracks[static_cast<size_t>(a.tracks[0])].path).parent_path();
    std::error_code ec;
    for (const char* n : {"cover.jpg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "front.png", "Cover.jpg", "Folder.jpg", "AlbumArt.jpg"})
        if (fs::exists(dir / n, ec)) return a.cover = (dir / n).string();
    const std::string out = cacheDir() + "/covers/" + md5(a.key) + ".jpg";
    if (fs::exists(out, ec)) return a.cover = out;
    a.cover = "-"; // asked for; none until it's made
    fs::create_directories(cacheDir() + "/covers", ec);
    const std::string key = a.key;
    m3::run({"ffmpeg", "-v", "quiet", "-y", "-i", st->tracks[static_cast<size_t>(a.tracks[0])].path, "-an", "-vf", "scale=512:-1", "-frames:v", "1", out},
            [st, key, out](int code, const std::string&) {
                if (!*st->alive || code != 0) return;
                std::error_code e2;
                if (!fs::exists(out, e2)) return;
                for (auto& al : st->albums)
                    if (al.key == key) al.cover = out;
                refreshSoon(st);
            });
    return "";
}

Album* albumOf(const std::shared_ptr<State>& st, int track) {
    for (auto& a : st->albums)
        if (std::ranges::find(a.tracks, track) != a.tracks.end()) return &a;
    return nullptr;
}

void coverView(Node* parent, const std::string& cover, float size, float radius) {
    if (!cover.empty()) {
        auto* v = parent->add<ImageView>(size, size, [cover, size](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().picture(cover, static_cast<int>(size * scale), static_cast<int>(size * scale), true);
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        v->cover = true;
        v->radius = radius;
        return;
    }
    auto* box = parent->add<Flex>(Flex::Direction::Row);
    box->fixedWidth = box->fixedHeight = size;
    box->setRadius(Radii(radius))->setBackground("secondary_container", false)->setAlignItems(Align::Center)->setJustify(Align::Center);
    box->add<Icon>("album", size * 0.45f)->setColor("on_secondary_container", false);
}

// ── Playing ──

void playQueue(const std::shared_ptr<State>& st, std::vector<int> list, int start) {
    if (list.empty() || !st->player) return;
    if (st->shuffle) {
        const int first = list[static_cast<size_t>(std::clamp(start, 0, static_cast<int>(list.size()) - 1))];
        list.erase(list.begin() + std::clamp(start, 0, static_cast<int>(list.size()) - 1));
        std::shuffle(list.begin(), list.end(), std::mt19937{std::random_device{}()});
        list.insert(list.begin(), first);
        start = 0;
    }
    st->queue = list;
    st->radio = {};
    st->onAir.clear();
    auto& p = *st->player;
    p.cmd({"playlist-clear"});
    for (size_t i = 0; i < list.size(); ++i)
        p.cmd({"loadfile", st->tracks[static_cast<size_t>(list[i])].path.c_str(), i == 0 ? "replace" : "append"});
    if (start > 0) {
        const std::string s = std::to_string(start);
        p.str("playlist-pos", s);
    }
    p.flag("pause", false);
    st->pos = start;
    if (st->refreshBar) st->refreshBar();
}

void enqueue(const std::shared_ptr<State>& st, int track, bool next) {
    if (!st->player) return;
    if (st->queue.empty()) return playQueue(st, {track}, 0);
    auto& p = *st->player;
    p.cmd({"loadfile", st->tracks[static_cast<size_t>(track)].path.c_str(), "append"});
    st->queue.push_back(track);
    if (next && st->pos >= 0) {
        // Moved to just after what's playing
        const std::string from = std::to_string(st->queue.size() - 1), to = std::to_string(st->pos + 1);
        p.cmd({"playlist-move", from.c_str(), to.c_str()});
        st->queue.pop_back();
        st->queue.insert(st->queue.begin() + st->pos + 1, track);
    }
    m3::run({"notify-send", "-a", "Music", "-i", "media-playlist-append", next ? "Plays next" : "Added to the queue", st->tracks[static_cast<size_t>(track)].title}, nullptr);
}

void setRepeat(const std::shared_ptr<State>& st, int r) {
    st->repeat = r;
    st->player->str("loop-playlist", r == 1 ? "inf" : "no");
    st->player->str("loop-file", r == 2 ? "inf" : "no");
    if (st->refreshBar) st->refreshBar();
}

void handleEvents(const std::shared_ptr<State>& st) {
    if (!st->player) return;
    bool changed = false;
    while (true) {
        mpv_event* e = mpv_wait_event(st->player->h, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id != MPV_EVENT_PROPERTY_CHANGE) continue;
        const auto* pr = static_cast<mpv_event_property*>(e->data);
        const std::string n = pr->name;
        if (pr->format == MPV_FORMAT_INT64 && n == "playlist-pos") {
            const int now = static_cast<int>(*static_cast<int64_t*>(pr->data));
            // The sleep timer said "when this song ends": the next one waits
            if (st->sleepAfterSong && now != st->pos && st->pos >= 0) {
                st->sleepAfterSong = false;
                st->player->flag("pause", true);
                m3::run({"notify-send", "-a", "Music", "-i", "audio-x-generic", "Music paused", "The sleep timer"}, nullptr);
            }
            st->pos = now;
        }
        else if (pr->format == MPV_FORMAT_DOUBLE && n == "time-pos") st->time = *static_cast<double*>(pr->data);
        else if (pr->format == MPV_FORMAT_DOUBLE && n == "duration") st->length = *static_cast<double*>(pr->data);
        else if (pr->format == MPV_FORMAT_DOUBLE && n == "volume") st->volume = *static_cast<double*>(pr->data);
        // Playing or not, as mpv sees it (paused, idle and stopped alike)
        else if (pr->format == MPV_FORMAT_FLAG && n == "core-idle") st->paused = *static_cast<int*>(pr->data);
        else if (n == "metadata/by-key/icy-title") st->onAir = pr->format == MPV_FORMAT_STRING && pr->data ? *static_cast<char**>(pr->data) : "";
        changed = true;
    }
    if (changed && st->refreshBar) st->refreshBar();
}

int currentTrack(const std::shared_ptr<State>& st) {
    return st->pos >= 0 && st->pos < static_cast<int>(st->queue.size()) ? st->queue[static_cast<size_t>(st->pos)] : -1;
}

// ── Radio ──

std::vector<Station> radioFavourites() {
    std::vector<Station> out;
    const auto j = Config::get().value<json>("/music/radio", json::array());
    if (j.is_array())
        for (const auto& e : j)
            if (e.is_object()) out.push_back(Station::from(e));
    return out;
}
bool isFavourite(const Station& s) {
    for (const auto& f : radioFavourites())
        if (f.url == s.url) return true;
    return false;
}
void toggleFavourite(const Station& s) {
    json j = json::array();
    bool had = false;
    for (const auto& f : radioFavourites()) {
        if (f.url == s.url) had = true;
        else j.push_back(f.toJson());
    }
    if (!had) j.push_back(s.toJson());
    Config::get().set("/music/radio", j);
}

void playRadio(std::shared_ptr<State> st, Station s) { // by value: the row clicked goes when the page redraws
    if (!st->player || s.url.empty()) return;
    st->queue.clear();
    st->pos = -1;
    st->radio = s;
    st->onAir.clear();
    st->player->cmd({"playlist-clear"});
    st->player->cmd({"loadfile", s.url.c_str(), "replace"});
    st->player->flag("pause", false);
    if (st->refreshBar) st->refreshBar();
    if (st->show && st->view == View::Radio) st->show();
}

// Stations from radio-browser.info: by name, by genre, or the most played.
// Only what's searched for goes with it (and a plain app name)
// `what` by value: a chip's click calls this, and the page it redraws takes
// the chip (and the string it held) with it
void findStations(std::shared_ptr<State> st, std::string what, bool byTag) {
    const int gen = ++st->radioGen;
    st->radioLoading = true;
    st->radioAsked = true;
    st->radioError.clear();
    st->radioQuery = byTag ? "" : what;
    st->radioHeading = what.empty() ? "Popular now" : byTag ? what : "Stations named “" + what + "”";
    if (st->show && st->view == View::Radio) st->show();
    char* q = g_uri_escape_string(what.c_str(), nullptr, false);
    const std::string esc = q ? q : "";
    g_free(q);
    const std::string base = "https://de1.api.radio-browser.info/json/stations/";
    const std::string common = "limit=60&hidebroken=true&order=clickcount&reverse=true";
    const std::string url = what.empty() ? base + "topclick/60?hidebroken=true"
                            : byTag      ? base + "bytagexact/" + esc + "?" + common
                                         : base + "search?name=" + esc + "&" + common;
    m3::run({"curl", "-fsSL", "--max-time", "15", "--retry", "1", "-A", "Material3UI-Music/1", url}, [st, gen](int code, const std::string& out) {
        if (!*st->alive || gen != st->radioGen) return;
        st->radioLoading = false;
        st->radioFound.clear();
        if (code != 0) {
            st->radioError = "Couldn't reach radio-browser.info: are you online?";
        } else {
            try {
                std::set<std::string> seen;
                for (const auto& e : json::parse(out)) {
                    Station s = Station::from(e);
                    if (s.url.empty() || s.name.empty() || !seen.insert(s.url).second) continue;
                    st->radioFound.push_back(std::move(s));
                }
            } catch (...) {
                st->radioError = "radio-browser.info answered with something unreadable";
            }
        }
        if (st->show && st->view == View::Radio) st->show();
    });
}

void stationRows(const std::shared_ptr<State>& st, Flex* c, const std::vector<Station>& list) {
    auto* box = c->add<Flex>(Flex::Direction::Column);
    box->setSpacing(2)->setAlignItems(Align::Stretch);
    for (const auto& s : list) {
        const bool playing = st->radio.url == s.url;
        auto* row = box->add<Button>(Flex::Direction::Row);
        row->setPadding({14, 8})->setSpacing(14)->setAlignItems(Align::Center)->setRadius(Radii(16));
        if (playing) row->setBackground("secondary_container", false);
        auto* badge = row->add<Flex>(Flex::Direction::Row);
        badge->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(12))->setBackground(playing ? "primary" : "surface_container_highest", false);
        badge->fixedWidth = badge->fixedHeight = 40;
        badge->add<Icon>(playing ? "graphic_eq" : "radio", 22)->setColor(playing ? "on_primary" : "on_surface_variant", false);
        auto* texts = row->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->grow = true;
        texts->givesWay = true;
        texts->add<Label>(s.name)->setSize(14)->setWeight(playing ? 600 : 500)->setMaxWidth(460);
        std::string sub = s.country;
        if (!s.tags.empty()) sub += (sub.empty() ? "" : "  ·  ") + s.tags;
        if (s.bitrate > 0) sub += std::format("{}{} kbps", sub.empty() ? "" : "  ·  ", s.bitrate);
        texts->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(460);
        const bool fav = isFavourite(s);
        auto* star = iconButton(row, "star", [st, s] {
            toggleFavourite(s);
            EventLoop::get().defer([st] {
                if (*st->alive && st->show && st->view == View::Radio) st->show();
            });
        }, 20);
        star->tooltip = fav ? "Remove from favourites" : "Add to favourites";
        if (auto* ic = dynamic_cast<Icon*>(star->children().front().get())) ic->setFill(fav)->setColor(fav ? "primary" : "on_surface_variant", false);
        row->onClicked = [st, s](uint32_t b) {
            if (b != BTN_LEFT) return;
            if (st->radio.url == s.url) st->player->cmd({"cycle", "pause"});
            else playRadio(st, s);
        };
    }
}

void radioPage(const std::shared_ptr<State>& st, Flex* c) {
    c->add<Label>("Radio")->setSize(26)->setWeight(500);
    caption(c, "Stations from around the world, from radio-browser.info (a free, open directory)");
    auto* box = c->add<Flex>(Flex::Direction::Row);
    pill(box->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    box->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* field = box->add<TextField>();
    field->bare = true;
    field->grow = true;
    field->setPlaceholder("Find a station by name, then Enter");
    field->setText(st->radioQuery);
    field->onSubmit = [st](const std::string& q) { findStations(st, q, false); };
    // Genres, a tap away
    auto* chips = c->add<kit::Flow>();
    chips->spacing = 8;
    for (const char* g : {"pop", "rock", "jazz", "classical", "news", "lofi", "electronic", "hip hop", "ambient", "talk"}) {
        auto* b = chips->add<Button>();
        const bool here = st->radioHeading == g;
        pill(b->setBackground(here ? "secondary_container" : "surface_container_high", false))->setPadding({14, 6});
        std::string label = g;
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
        b->add<Label>(label)->setSize(13)->setWeight(500);
        b->onClicked = [st, tag = std::string(g)](uint32_t btn) {
            if (btn == BTN_LEFT) findStations(st, tag, true);
        };
    }
    if (const auto favs = radioFavourites(); !favs.empty()) {
        c->add<Label>("Favourites")->setSize(18)->setWeight(500);
        stationRows(st, c, favs);
    }
    if (!st->radioAsked) findStations(st, "", false);
    std::string heading = st->radioHeading;
    if (!heading.empty()) heading[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(heading[0])));
    c->add<Label>(heading)->setSize(18)->setWeight(500);
    if (st->radioLoading) caption(c, "Looking…");
    else if (!st->radioError.empty()) caption(c, st->radioError);
    else if (st->radioFound.empty()) caption(c, "No stations found");
    else stationRows(st, c, st->radioFound);
}

// ── The pages ──

void go(const std::shared_ptr<State>& st, View v, const std::string& what = {}) {
    st->view = v;
    st->current = what;
    EventLoop::get().defer([st] {
        if (!*st->alive) return;
        if (st->sidebar) st->sidebar();
        if (st->show) st->show();
        st->arrive(); // navigated: the page comes in
    });
}

void trackMenu(const std::shared_ptr<State>& st, Node* from, int track, std::function<void()> remove = {}) {
    std::vector<MenuItem> items;
    items.emplace_back("queue_play_next", "Play next", "", [st, track] { enqueue(st, track, true); });
    items.emplace_back("add_to_queue", "Add to the queue", "", [st, track] { enqueue(st, track, false); });
    for (size_t i = 0; i < st->playlistOrder.size(); ++i) {
        const std::string name = st->playlistOrder[i];
        items.emplace_back("playlist_add", "Add to " + name, "", [st, track, name] {
            st->playlists[name].push_back(track);
            savePlaylists(st);
        }, false, false, true, i == 0);
    }
    if (remove) items.emplace_back("playlist_remove", "Remove from this list", "", remove, false, true, true, true);
    if (Album* a = albumOf(st, track)) {
        const std::string key = a->key;
        items.emplace_back("album", "Go to the album", "", [st, key] { go(st, View::Album, key); }, false, false, true, true);
    }
    const std::string artist = st->tracks[static_cast<size_t>(track)].artist;
    items.emplace_back("person", "Go to " + artist, "", [st, artist] { go(st, View::Artist, artist); });
    const std::string path = st->tracks[static_cast<size_t>(track)].path;
    items.emplace_back("folder_open", "Show in Files", "", [path] {
        if (open) open("files " + path);
    });
    floatingMenu(from, items);
}

// Songs as rows: number or cover, title, artist, album, time; a click plays
// the list from there
void songRows(const std::shared_ptr<State>& st, Flex* c, const std::vector<int>& list, bool numbers, bool showAlbum, std::function<void(size_t)> remove = {}) {
    const int now = currentTrack(st);
    for (size_t i = 0; i < list.size(); ++i) {
        const int ti = list[i];
        const auto& t = st->tracks[static_cast<size_t>(ti)];
        auto* b = c->add<Button>(Flex::Direction::Row);
        b->setPadding({14, 8})->setSpacing(14)->setRadius(Radii(14))->setAlignItems(Align::Center);
        const bool playing = ti == now;
        if (playing) b->setBackground("secondary_container", false);
        auto* n = b->add<Label>(playing ? "" : numbers && t.track ? std::to_string(t.track) : std::to_string(i + 1));
        n->setSize(13)->setColor("on_surface_variant", false);
        n->fixedWidth = 26;
        if (playing) {
            b->remove(n);
            auto* ic = b->add<Icon>(st->paused ? "pause" : "graphic_eq", 18);
            ic->setColor("primary", false);
            ic->fixedWidth = 26;
        }
        auto* texts = b->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->grow = true;
        texts->givesWay = true;
        texts->add<Label>(t.title)->setSize(14)->setWeight(playing ? 600 : 500)->setMaxWidth(420);
        texts->add<Label>(showAlbum ? t.artist + "  ·  " + t.album : t.artist)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420);
        auto* d = b->add<Label>(clock(t.duration));
        d->setSize(13)->setColor("on_surface_variant", false);
        b->onClicked = [st, list, i, b, remove](uint32_t btn) {
            if (btn == BTN_LEFT) playQueue(st, list, static_cast<int>(i));
            if (btn == BTN_RIGHT) trackMenu(st, b, list[i], remove ? std::function<void()>([remove, i] { remove(i); }) : std::function<void()>());
        };
    }
}

void header(const std::shared_ptr<State>& st, Flex* c, const std::string& cover, const std::string& kind, const std::string& title, const std::string& sub,
            const std::vector<int>& list, bool round = false) {
    auto* h = c->add<Flex>(Flex::Direction::Row);
    h->setSpacing(22)->setAlignItems(Align::End)->setPadding({4, 4, 4, 8});
    coverView(h, cover, 180, round ? 90 : 24);
    auto* t = h->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(4);
    t->grow = true;
    t->add<Label>(kind)->setSize(12)->setWeight(600)->setColor("primary", false);
    t->add<Label>(title)->setSize(30)->setWeight(500)->setMaxWidth(460)->setMaxLines(2);
    t->add<Label>(sub)->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(460);
    auto* btns = t->add<Flex>(Flex::Direction::Row);
    btns->setSpacing(8)->setPadding({0, 8, 0, 0});
    pillButton(btns, "Play", true, [st, list] {
        st->shuffle = false;
        playQueue(st, list, 0);
    }, "play_arrow");
    pillButton(btns, "Shuffle", false, [st, list] {
        st->shuffle = true;
        playQueue(st, list, static_cast<int>(std::random_device{}() % std::max<size_t>(1, list.size())));
    }, "shuffle");
}

std::vector<int> filtered(const std::shared_ptr<State>& st) {
    std::vector<int> out;
    const std::string q = lower(st->query);
    for (int i = 0; i < static_cast<int>(st->tracks.size()); ++i) {
        const auto& t = st->tracks[static_cast<size_t>(i)];
        if (q.empty() || lower(t.title + " " + t.artist + " " + t.album + " " + t.genre).find(q) != std::string::npos) out.push_back(i);
    }
    return out;
}

void albumGrid(const std::shared_ptr<State>& st, Flex* c, const std::vector<Album*>& albums) {
    auto* flow = c->add<Flow>();
    flow->spacing = 16;
    for (Album* a : albums) {
        auto* b = flow->add<Button>(Flex::Direction::Column);
        b->setPadding(Insets(8))->setSpacing(6)->setRadius(Radii(20))->setAlignItems(Align::Start);
        b->fixedWidth = 176;
        b->hoverLift = true;
        coverView(b, coverOf(st, *a), 160, 16);
        b->add<Label>(a->name)->setSize(14)->setWeight(600)->setMaxWidth(160);
        b->add<Label>(a->artist + (a->year ? std::format("  ·  {}", a->year) : ""))->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(160);
        const std::string key = a->key;
        const std::vector<int> list = a->tracks;
        b->onClicked = [st, key, list](uint32_t btn) {
            if (btn == BTN_LEFT) go(st, View::Album, key);
            if (btn == BTN_MIDDLE) playQueue(st, list, 0);
        };
    }
}

void emptyLibrary(const std::shared_ptr<State>& st, Flex* c) {
    auto* e = c->add<Flex>(Flex::Direction::Column);
    e->setAlignItems(Align::Center)->setSpacing(12)->setPadding({0, 70});
    e->add<Icon>("library_music", 72)->setColor("primary", false);
    e->add<Label>("Your music goes here")->setSize(22)->setWeight(500);
    caption(e, "Songs in " + fileops::displayPath(musicFolders().front()) + " show up by themselves. More folders: the gear → Settings");
    pillButton(e, "Open the Music folder", true, [] {
        if (open) open("files " + musicFolders().front());
    }, "folder_open");
    (void)st;
}

void page(const std::shared_ptr<State>& st) {
    auto* c = st->content;
    c->clearChildren();
    if (st->view == View::Radio) return radioPage(st, c);
    // Search, at the top of every page
    auto* tools = c->add<Flex>(Flex::Direction::Row);
    tools->setSpacing(10)->setAlignItems(Align::Center);
    auto* box = tools->add<Flex>(Flex::Direction::Row);
    pill(box->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    box->grow = true;
    box->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* field = box->add<TextField>();
    field->bare = true;
    field->grow = true;
    field->setPlaceholder("Songs, artists, albums");
    field->setText(st->query);
    field->onChanged = [st](const std::string& q) {
        st->query = q;
        if (st->view != View::Songs && !q.empty()) st->view = View::Songs;
        EventLoop::get().defer([st] {
            if (!*st->alive) return;
            if (st->sidebar) st->sidebar();
            page(st);
            // Typing goes on
            for (auto& ch : st->content->children().front()->children())
                for (auto& g : ch->children())
                    if (auto* f = dynamic_cast<TextField*>(g.get())) f->focus();
        });
    };
    if (st->tracks.empty()) return emptyLibrary(st, c);

    switch (st->view) {
    case View::Albums: {
        c->add<Label>("Albums")->setSize(26)->setWeight(500);
        std::vector<Album*> all;
        for (auto& a : st->albums) all.push_back(&a);
        // In the order Settings asks: by artist (as read), name, year, or newest added
        const std::string order = Config::get().value<std::string>("/music/albumSort", "artist");
        auto newest = [st](const Album* a) {
            std::time_t t = 0;
            for (int i : a->tracks) t = std::max(t, st->tracks[static_cast<size_t>(i)].mtime);
            return t;
        };
        if (order == "name") std::ranges::stable_sort(all, [](const Album* a, const Album* b) { return lower(a->name) < lower(b->name); });
        else if (order == "year") std::ranges::stable_sort(all, [](const Album* a, const Album* b) { return a->year > b->year; });
        else if (order == "recent") std::ranges::stable_sort(all, [&](const Album* a, const Album* b) { return newest(a) > newest(b); });
        albumGrid(st, c, all);
        break;
    }
    case View::Album: {
        Album* a = nullptr;
        for (auto& al : st->albums)
            if (al.key == st->current) a = &al;
        if (!a) return go(st, View::Albums);
        header(st, c, coverOf(st, *a), "ALBUM", a->name,
               std::format("{}{}  ·  {} songs, {}", a->artist, a->year ? std::format("  ·  {}", a->year) : "", a->tracks.size(), clock(a->duration)), a->tracks);
        auto* list = c->add<Flex>(Flex::Direction::Column);
        list->setSpacing(2)->setAlignItems(Align::Stretch);
        songRows(st, list, a->tracks, true, false);
        break;
    }
    case View::Artists: {
        c->add<Label>("Artists")->setSize(26)->setWeight(500);
        std::map<std::string, std::pair<int, int>> artists; // songs, albums
        std::map<std::string, std::string> spelled;
        for (const auto& t : st->tracks) {
            ++artists[lower(t.artist)].first;
            spelled[lower(t.artist)] = t.artist;
        }
        for (const auto& a : st->albums) ++artists[lower(a.artist)].second;
        auto* list = c->add<Flex>(Flex::Direction::Column);
        list->setSpacing(2)->setAlignItems(Align::Stretch);
        for (const auto& [k, n] : artists) {
            if (!spelled.contains(k)) continue;
            auto* b = list->add<Button>(Flex::Direction::Row);
            b->setPadding({14, 8})->setSpacing(14)->setRadius(Radii(16))->setAlignItems(Align::Center);
            auto* dot = b->add<Flex>(Flex::Direction::Row);
            dot->fixedWidth = dot->fixedHeight = 44;
            dot->setRadius(Radii(22))->setBackground("tertiary_container", false)->setAlignItems(Align::Center)->setJustify(Align::Center);
            dot->add<Label>(spelled[k].substr(0, spelled[k].empty() ? 0 : static_cast<size_t>(g_utf8_next_char(spelled[k].c_str()) - spelled[k].c_str())))->setSize(18)->setWeight(600)->setColor("on_tertiary_container", false);
            auto* t = b->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start);
            t->grow = true;
            t->add<Label>(spelled[k])->setSize(15)->setWeight(500)->setMaxWidth(440);
            t->add<Label>(std::format("{} songs  ·  {} albums", n.first, n.second))->setSize(12)->setColor("on_surface_variant", false);
            const std::string name = spelled[k];
            b->onClicked = [st, name](uint32_t btn) {
                if (btn == BTN_LEFT) go(st, View::Artist, name);
            };
        }
        break;
    }
    case View::Artist: {
        std::vector<int> songs;
        for (int i = 0; i < static_cast<int>(st->tracks.size()); ++i)
            if (lower(st->tracks[static_cast<size_t>(i)].artist) == lower(st->current) || lower(st->tracks[static_cast<size_t>(i)].albumArtist) == lower(st->current)) songs.push_back(i);
        std::vector<Album*> albums;
        for (auto& a : st->albums)
            if (lower(a.artist) == lower(st->current)) albums.push_back(&a);
        header(st, c, albums.empty() ? "" : coverOf(st, *albums.front()), "ARTIST", st->current, std::format("{} songs  ·  {} albums", songs.size(), albums.size()), songs, true);
        if (!albums.empty()) {
            caption(c, "Albums");
            albumGrid(st, c, albums);
        }
        caption(c, "Songs");
        auto* list = c->add<Flex>(Flex::Direction::Column);
        list->setSpacing(2)->setAlignItems(Align::Stretch);
        songRows(st, list, songs, false, true);
        break;
    }
    case View::Songs: {
        auto songs = filtered(st);
        std::ranges::sort(songs, [&](int x, int y) {
            const auto &a = st->tracks[static_cast<size_t>(x)], &b = st->tracks[static_cast<size_t>(y)];
            return lower(a.title) < lower(b.title);
        });
        c->add<Label>(st->query.empty() ? "Songs" : "Search")->setSize(26)->setWeight(500);
        caption(c, std::format("{} songs", songs.size()));
        if (songs.size() > 1500) songs.resize(1500);
        auto* list = c->add<Flex>(Flex::Direction::Column);
        list->setSpacing(2)->setAlignItems(Align::Stretch);
        songRows(st, list, songs, false, true);
        break;
    }
    case View::Playlist: {
        auto& list = st->playlists[st->current];
        header(st, c, list.empty() ? "" : [&]() {
            Album* a = albumOf(st, list.front());
            return a ? coverOf(st, *a) : std::string();
        }(), "PLAYLIST", st->current, std::format("{} songs", list.size()), list);
        auto* row = c->add<Flex>(Flex::Direction::Row);
        row->setSpacing(8);
        const std::string name = st->current;
        pillButton(row, "Delete the playlist", false, [st, name] {
            st->playlists.erase(name);
            std::erase(st->playlistOrder, name);
            savePlaylists(st);
            go(st, View::Albums);
        }, "delete");
        if (list.empty()) caption(c, "Right-click a song and Add to " + name);
        auto* rows = c->add<Flex>(Flex::Direction::Column);
        rows->setSpacing(2)->setAlignItems(Align::Stretch);
        songRows(st, rows, list, false, true, [st, name](size_t i) {
            auto& l = st->playlists[name];
            if (i < l.size()) l.erase(l.begin() + static_cast<long>(i));
            savePlaylists(st);
            go(st, View::Playlist, name);
        });
        break;
    }
    case View::Queue: {
        c->add<Label>("Queue")->setSize(26)->setWeight(500);
        if (st->queue.empty()) {
            caption(c, "Nothing's playing. Pick an album or a song");
            break;
        }
        caption(c, std::format("{} songs  ·  {}{}", st->queue.size(), st->shuffle ? "shuffled" : "in order", st->repeat == 1 ? ", repeating" : st->repeat == 2 ? ", repeating one" : ""));
        auto* rows = c->add<Flex>(Flex::Direction::Column);
        rows->setSpacing(2)->setAlignItems(Align::Stretch);
        const auto q = st->queue;
        for (size_t i = 0; i < q.size(); ++i) {
            const auto& t = st->tracks[static_cast<size_t>(q[i])];
            auto* b = rows->add<Button>(Flex::Direction::Row);
            b->setPadding({14, 8})->setSpacing(14)->setRadius(Radii(14))->setAlignItems(Align::Center);
            const bool here = static_cast<int>(i) == st->pos;
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(here ? "graphic_eq" : "music_note", 18)->setColor(here ? "primary" : "on_surface_variant", false);
            auto* l = b->add<Label>(t.title + "  ·  " + t.artist);
            l->setSize(14)->setWeight(here ? 600 : 400)->setMaxWidth(480);
            l->grow = true;
            const int idx = static_cast<int>(i);
            b->onClicked = [st, idx](uint32_t btn) {
                if (btn != BTN_LEFT || !st->player) return;
                st->player->str("playlist-pos", std::to_string(idx));
                st->player->flag("pause", false);
            };
            iconButton(b, "close", [st, idx] {
                if (!st->player || idx == st->pos) return;
                st->player->cmd({"playlist-remove", std::to_string(idx).c_str()});
                st->queue.erase(st->queue.begin() + idx);
                if (idx < st->pos) --st->pos;
                go(st, View::Queue);
            }, 16)->tooltip = "Take it out";
        }
        break;
    }
    }
}

class MusicKeeper : public Node {
public:
    explicit MusicKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~MusicKeeper() override {
        *m_st->alive = false;
        if (m_st->saveTimer) EventLoop::get().cancel(m_st->saveTimer);
        if (m_st->uiTimer) EventLoop::get().cancel(m_st->uiTimer);
        if (m_st->sleepTimer) EventLoop::get().cancel(m_st->sleepTimer);
        // Where you were, for next time (Settings → carry on); the radio
        // leaves the last queue as it was
        if (!m_st->radio.url.empty() && m_st->queue.empty()) {
            saveCache(m_st);
            if (m_st->player) m_st->player->onEvents = nullptr;
            m_st->show = m_st->sidebar = m_st->refreshBar = nullptr;
            return;
        }
        std::vector<std::string> queue;
        for (int t : m_st->queue)
            if (t >= 0 && t < static_cast<int>(m_st->tracks.size())) queue.push_back(m_st->tracks[static_cast<size_t>(t)].path);
        Config::get().set("/music/session", nlohmann::json{{"queue", queue}, {"pos", m_st->pos}, {"time", m_st->time}});
        saveCache(m_st);
        if (m_st->player) m_st->player->onEvents = nullptr;
        m_st->show = m_st->sidebar = m_st->refreshBar = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

// The sleep timer: playing stops after a while, or when this song ends
void setSleep(const std::shared_ptr<State>& st, int minutes, bool afterSong) {
    if (st->sleepTimer) EventLoop::get().cancel(st->sleepTimer);
    st->sleepTimer = 0;
    st->sleepAt = 0;
    st->sleepAfterSong = afterSong;
    if (minutes > 0) {
        st->sleepAt = std::time(nullptr) + minutes * 60;
        st->sleepTimer = EventLoop::get().addTimer(minutes * 60 * 1000, [st] {
            if (!*st->alive) return;
            st->sleepTimer = 0;
            st->sleepAt = 0;
            st->player->flag("pause", true);
            m3::run({"notify-send", "-a", "Music", "-i", "audio-x-generic", "Music paused", "The sleep timer"}, nullptr);
            if (st->refreshBar) st->refreshBar();
        });
    }
    if (st->refreshBar) st->refreshBar();
}

void sleepMenu(const std::shared_ptr<State>& st, Node* from) {
    std::vector<MenuItem> m;
    const bool on = st->sleepAt || st->sleepAfterSong;
    for (int mins : {15, 30, 45, 60, 90}) m.emplace_back("bedtime", std::format("In {} minutes", mins), "", [st, mins] { setSleep(st, mins, false); });
    m.emplace_back("music_note", "When this song ends", "", [st] { setSleep(st, 0, true); }, st->sleepAfterSong);
    if (on) {
        std::string left = st->sleepAfterSong ? "after this song" : std::format("{} min left", std::max<long>(1, (st->sleepAt - std::time(nullptr) + 59) / 60));
        m.emplace_back("timer_off", "Turn off the sleep timer", left, [st] { setSleep(st, 0, false); }, false, true, true, true);
    }
    floatingMenu(from, m);
}

void musicApp(Flex* card, Popup& popup, const std::string& file) {
    auto st = std::make_shared<State>();
    st->player = Player::create();
    card->setPadding(Insets(12))->setSpacing(8);
    if (!st->player) {
        caption(card, "Music couldn't start: libmpv didn't initialise (is mpv installed?)");
        return;
    }
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(12)->setAlignItems(Align::Stretch);
    columns->grow = true;
    st->side = columns->add<Flex>(Flex::Direction::Column);
    st->side->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8});
    st->side->fixedWidth = 210;
    st->scroll = columns->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->scroll->blurEdges = true;
    st->content = st->scroll->add<Flex>(Flex::Direction::Column);
    st->content->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({4, 4, 16, 12});

    // Now playing, along the bottom
    st->bar = card->add<Flex>(Flex::Direction::Row);
    st->bar->setSpacing(12)->setAlignItems(Align::Center)->setPadding({12, 10})->setRadius(Radii(24));
    st->bar->setBackground("surface_container_high", false);
    {
        auto* coverBox = st->bar->add<Flex>(Flex::Direction::Row);
        coverBox->fixedWidth = coverBox->fixedHeight = 56;
        st->nowCover = coverBox->add<ImageView>(56, 56, [st](float scale, int& w, int& h) -> GLuint {
            if (st->nowCoverPath.empty()) return 0;
            auto* t = Images::get().picture(st->nowCoverPath, static_cast<int>(56 * scale), static_cast<int>(56 * scale), true);
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        st->nowCover->cover = true;
        st->nowCover->radius = 12;
        auto* texts = st->bar->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->fixedWidth = 200;
        st->nowTitle = texts->add<Label>("Nothing playing");
        st->nowTitle->setSize(14)->setWeight(600)->setMaxWidth(200);
        st->nowArtist = texts->add<Label>("");
        st->nowArtist->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(200);
        auto* mid = st->bar->add<Flex>(Flex::Direction::Column);
        mid->setAlignItems(Align::Stretch)->setSpacing(2);
        mid->grow = true;
        auto* ctl = mid->add<Flex>(Flex::Direction::Row);
        ctl->setSpacing(4)->setAlignItems(Align::Center)->setJustify(Align::Center);
        auto* sh = iconButton(ctl, "shuffle", [st] {
            st->shuffle = !st->shuffle;
            if (st->shuffle && !st->queue.empty()) {
                // The rest of the queue in a new order
                st->player->cmd({"playlist-shuffle"});
            } else if (!st->shuffle) {
                st->player->cmd({"playlist-unshuffle"});
            }
            if (st->refreshBar) st->refreshBar();
        }, 20);
        sh->tooltip = "Shuffle";
        st->shuffleIcon = dynamic_cast<Icon*>(sh->children().front().get());
        iconButton(ctl, "skip_previous", [st] {
            if (st->time > 3) st->player->cmd({"seek", "0", "absolute"});
            else st->player->cmd({"playlist-prev"});
        }, 24);
        auto* play = ctl->add<Button>();
        play->setPadding(Insets(10))->setRadius(Radii(20))->setBackground("primary", false);
        st->playIcon = play->add<Icon>("play_arrow", 26);
        st->playIcon->setFill(true)->setColor("on_primary", false);
        play->onClicked = [st](uint32_t b) {
            if (b == BTN_LEFT) st->player->cmd({"cycle", "pause"});
        };
        iconButton(ctl, "skip_next", [st] { st->player->cmd({"playlist-next"}); }, 24);
        auto* rp = iconButton(ctl, "repeat", [st] { setRepeat(st, (st->repeat + 1) % 3); }, 20);
        rp->tooltip = "Repeat: off, all, one";
        st->repeatIcon = dynamic_cast<Icon*>(rp->children().front().get());
        Button* sleep = iconButton(ctl, "bedtime", [] {}, 20);
        sleep->tooltip = "Sleep timer";
        sleep->onClicked = [st, sleep](uint32_t b) {
            if (b == BTN_LEFT) sleepMenu(st, sleep);
        };
        auto* seekRow = mid->add<Flex>(Flex::Direction::Row);
        seekRow->setSpacing(10)->setAlignItems(Align::Center);
        st->tPos = seekRow->add<Label>("0:00");
        st->tPos->setSize(11)->setColor("on_surface_variant", false);
        st->tPos->fixedWidth = 40;
        st->seek = seekRow->add<Slider>();
        st->seek->setRange(0, 1);
        st->seek->grow = true;
        st->seek->onChanged = [st](float v) {
            if (!st->radio.url.empty()) return; // live radio: nothing to seek
            st->seeking = true;
            st->tPos->setText(clock(v * st->length));
        };
        st->seek->onCommitted = [st](float v) {
            st->seeking = false;
            if (!st->radio.url.empty()) return st->refreshBar ? st->refreshBar() : void();
            if (st->length > 0) st->player->cmd({"seek", std::format("{}", v * 100).c_str(), "absolute-percent"});
        };
        st->tLen = seekRow->add<Label>("0:00");
        st->tLen->setSize(11)->setColor("on_surface_variant", false);
        st->tLen->fixedWidth = 40;
        iconButton(st->bar, "lyrics", [] {
            if (open) open("lyrics");
        }, 20)->tooltip = "Lyrics";
        iconButton(st->bar, "queue_music", [st] { go(st, View::Queue); }, 20)->tooltip = "Queue";
        st->bar->add<Icon>("volume_up", 20)->setColor("on_surface_variant", false);
        st->vol = st->bar->add<Slider>();
        st->vol->setRange(0, 130);
        st->vol->fixedWidth = 100;
        st->vol->onChanged = [st](float v) { st->player->num("volume", v); };
        st->vol->onCommitted = [](float v) { Config::get().set("/music/volume", static_cast<double>(v)); };
    }
    card->add<MusicKeeper>(st);

    st->refreshBar = [st] {
        const int t = currentTrack(st);
        st->playIcon->setText(st->paused ? "play_arrow" : "pause");
        st->shuffleIcon->setColor(st->shuffle ? "primary" : "on_surface_variant", false);
        st->repeatIcon->setText(st->repeat == 2 ? "repeat_one" : "repeat");
        st->repeatIcon->setColor(st->repeat ? "primary" : "on_surface_variant", false);
        st->tPos->setText(clock(st->seeking ? st->seek->value() * st->length : st->time));
        st->tLen->setText(clock(st->length));
        if (!st->seeking) st->seek->setValue(st->length > 0 ? static_cast<float>(st->time / st->length) : 0, false);
        st->vol->setValue(static_cast<float>(st->volume), false);
        static int lastTrack = -2;
        if (t < 0 && !st->radio.url.empty()) {
            st->nowTitle->setText(st->radio.name);
            st->nowArtist->setText(st->onAir.empty() ? "Live radio" + (st->radio.country.empty() ? "" : "  ·  " + st->radio.country) : st->onAir);
            st->nowCoverPath.clear();
            st->tLen->setText("Live");
            st->seek->setValue(1, false); // live: nothing to seek to
            st->seek->setOpacity(0.38f, false);
            return;
        }
        st->seek->setOpacity(1, false);
        if (t >= 0) {
            const auto& tr = st->tracks[static_cast<size_t>(t)];
            st->nowTitle->setText(tr.title);
            st->nowArtist->setText(tr.artist);
            if (Album* a = albumOf(st, t)) st->nowCoverPath = coverOf(st, *a);
            st->nowCover->requestRedraw();
        }
        // The highlighted row follows the song
        static bool lastPaused = true;
        if (t != lastTrack || st->paused != lastPaused) {
            lastTrack = t;
            lastPaused = st->paused;
            if (st->show && (st->view == View::Queue || st->view == View::Album || st->view == View::Playlist)) st->show();
        }
    };
    st->player->onEvents = [st] {
        if (*st->alive) handleEvents(st);
    };

    st->sidebar = [st] {
        auto* s = st->side;
        s->clearChildren();
        auto* brand = s->add<Label>("Music");
        brand->setSize(30)->setWeight(400);
        brand->alignSelf = Align::Start;
        s->add<Node>()->fixedHeight = 6;
        auto item = [&](const char* icon, const std::string& name, bool here, std::function<void()> fn) {
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({16, 10})->setSpacing(14);
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(icon, 22)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            b->add<Label>(name)->setSize(14)->setWeight(here ? 600 : 500)->setColor(here ? "on_secondary_container" : "on_surface", false)->setMaxWidth(140);
            b->onClicked = [fn](uint32_t btn) {
                if (btn == BTN_LEFT) fn();
            };
        };
        item("album", "Albums", st->view == View::Albums || st->view == View::Album, [st] { go(st, View::Albums); });
        item("person", "Artists", st->view == View::Artists || st->view == View::Artist, [st] { go(st, View::Artists); });
        item("music_note", "Songs", st->view == View::Songs, [st] {
            st->query.clear();
            go(st, View::Songs);
        });
        item("queue_music", "Queue", st->view == View::Queue, [st] { go(st, View::Queue); });
        item("radio", "Radio", st->view == View::Radio, [st] { go(st, View::Radio); });
        caption(s, "  Playlists");
        for (const auto& name : st->playlistOrder)
            item("playlist_play", name, st->view == View::Playlist && st->current == name, [st, name] { go(st, View::Playlist, name); });
        auto* add = s->add<Flex>(Flex::Direction::Row);
        add->setPadding({16, 2, 8, 2})->setSpacing(10)->setAlignItems(Align::Center);
        add->add<Icon>("playlist_add", 22)->setColor("on_surface_variant", false);
        auto* f = add->add<TextField>();
        f->bare = true;
        f->grow = true;
        f->setPlaceholder("New playlist");
        f->onSubmit = [st](const std::string& t) {
            if (t.empty() || st->playlists.contains(t)) return;
            st->playlistOrder.push_back(t);
            st->playlists[t] = {};
            savePlaylists(st);
            go(st, View::Playlist, t);
        };
        s->add<Node>()->grow = true;
        if (st->pending > 0 && st->done < st->pending) caption(s, std::format("Reading songs: {} of {}", st->done, st->pending));
        else caption(s, std::format("{} songs  ·  {} albums", st->tracks.size(), st->albums.size()));
    };
    st->show = [st] { page(st); };

    popup.onKey = [st](const KeyEvent& e) {
        if (e.ctrl() && (e.baseSym == 'f' || e.baseSym == 'F')) {
            for (auto& ch : st->content->children().front()->children())
                for (auto& g : ch->children())
                    if (auto* f = dynamic_cast<TextField*>(g.get())) f->focus();
            return true;
        }
        if (e.sym == XKB_KEY_space) return st->player->cmd({"cycle", "pause"}), true;
        if (!e.ctrl() && !(e.mods & ModAlt)) {
            switch (e.sym) {
            case XKB_KEY_n: return st->player->cmd({"playlist-next"}), true;
            case XKB_KEY_p:
                if (st->time > 3) st->player->cmd({"seek", "0", "absolute"});
                else st->player->cmd({"playlist-prev"});
                return true;
            case XKB_KEY_m: return st->player->cmd({"cycle", "mute"}), true;
            case XKB_KEY_r: return setRepeat(st, (st->repeat + 1) % 3), true;
            case XKB_KEY_Up: return st->player->num("volume", std::min(130.0, st->volume + 5)), true;
            case XKB_KEY_Down: return st->player->num("volume", std::max(0.0, st->volume - 5)), true;
            default: break;
            }
        }
        if (e.ctrl() && e.sym == XKB_KEY_Right) return st->player->cmd({"playlist-next"}), true;
        if (e.ctrl() && e.sym == XKB_KEY_Left) return st->player->cmd({"playlist-prev"}), true;
        if (e.sym == XKB_KEY_Right) return st->player->cmd({"seek", "5", "relative"}), true;
        if (e.sym == XKB_KEY_Left) return st->player->cmd({"seek", "-5", "relative"}), true;
        return false;
    };
    musicGo = [st](const std::string& f) {
        if (!*st->alive || f.empty()) return;
        auto it = st->byPath.find(f);
        if (it != st->byPath.end()) {
            Album* a = albumOf(st, it->second);
            if (a) {
                const auto list = a->tracks;
                playQueue(st, list, static_cast<int>(std::ranges::find(list, it->second) - list.begin()));
                return;
            }
        }
        // A song from outside the library: on its own
        Track t;
        t.path = f, t.title = fs::path(f).stem().string(), t.artist = "", t.album = fs::path(f).parent_path().filename().string();
        st->tracks.push_back(t);
        st->byPath[f] = static_cast<int>(st->tracks.size()) - 1;
        playQueue(st, {static_cast<int>(st->tracks.size()) - 1}, 0);
    };
    scan(st);
    // Carry on where you were: last time's queue, paused at its place
    if (file.empty() && Config::get().value<bool>("/music/resume", true)) {
        const auto sess = Config::get().value<nlohmann::json>("/music/session", nlohmann::json());
        std::vector<int> list;
        if (sess.is_object())
            for (const auto& p : sess.value("queue", nlohmann::json::array()))
                if (p.is_string())
                    if (auto it = st->byPath.find(p.get<std::string>()); it != st->byPath.end()) list.push_back(it->second);
        if (!list.empty()) {
            const int pos = std::clamp(sess.value("pos", 0), 0, static_cast<int>(list.size()) - 1);
            const double at = sess.value("time", 0.0);
            const bool shuffle = st->shuffle;
            st->shuffle = false; // the queue as it was
            playQueue(st, list, pos);
            st->shuffle = shuffle;
            st->player->flag("pause", true);
            if (at > 1)
                EventLoop::get().addTimer(400, [st, at] {
                    if (!*st->alive) return;
                    const std::string t = std::format("{:.1f}", at);
                    st->player->cmd({"seek", t.c_str(), "absolute"});
                });
        }
    }
    st->sidebar();
    st->show();
    st->refreshBar();
    if (!file.empty()) EventLoop::get().defer([file] {
        if (musicGo) musicGo(file);
    });
}

void musicClosed() {
    musicGo = nullptr;
}

// Settings → Music
void musicSettings(Flex* page) {
    auto* g = group(page, "Library");
    auto r = row(g, "folder", "The Music folder", fileops::displayPath(musicFolders().front()));
    (void)r;
    textRow(g, "create_new_folder", "More folders", "/music/folders", "");
    caption(g, "  Separated by colons: ~/Downloads/Albums:/mnt/music (Enter saves; read when Music opens)");
    auto rescan = row(g, "refresh", "Read every song again", "Tags changed outside Music? Forget what was read");
    pillButton(rescan.row, "Forget", false, [] {
        std::error_code ec;
        fs::remove(cacheDir() + "/library.json", ec);
        fs::remove_all(cacheDir() + "/covers", ec);
    }, "delete_sweep");
    auto* p = group(page, "Playback", "From the next time Music opens");
    switchRow(p, "linear_scale", "Gapless", "One song runs into the next, as on the album", "/music/gapless", true);
    switchRow(p, "equalizer", "Even out loudness", "ReplayGain, where the songs have it", "/music/replaygain", true);
    switchRow(p, "history", "Carry on where you left off", "Opening Music brings back the queue, paused at the song and second it was at", "/music/resume", true);
    auto* v = group(page, "Albums");
    choiceRow<std::string>(v, "sort", "Order", "", "/music/albumSort", "artist", {{"artist", "By artist"}, {"name", "By name"}, {"year", "Newest release first"}, {"recent", "Recently added"}});
    caption(page, "  Keys: Space plays or pauses, N and P next and previous, ← → skip 5 s, ↑ ↓ volume, M mutes, R repeats");
    caption(page, fs::exists("/usr/lib/mpv-mpris/mpris.so") ? "The bar, the lock screen and lyrics follow Music (mpv-mpris)"
                                                             : "For the bar, the lock screen and lyrics to follow Music: sudo pacman -S mpv-mpris");
}

} // namespace m3::shell::panels
