// Podcasts (material3ui-podcasts): shows you subscribe to by their feed,
// found through Apple's podcast directory (only what's searched for is
// sent) or pasted. New episodes from all of them, each show's page, and a
// player that remembers where each episode was left; episodes stream, or
// play from a download.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <glib.h>
#include <mpv/client.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> podcastsGo;

namespace {

std::string cacheDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.cache") + "/material3ui/podcasts";
}
std::string downloadsDir() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/material3ui/podcasts";
}
std::string stateFile() {
    return paths::stateDir() + "/podcasts.json";
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
std::string minutes(double s) {
    const long m = std::lround(s / 60);
    return m >= 60 ? std::format("{} h {} min", m / 60, m % 60) : std::format("{} min", std::max(1L, m));
}

// ── Feeds: RSS read leniently (feeds in the wild aren't all valid XML)

std::string decode(std::string s) {
    // CDATA kept as it is; entities turned back into characters
    if (auto a = s.find("<![CDATA["); a != std::string::npos) {
        const auto b = s.find("]]>", a);
        s = s.substr(a + 9, b == std::string::npos ? std::string::npos : b - a - 9);
        return s;
    }
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += '&';
            continue;
        }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent == "nbsp") out += ' ';
        else if (!ent.empty() && ent[0] == '#') {
            const gunichar c = static_cast<gunichar>(ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::strtoul(ent.c_str() + 2, nullptr, 16)
                                                                                                           : std::strtoul(ent.c_str() + 1, nullptr, 10));
            char buf[8];
            out.append(buf, static_cast<size_t>(g_unichar_to_utf8(c, buf)));
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}
// HTML (show notes) as plain text, on one line
std::string plain(const std::string& html, size_t most = 400) {
    std::string out;
    bool inTag = false;
    for (char c : decode(html)) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false, out += ' ';
        else if (!inTag) out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    std::string squeezed;
    for (char c : decode(out))
        if (!(c == ' ' && (squeezed.empty() || squeezed.back() == ' '))) squeezed += c;
    while (!squeezed.empty() && squeezed.back() == ' ') squeezed.pop_back();
    if (squeezed.size() > most) {
        size_t cut = squeezed.rfind(' ', most);
        squeezed = squeezed.substr(0, cut == std::string::npos ? most : cut) + "…";
    }
    return squeezed;
}
// The text of the first <tag>…</tag> in a block
std::string tagText(const std::string& block, const std::string& tag) {
    size_t a = 0;
    while ((a = block.find("<" + tag, a)) != std::string::npos) {
        const char next = a + tag.size() + 1 < block.size() ? block[a + tag.size() + 1] : '>';
        if (next == '>' || next == ' ' || next == '/' || next == '\t' || next == '\n') break;
        a += tag.size() + 1;
    }
    if (a == std::string::npos) return "";
    const size_t open = block.find('>', a);
    if (open == std::string::npos || block[open - 1] == '/') return "";
    const size_t close = block.find("</" + tag + ">", open);
    if (close == std::string::npos) return "";
    return block.substr(open + 1, close - open - 1);
}
// An attribute of the first <tag …>
std::string tagAttr(const std::string& block, const std::string& tag, const std::string& attr) {
    const size_t a = block.find("<" + tag);
    if (a == std::string::npos) return "";
    const size_t end = block.find('>', a);
    const std::string t = block.substr(a, end == std::string::npos ? std::string::npos : end - a);
    for (const char q : {'"', '\''}) {
        const std::string key = attr + "=" + q;
        if (size_t k = t.find(" " + key); k != std::string::npos) {
            k += key.size() + 1;
            const size_t z = t.find(q, k);
            if (z != std::string::npos) return decode(t.substr(k, z - k));
        }
    }
    return "";
}
int durationOf(const std::string& d) {
    // "1:02:03", "62:03" or seconds
    int total = 0;
    std::istringstream in(d);
    for (std::string part; std::getline(in, part, ':');) total = total * 60 + std::atoi(part.c_str());
    return total;
}
std::time_t dateOf(const std::string& d) {
    std::tm tm{};
    const std::string s = d.find(',') != std::string::npos ? d.substr(d.find(',') + 1) : d;
    if (!strptime(s.c_str(), " %d %b %Y %H:%M:%S", &tm) && !strptime(s.c_str(), " %d %b %Y", &tm)) return 0;
    return timegm(&tm);
}

struct Episode {
    std::string title, url, notes, show;
    std::time_t when = 0;
    int duration = 0;
};
struct Show {
    std::string feed, title, author, image, about;
    std::vector<Episode> episodes;
    bool loading = false;
    std::string error;
};
struct Found {
    std::string title, author, feed;
};
struct Progress {
    double pos = 0, length = 0;
    bool done = false;
    std::string file; // downloaded to
};

void parseFeed(Show& s, const std::string& xml) {
    const size_t firstItem = xml.find("<item");
    const std::string channel = xml.substr(0, firstItem);
    if (const std::string t = plain(tagText(channel, "title"), 200); !t.empty()) s.title = t;
    if (const std::string a = plain(tagText(channel, "itunes:author"), 200); !a.empty()) s.author = a;
    s.image = tagAttr(channel, "itunes:image", "href");
    if (s.image.empty()) s.image = plain(tagText(tagText(channel, "image"), "url"), 1000);
    s.about = plain(tagText(channel, "description"), 600);
    s.episodes.clear();
    size_t at = firstItem;
    while (at != std::string::npos && s.episodes.size() < 500) {
        const size_t end = xml.find("</item>", at);
        if (end == std::string::npos) break;
        const std::string item = xml.substr(at, end - at);
        at = xml.find("<item", end);
        Episode e;
        e.url = tagAttr(item, "enclosure", "url");
        if (e.url.empty()) continue;
        e.title = plain(tagText(item, "title"), 300);
        e.notes = plain(tagText(item, "description").empty() ? tagText(item, "itunes:summary") : tagText(item, "description"));
        e.when = dateOf(plain(tagText(item, "pubDate"), 64));
        e.duration = durationOf(plain(tagText(item, "itunes:duration"), 32));
        e.show = s.title;
        s.episodes.push_back(std::move(e));
    }
}

// ── The player: libmpv, no picture

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
                 {"audio-client-name", "Material3UI Podcasts"}, {"load-scripts", "no"}, {"ytdl", "no"}})
            mpv_set_option_string(p->h, k, v);
        mpv_set_option_string(p->h, "volume", std::to_string(static_cast<int>(Config::get().value<double>("/podcasts/volume", 100))).c_str());
        // The bar's and the lock screen's media controls (mpv-mpris)
        for (const char* so : {"/usr/lib/mpv-mpris/mpris.so", "/etc/mpv/scripts/mpris.so", "/usr/share/mpv/scripts/mpris.so"})
            if (fs::exists(so)) {
                mpv_set_option_string(p->h, "scripts", so);
                break;
            }
        if (mpv_initialize(p->h) < 0) return nullptr;
        mpv_set_wakeup_callback(p->h, &Player::wakeup, p.get());
        for (const auto& [n, f] : std::initializer_list<std::pair<const char*, mpv_format>>{
                 {"time-pos", MPV_FORMAT_DOUBLE}, {"duration", MPV_FORMAT_DOUBLE}, {"core-idle", MPV_FORMAT_FLAG}, {"speed", MPV_FORMAT_DOUBLE}})
            mpv_observe_property(p->h, 0, n, f);
        return p;
    }
    void cmd(std::initializer_list<const char*> a) {
        std::vector<const char*> v(a);
        v.push_back(nullptr);
        mpv_command_async(h, 0, v.data());
    }
    void str(const char* n, const std::string& v) { mpv_set_property_string(h, n, v.c_str()); }
    void num(const char* n, double v) { mpv_set_property(h, n, MPV_FORMAT_DOUBLE, &v); }
};

enum class View { New, Show, Add };

struct State {
    std::vector<Show> shows;
    std::map<std::string, Progress> progress; // by episode url
    std::shared_ptr<Player> player;
    View view = View::New;
    int current = 0; // the show shown
    std::vector<Found> found;
    std::string query, findError, armed;
    bool finding = false;
    std::set<std::string> downloading;
    // What plays
    Episode now;
    std::string nowCover;
    double time = 0, length = 0, speed = 1;
    bool paused = true;
    Flex *side = nullptr, *content = nullptr;
    ScrollView* scroll = nullptr;
    Slider* seek = nullptr;
    Label *nowTitle = nullptr, *nowShow = nullptr, *tPos = nullptr, *tLen = nullptr, *speedLabel = nullptr;
    Icon* playIcon = nullptr;
    ImageView* nowArt = nullptr;
    bool seeking = false;
    std::function<void()> show, sidebar, refreshBar;
    EventLoop::TimerId saveTimer = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

std::string coverFile(const Show& s) {
    return cacheDir() + "/" + md5(s.feed) + ".img";
}

void save(const std::shared_ptr<State>& st) {
    json subs = json::array();
    for (const auto& s : st->shows) subs.push_back({{"feed", s.feed}, {"title", s.title}, {"author", s.author}, {"image", s.image}});
    json prog = json::object();
    for (const auto& [url, p] : st->progress)
        if (p.pos > 0 || p.done || !p.file.empty()) prog[url] = {{"pos", p.pos}, {"length", p.length}, {"done", p.done}, {"file", p.file}};
    std::error_code ec;
    fs::create_directories(paths::stateDir(), ec);
    paths::writeFile(stateFile(), json{{"subs", subs}, {"progress", prog}}.dump());
}
void saveSoon(const std::shared_ptr<State>& st) {
    std::weak_ptr<State> weak = st;
    EventLoop::get().debounce(st->saveTimer, 2000, [weak] {
        if (auto s = weak.lock()) save(s);
    });
}
void load(const std::shared_ptr<State>& st) {
    try {
        std::ifstream in(stateFile());
        if (!in) return;
        const json j = json::parse(in);
        for (const auto& s : j.value("subs", json::array())) {
            Show sh;
            sh.feed = s.value("feed", ""), sh.title = s.value("title", ""), sh.author = s.value("author", ""), sh.image = s.value("image", "");
            if (sh.feed.empty()) continue;
            // The feed as last fetched: shown at once, fetched again after
            std::ifstream x(cacheDir() + "/" + md5(sh.feed) + ".xml");
            if (x) {
                std::stringstream ss;
                ss << x.rdbuf();
                parseFeed(sh, ss.str());
            }
            st->shows.push_back(std::move(sh));
        }
        const json prog = j.value("progress", json::object()); // kept: items() refers to it
        for (const auto& [url, p] : prog.items())
            st->progress[url] = {p.value("pos", 0.0), p.value("length", 0.0), p.value("done", false), p.value("file", "")};
    } catch (...) {
    }
}

void redraw(const std::shared_ptr<State>& st) {
    EventLoop::get().defer([st] {
        if (!*st->alive) return;
        if (st->sidebar) st->sidebar();
        if (st->show) st->show();
    });
}

// The feed fetched (and its picture, the first time)
void refreshShow(std::shared_ptr<State> st, std::string feed) {
    auto it = std::ranges::find(st->shows, feed, &Show::feed);
    if (it == st->shows.end()) return;
    it->loading = true;
    it->error.clear();
    std::error_code ec;
    fs::create_directories(cacheDir(), ec);
    const std::string file = cacheDir() + "/" + md5(feed) + ".xml";
    m3::run({"curl", "-fsSL", "--compressed", "--max-time", "40", "-A", "Material3UI-Podcasts/1", "-o", file + ".part", feed}, [st, feed, file](int code, const std::string&) {
        if (!*st->alive) return;
        auto it2 = std::ranges::find(st->shows, feed, &Show::feed);
        if (it2 == st->shows.end()) return;
        it2->loading = false;
        std::error_code ec2;
        if (code != 0) {
            fs::remove(file + ".part", ec2);
            it2->error = "Couldn't fetch the feed: are you online?";
            return redraw(st);
        }
        fs::rename(file + ".part", file, ec2);
        std::ifstream x(file);
        std::stringstream ss;
        ss << x.rdbuf();
        parseFeed(*it2, ss.str());
        if (it2->episodes.empty()) it2->error = "No episodes in this feed (is it a podcast's?)";
        // Its picture, once
        if (!it2->image.empty() && !fs::exists(coverFile(*it2), ec2)) {
            const std::string cover = coverFile(*it2);
            m3::run({"curl", "-fsSL", "--max-time", "30", "-o", cover, it2->image}, [st](int, const std::string&) {
                if (*st->alive) redraw(st);
            });
        }
        save(st);
        redraw(st);
    });
}

void subscribe(std::shared_ptr<State> st, std::string feed, std::string title = {}) {
    if (feed.empty()) return;
    if (feed.starts_with("feed://")) feed = "https://" + feed.substr(7);
    if (std::ranges::find(st->shows, feed, &Show::feed) == st->shows.end()) {
        Show s;
        s.feed = feed;
        s.title = title.empty() ? feed : title;
        st->shows.push_back(std::move(s));
    }
    st->view = View::Show;
    st->current = static_cast<int>(std::ranges::find(st->shows, feed, &Show::feed) - st->shows.begin());
    save(st);
    refreshShow(st, feed);
    redraw(st);
}

void unsubscribe(std::shared_ptr<State> st, std::string feed) {
    std::erase_if(st->shows, [&](const Show& s) { return s.feed == feed; });
    std::error_code ec;
    fs::remove(cacheDir() + "/" + md5(feed) + ".xml", ec);
    st->view = View::New;
    save(st);
    redraw(st);
}

// Apple's directory: feeds by name (only the words go)
void find(std::shared_ptr<State> st, std::string q) {
    st->query = q;
    if (q.empty()) return;
    if (q.starts_with("http://") || q.starts_with("https://") || q.starts_with("feed://")) return subscribe(st, q);
    st->finding = true;
    st->findError.clear();
    redraw(st);
    char* e = g_uri_escape_string(q.c_str(), nullptr, false);
    const std::string url = std::string("https://itunes.apple.com/search?media=podcast&entity=podcast&limit=30&term=") + (e ? e : "");
    g_free(e);
    m3::run({"curl", "-fsSL", "--max-time", "20", "-A", "Material3UI-Podcasts/1", url}, [st, q](int code, const std::string& out) {
        if (!*st->alive || st->query != q) return;
        st->finding = false;
        st->found.clear();
        if (code != 0) st->findError = "Couldn't search: are you online?";
        else {
            try {
                for (const auto& r : json::parse(out).value("results", json::array())) {
                    Found f{r.value("collectionName", ""), r.value("artistName", ""), r.value("feedUrl", "")};
                    if (!f.feed.empty()) st->found.push_back(std::move(f));
                }
            } catch (...) {
                st->findError = "The directory answered with something unreadable";
            }
        }
        redraw(st);
    });
}

// ── Playing

void remember(const std::shared_ptr<State>& st) {
    if (st->now.url.empty() || st->time <= 0) return;
    auto& p = st->progress[st->now.url];
    p.pos = st->time;
    if (st->length > 0) p.length = st->length;
    // Near the end: played
    if (st->length > 0 && st->time >= st->length - Config::get().value<int>("/podcasts/doneWithin", 45)) p.done = true, p.pos = 0;
    saveSoon(st);
}

void play(std::shared_ptr<State> st, Episode e, std::string cover) {
    if (!st->player) return;
    remember(st);
    st->now = e;
    st->nowCover = cover;
    st->time = 0, st->length = e.duration;
    auto& p = st->progress[e.url];
    std::error_code ec;
    const std::string src = !p.file.empty() && fs::exists(p.file, ec) ? p.file : e.url;
    // Where it was left (a played one from the start)
    st->player->str("start", p.done || p.pos < 5 ? "0" : std::format("{:.0f}", std::max(0.0, p.pos - 3)));
    st->player->cmd({"loadfile", src.c_str(), "replace"});
    st->player->str("pause", "no");
    st->player->num("speed", Config::get().value<double>("/podcasts/speed", 1.0));
    if (p.done) p.done = false;
    if (st->refreshBar) st->refreshBar();
    redraw(st);
}

void download(std::shared_ptr<State> st, Episode e) {
    if (st->downloading.contains(e.url)) return;
    std::error_code ec;
    fs::create_directories(downloadsDir(), ec);
    std::string ext = fs::path(e.url.substr(0, e.url.find('?'))).extension().string();
    if (ext.empty() || ext.size() > 5) ext = ".mp3";
    const std::string file = downloadsDir() + "/" + md5(e.url) + ext;
    st->downloading.insert(e.url);
    redraw(st);
    m3::run({"curl", "-fsSL", "--max-time", "1800", "-A", "Material3UI-Podcasts/1", "-o", file + ".part", e.url}, [st, e, file](int code, const std::string&) {
        if (!*st->alive) return;
        st->downloading.erase(e.url);
        std::error_code ec2;
        if (code == 0) {
            fs::rename(file + ".part", file, ec2);
            st->progress[e.url].file = file;
            save(st);
        } else {
            fs::remove(file + ".part", ec2);
            m3::run({"notify-send", "-a", "Podcasts", "-i", "audio-x-generic", "Couldn't download", e.title}, nullptr);
        }
        redraw(st);
    });
}

void handleEvents(const std::shared_ptr<State>& st) {
    if (!st->player) return;
    bool changed = false;
    while (true) {
        mpv_event* ev = mpv_wait_event(st->player->h, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<mpv_event_end_file*>(ev->data);
            if (end && end->reason == MPV_END_FILE_REASON_EOF && !st->now.url.empty()) {
                auto& p = st->progress[st->now.url];
                p.done = true, p.pos = 0;
                save(st);
                // Played: its download goes, if Settings says so
                std::error_code ec;
                if (!p.file.empty() && Config::get().value<bool>("/podcasts/deletePlayed", false)) fs::remove(p.file, ec), p.file.clear();
                redraw(st);
            }
            continue;
        }
        if (ev->event_id != MPV_EVENT_PROPERTY_CHANGE) continue;
        const auto* pr = static_cast<mpv_event_property*>(ev->data);
        const std::string n = pr->name;
        if (pr->format == MPV_FORMAT_DOUBLE && n == "time-pos") st->time = *static_cast<double*>(pr->data);
        else if (pr->format == MPV_FORMAT_DOUBLE && n == "duration") st->length = *static_cast<double*>(pr->data);
        else if (pr->format == MPV_FORMAT_DOUBLE && n == "speed") st->speed = *static_cast<double*>(pr->data);
        else if (pr->format == MPV_FORMAT_FLAG && n == "core-idle") {
            const bool was = st->paused;
            st->paused = *static_cast<int*>(pr->data);
            if (st->paused && !was) remember(st);
        }
        changed = true;
    }
    if (changed && st->refreshBar) st->refreshBar();
    // Every few seconds, where it is
    static int tick = 0;
    if (!st->paused && ++tick % 40 == 0) remember(st);
}

// ── The pages

void art(Node* parent, const std::string& file, float size, float radius) {
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec)) {
        auto* box = parent->add<Flex>(Flex::Direction::Row);
        box->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(radius))->setBackground("tertiary_container", false);
        box->fixedWidth = box->fixedHeight = size;
        box->add<Icon>("podcasts", size * 0.5f)->setColor("on_tertiary_container", false);
        return;
    }
    auto* v = parent->add<ImageView>(size, size, [file, size](float scale, int& w, int& h) -> GLuint {
        auto* t = Images::get().picture(file, static_cast<int>(size * scale), static_cast<int>(size * scale), true);
        if (!t) return 0;
        w = t->width, h = t->height;
        return t->id;
    });
    v->cover = true;
    v->radius = radius;
}

void episodeRows(const std::shared_ptr<State>& st, Flex* c, const std::vector<std::pair<Episode, std::string>>& list, bool showShow) {
    auto* box = c->add<Flex>(Flex::Direction::Column);
    box->setSpacing(2)->setAlignItems(Align::Stretch);
    for (const auto& [e, cover] : list) {
        const auto pit = st->progress.find(e.url);
        const Progress prog = pit != st->progress.end() ? pit->second : Progress{};
        const bool playing = st->now.url == e.url;
        auto* row = box->add<Button>(Flex::Direction::Row);
        row->setPadding({12, 8})->setSpacing(14)->setAlignItems(Align::Center)->setRadius(Radii(16));
        if (playing) row->setBackground("secondary_container", false);
        art(row, cover, 48, 10);
        auto* texts = row->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(2);
        texts->grow = true;
        texts->givesWay = true;
        texts->add<Label>(e.title)->setSize(14)->setWeight(playing ? 600 : 500)->setMaxWidth(520)->setColor(prog.done ? "on_surface_variant" : "on_surface", false);
        std::string sub;
        if (showShow) sub = e.show;
        if (e.when > 0) {
            char buf[32];
            std::strftime(buf, sizeof buf, "%-d %b %Y", std::localtime(&e.when));
            sub += (sub.empty() ? "" : "  ·  ") + std::string(buf);
        }
        const double len = prog.length > 0 ? prog.length : e.duration;
        if (prog.done) sub += "  ·  Played";
        else if (prog.pos > 5 && len > 0) sub += "  ·  " + minutes(len - prog.pos) + " left";
        else if (len > 0) sub += "  ·  " + minutes(len);
        if (!prog.file.empty()) sub += "  ·  Downloaded";
        texts->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(520);
        if (!e.notes.empty()) texts->add<Label>(e.notes)->setSize(12)->setMaxWidth(520)->setMaxLines(2)->setColor("on_surface_variant", false);
        if (prog.pos > 5 && len > 0 && !prog.done) {
            auto* m = texts->add<Meter>(static_cast<float>(prog.pos / len), ColorRef("primary"), 4);
            m->fixedWidth = 160;
        }
        const bool busy = st->downloading.contains(e.url);
        if (prog.file.empty())
            iconButton(row, busy ? "downloading" : "download", [st, e] { download(st, e); }, 20)->tooltip = busy ? "Downloading…" : "Download, to play offline";
        else
            iconButton(row, "delete", [st, url = e.url] {
                std::error_code ec;
                fs::remove(st->progress[url].file, ec);
                st->progress[url].file.clear();
                save(st);
                redraw(st);
            }, 20)->tooltip = "Delete the download";
        iconButton(row, prog.done ? "replay" : "check", [st, url = e.url, done = prog.done] {
            st->progress[url].done = !done;
            st->progress[url].pos = 0;
            save(st);
            redraw(st);
        }, 20)->tooltip = prog.done ? "Mark as not played" : "Mark as played";
        row->onClicked = [st, e, cover](uint32_t b) {
            if (b != BTN_LEFT) return;
            if (st->now.url == e.url) st->player->cmd({"cycle", "pause"});
            else play(st, e, cover);
        };
    }
}

void page(const std::shared_ptr<State>& st) {
    auto* c = st->content;
    c->clearChildren();
    if (st->view == View::Add) {
        c->add<Label>("Add a podcast")->setSize(26)->setWeight(500);
        caption(c, "Search Apple's podcast directory (only your words are sent), or paste a feed's address");
        auto* box = c->add<Flex>(Flex::Direction::Row);
        pill(box->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
        box->add<Icon>("search", 20)->setColor("on_surface_variant", false);
        auto* field = box->add<TextField>();
        field->bare = true;
        field->grow = true;
        field->setPlaceholder("A show's name, or https://…/feed.xml, then Enter");
        field->setText(st->query);
        field->onSubmit = [st](const std::string& q) { find(st, q); };
        EventLoop::get().defer([field, alive = st->alive] {
            if (*alive) field->focus();
        });
        if (st->finding) caption(c, "Searching…");
        else if (!st->findError.empty()) caption(c, st->findError);
        else if (!st->query.empty() && st->found.empty()) caption(c, "Nothing found");
        for (const auto& f : st->found) {
            auto* row = c->add<Flex>(Flex::Direction::Row);
            row->setPadding({14, 8})->setSpacing(14)->setAlignItems(Align::Center)->setRadius(Radii(16))->setBackground("surface_container_high", false);
            art(row, "", 44, 10);
            auto* t = row->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start)->setSpacing(1);
            t->grow = true;
            t->givesWay = true;
            t->add<Label>(f.title)->setSize(14)->setWeight(500)->setMaxWidth(480);
            t->add<Label>(f.author)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(480);
            const bool have = std::ranges::find(st->shows, f.feed, &Show::feed) != st->shows.end();
            if (have) caption(row, "Subscribed");
            else pillButton(row, "Subscribe", true, [st, f] { subscribe(st, f.feed, f.title); }, "add");
        }
        return;
    }
    if (st->shows.empty()) {
        auto* e = c->add<Flex>(Flex::Direction::Column);
        e->setSpacing(10)->setPadding({0, 60})->setAlignItems(Align::Center);
        e->add<Icon>("podcasts", 56)->setColor("primary", false);
        e->add<Label>("Your podcasts go here")->setSize(20)->setWeight(600);
        caption(e, "Find a show and subscribe: its new episodes come here");
        pillButton(e, "Add a podcast", true, [st] {
            st->view = View::Add;
            redraw(st);
        }, "add");
        return;
    }
    if (st->view == View::Show && st->current < static_cast<int>(st->shows.size())) {
        const Show& s = st->shows[static_cast<size_t>(st->current)];
        auto* head = c->add<Flex>(Flex::Direction::Row);
        head->setSpacing(18)->setAlignItems(Align::Start);
        art(head, coverFile(s), 128, 20);
        auto* t = head->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(4);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(s.title)->setSize(24)->setWeight(600)->setMaxWidth(520)->setMaxLines(2);
        if (!s.author.empty()) t->add<Label>(s.author)->setSize(14)->setColor("on_surface_variant", false);
        if (!s.about.empty()) t->add<Label>(s.about)->setSize(12)->setMaxWidth(520)->setMaxLines(3)->setColor("on_surface_variant", false);
        auto* btns = t->add<Flow>();
        btns->spacing = 8;
        pillButton(btns, s.loading ? "Refreshing…" : "Refresh", false, [st, feed = s.feed] { refreshShow(st, feed); }, "refresh");
        pillButton(btns, "Mark all played", false, [st, feed = s.feed] {
            auto it = std::ranges::find(st->shows, feed, &Show::feed);
            if (it == st->shows.end()) return;
            for (const auto& e : it->episodes) st->progress[e.url].done = true, st->progress[e.url].pos = 0;
            save(st);
            redraw(st);
        }, "done_all");
        const bool armed = st->armed == s.feed;
        auto* un = pillButton(btns, armed ? "Tap again to unsubscribe" : "Unsubscribe", false, [st, feed = s.feed] {
            if (st->armed != feed) {
                st->armed = feed;
                return redraw(st);
            }
            st->armed.clear();
            unsubscribe(st, feed);
        }, "remove_circle");
        if (armed) un->setBackground("error_container", false);
        if (!s.error.empty()) caption(c, s.error);
        else if (s.episodes.empty()) caption(c, s.loading ? "Fetching its episodes…" : "No episodes yet");
        std::vector<std::pair<Episode, std::string>> list;
        for (const auto& e : s.episodes) list.push_back({e, coverFile(s)});
        if (list.size() > 200) list.resize(200);
        episodeRows(st, c, list, false);
        return;
    }
    // New episodes: the latest from every show
    c->add<Label>("New episodes")->setSize(26)->setWeight(500);
    std::vector<std::pair<Episode, std::string>> all;
    const bool hidePlayed = Config::get().value<bool>("/podcasts/hidePlayed", true);
    for (const auto& s : st->shows)
        for (size_t i = 0; i < s.episodes.size() && i < 30; ++i) {
            const auto& e = s.episodes[i];
            if (hidePlayed) {
                auto p = st->progress.find(e.url);
                if (p != st->progress.end() && p->second.done) continue;
            }
            all.push_back({e, coverFile(s)});
        }
    std::ranges::sort(all, [](const auto& a, const auto& b) { return a.first.when > b.first.when; });
    if (all.size() > 80) all.resize(80);
    bool loading = false;
    for (const auto& s : st->shows) loading = loading || s.loading;
    if (all.empty()) caption(c, loading ? "Fetching episodes…" : "You're up to date");
    episodeRows(st, c, all, true);
}

class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override {
        remember(m_st);
        *m_st->alive = false;
        if (m_st->saveTimer) EventLoop::get().cancel(m_st->saveTimer);
        save(m_st);
        if (m_st->player) m_st->player->onEvents = nullptr;
        m_st->show = m_st->sidebar = m_st->refreshBar = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

void skip(const std::shared_ptr<State>& st, int by) {
    st->player->cmd({"seek", std::to_string(by).c_str(), "relative"});
}

} // namespace

void podcastsApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    st->player = Player::create();
    card->setPadding(Insets(12))->setSpacing(8);
    if (!st->player) {
        caption(card, "Podcasts couldn't start: libmpv didn't initialise (is mpv installed?)");
        return;
    }
    load(st);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(12)->setAlignItems(Align::Stretch);
    columns->grow = true;
    st->side = columns->add<Flex>(Flex::Direction::Column);
    st->side->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8});
    st->side->fixedWidth = 230;
    st->scroll = columns->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->scroll->blurEdges = true;
    st->content = st->scroll->add<Flex>(Flex::Direction::Column);
    st->content->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({4, 4, 16, 12});

    // What plays, along the bottom
    auto* bar = card->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(12)->setAlignItems(Align::Center)->setPadding({12, 10})->setRadius(Radii(24))->setBackground("surface_container_high", false);
    {
        auto* artBox = bar->add<Flex>(Flex::Direction::Row);
        artBox->fixedWidth = artBox->fixedHeight = 56;
        st->nowArt = artBox->add<ImageView>(56, 56, [st](float scale, int& w, int& h) -> GLuint {
            std::error_code ec;
            if (st->nowCover.empty() || !fs::exists(st->nowCover, ec)) return 0;
            auto* t = Images::get().picture(st->nowCover, static_cast<int>(56 * scale), static_cast<int>(56 * scale), true);
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        st->nowArt->cover = true;
        st->nowArt->radius = 12;
        auto* texts = bar->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->fixedWidth = 220;
        st->nowTitle = texts->add<Label>("Nothing playing");
        st->nowTitle->setSize(14)->setWeight(600)->setMaxWidth(220);
        st->nowShow = texts->add<Label>("");
        st->nowShow->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(220);
        auto* mid = bar->add<Flex>(Flex::Direction::Column);
        mid->setAlignItems(Align::Stretch)->setSpacing(2);
        mid->grow = true;
        auto* ctl = mid->add<Flex>(Flex::Direction::Row);
        ctl->setSpacing(6)->setAlignItems(Align::Center)->setJustify(Align::Center);
        iconButton(ctl, "replay_10", [st] { skip(st, -Config::get().value<int>("/podcasts/back", 15)); }, 24)->tooltip = "Back (←)";
        auto* playB = ctl->add<Button>();
        playB->setPadding(Insets(10))->setRadius(Radii(20))->setBackground("primary", false);
        st->playIcon = playB->add<Icon>("play_arrow", 26);
        st->playIcon->setFill(true)->setColor("on_primary", false);
        playB->onClicked = [st](uint32_t b) {
            if (b == BTN_LEFT) st->player->cmd({"cycle", "pause"});
        };
        iconButton(ctl, "forward_30", [st] { skip(st, Config::get().value<int>("/podcasts/forward", 30)); }, 24)->tooltip = "Forward (→)";
        auto* sp = ctl->add<Button>();
        pill(sp->setBackground("surface_container_highest", false))->setPadding({10, 4});
        st->speedLabel = sp->add<Label>("1×");
        st->speedLabel->setSize(12)->setWeight(600);
        sp->tooltip = "Speed";
        sp->onClicked = [st](uint32_t b) {
            if (b != BTN_LEFT) return;
            static const double speeds[] = {1.0, 1.25, 1.5, 1.75, 2.0, 0.8};
            size_t i = 0;
            while (i < std::size(speeds) && std::abs(speeds[i] - st->speed) > 0.01) ++i;
            const double next = speeds[(i + 1) % std::size(speeds)];
            st->player->num("speed", next);
            Config::get().set("/podcasts/speed", next);
        };
        auto* seekRow = mid->add<Flex>(Flex::Direction::Row);
        seekRow->setSpacing(10)->setAlignItems(Align::Center);
        st->tPos = seekRow->add<Label>("0:00");
        st->tPos->setSize(11)->setColor("on_surface_variant", false);
        st->tPos->fixedWidth = 48;
        st->seek = seekRow->add<Slider>();
        st->seek->setRange(0, 1);
        st->seek->grow = true;
        st->seek->onChanged = [st](float v) {
            st->seeking = true;
            st->tPos->setText(clock(v * st->length));
        };
        st->seek->onCommitted = [st](float v) {
            st->seeking = false;
            if (st->length > 0) st->player->cmd({"seek", std::format("{}", v * 100).c_str(), "absolute-percent"});
        };
        st->tLen = seekRow->add<Label>("0:00");
        st->tLen->setSize(11)->setColor("on_surface_variant", false);
        st->tLen->fixedWidth = 48;
    }
    card->add<Keeper>(st);

    st->refreshBar = [st] {
        st->playIcon->setText(st->paused ? "play_arrow" : "pause");
        st->speedLabel->setText(std::abs(st->speed - std::round(st->speed)) < 0.01 ? std::format("{:.0f}×", st->speed) : std::format("{:g}×", st->speed));
        if (!st->now.url.empty()) {
            st->nowTitle->setText(st->now.title);
            st->nowShow->setText(st->now.show);
        }
        st->tPos->setText(clock(st->seeking ? st->seek->value() * st->length : st->time));
        st->tLen->setText(st->length > 0 ? "-" + clock(st->length - st->time) : "0:00");
        if (!st->seeking) st->seek->setValue(st->length > 0 ? static_cast<float>(st->time / st->length) : 0, false);
        st->nowArt->requestRedraw();
    };
    st->player->onEvents = [st] {
        if (*st->alive) handleEvents(st);
    };
    st->sidebar = [st] {
        auto* s = st->side;
        s->clearChildren();
        auto* brand = s->add<Label>("Podcasts");
        brand->setSize(30)->setWeight(400);
        brand->alignSelf = Align::Start;
        s->add<Node>()->fixedHeight = 6;
        auto item = [&](const std::string& icon, const std::string& name, bool here, std::function<void()> fn) {
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({16, 10})->setSpacing(14);
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(icon, 22)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            b->add<Label>(name)->setSize(14)->setWeight(here ? 600 : 500)->setColor(here ? "on_secondary_container" : "on_surface", false)->setMaxWidth(150);
            b->onClicked = [fn](uint32_t btn) {
                if (btn == BTN_LEFT) fn();
            };
        };
        item("new_releases", "New episodes", st->view == View::New, [st] {
            st->view = View::New;
            redraw(st);
        });
        item("add", "Add a podcast", st->view == View::Add, [st] {
            st->view = View::Add;
            redraw(st);
        });
        if (!st->shows.empty()) caption(s, "  Subscriptions");
        for (size_t i = 0; i < st->shows.size(); ++i)
            item("podcasts", st->shows[i].title, st->view == View::Show && st->current == static_cast<int>(i), [st, i] {
                st->view = View::Show;
                st->current = static_cast<int>(i);
                st->armed.clear();
                redraw(st);
                if (st->scroll) st->scroll->scrollTo(0, false);
            });
        s->add<Node>()->grow = true;
        caption(s, std::format("{} subscription{}", st->shows.size(), st->shows.size() == 1 ? "" : "s"));
    };
    st->show = [st] { page(st); };

    popup.onKeyBeforeNavigation = [st](const KeyEvent& e) {
        if (e.ctrl() || (e.mods & ModAlt)) return false;
        switch (e.sym) {
        case XKB_KEY_space: return st->player->cmd({"cycle", "pause"}), true;
        case XKB_KEY_Left: return skip(st, -Config::get().value<int>("/podcasts/back", 15)), true;
        case XKB_KEY_Right: return skip(st, Config::get().value<int>("/podcasts/forward", 30)), true;
        default: return false;
        }
    };
    podcastsGo = [st](const std::string& a) {
        if (*st->alive && !a.empty()) subscribe(st, a);
    };
    st->sidebar();
    st->show();
    // Each feed fetched again, as Settings says
    if (Config::get().value<bool>("/podcasts/refreshOnOpen", true))
        for (const auto& s : st->shows) refreshShow(st, s.feed);
    if (!arg.empty()) subscribe(st, arg);
}

void podcastsClosed() {
    podcastsGo = nullptr;
}

void podcastsSettings(Flex* page) {
    auto* g = group(page, "Playing");
    choiceRow<int>(g, "replay_10", "Skip back", "The back button and ←", "/podcasts/back", 15, {{10, "10 s"}, {15, "15 s"}, {30, "30 s"}});
    choiceRow<int>(g, "forward_30", "Skip forward", "The forward button and →", "/podcasts/forward", 30, {{15, "15 s"}, {30, "30 s"}, {60, "60 s"}});
    choiceRow<int>(g, "done_all", "Played when this close to the end", "Outros and ads after the show", "/podcasts/doneWithin", 45, {{0, "The end"}, {45, "45 s"}, {120, "2 min"}});
    auto* e = group(page, "Episodes");
    switchRow(e, "refresh", "Check for new episodes when opened", "", "/podcasts/refreshOnOpen", true);
    switchRow(e, "visibility_off", "Hide played ones from New episodes", "", "/podcasts/hidePlayed", true);
    switchRow(e, "delete_sweep", "Delete downloads once played", "", "/podcasts/deletePlayed", false);
    caption(page, "Shows are found through Apple's podcast directory: only what you search for is sent. Episodes come from each show's own feed.");
}

} // namespace m3::shell::panels
