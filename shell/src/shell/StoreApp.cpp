// Store: apps to find, install and keep up to date. Explore shows Flathub's
// catalogue, read from the AppStream data flatpak keeps on disk (featured
// apps, categories, the verified ones first, icons and descriptions without
// a network round trip); search adds your system's repositories (pacman)
// and the AUR. An app's page has its screenshots (fetched when shown), what
// it does, its license and website, and Install, Open or Remove. Installed
// lists your apps; Updates is Software & updates' list, with Update all.
// Installing, removing and updating go through the shell's Packages service
// (one password, a notification when done). It's the `material3ui-store` app.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/FileOps.h"
#include "services/Packages.h"
#include "services/Search.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

using Source = Packages::Source;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ── Flathub's catalogue (AppStream) ──

struct CatalogApp {
    std::string id, name, summary, description, developer, license, homepage, icon, keywords;
    std::vector<std::string> categories, screenshots;
    bool verified = false;
    std::string version;   // the newest release's
    std::time_t released = 0;
};

const char* kAppstreamDir = "/var/lib/flatpak/appstream/flathub/x86_64/active";

std::string unescape(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += s[i];
            continue;
        }
        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            const long cp = ent[1] == 'x' ? std::strtol(ent.c_str() + 2, nullptr, 16) : std::strtol(ent.c_str() + 1, nullptr, 10);
            // UTF-8
            if (cp < 0x80) out += static_cast<char>(cp);
            else if (cp < 0x800) out += static_cast<char>(0xc0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3f));
            else if (cp < 0x10000)
                out += static_cast<char>(0xe0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)), out += static_cast<char>(0x80 | (cp & 0x3f));
            else
                out += static_cast<char>(0xf0 | (cp >> 18)), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f)),
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f)), out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

std::string collapse(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// The text of <tag>…</tag> (the untranslated one: no attributes) in `block`
std::string tagText(const std::string& block, const std::string& tag) {
    const std::string open = "<" + tag + ">", close = "</" + tag + ">";
    const size_t a = block.find(open);
    if (a == std::string::npos) return "";
    const size_t b = block.find(close, a);
    if (b == std::string::npos) return "";
    return unescape(collapse(block.substr(a + open.size(), b - a - open.size())));
}

// Removes every <tag …>…</tag> from `s`
void strip(std::string& s, const std::string& tag) {
    for (size_t a; (a = s.find("<" + tag)) != std::string::npos;) {
        const char after = a + tag.size() + 1 < s.size() ? s[a + tag.size() + 1] : '>';
        if (after != '>' && after != ' ') {
            s.erase(a, 1); // a longer tag's name: not this one
            continue;
        }
        const size_t b = s.find("</" + tag + ">", a);
        if (b == std::string::npos) {
            const size_t e = s.find('>', a);
            s.erase(a, e == std::string::npos ? std::string::npos : e - a + 1);
        } else {
            s.erase(a, b - a + tag.size() + 3);
        }
    }
}

// AppStream's description markup (<p>, <ul><li>) as text
std::string descriptionText(const std::string& raw) {
    std::string out, s = raw;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '<') {
            const size_t e = s.find('>', i);
            if (e == std::string::npos) break;
            const std::string t = s.substr(i + 1, e - i - 1);
            if (t == "li") out += "\n• ";
            else if (t == "/p" || t == "/ul" || t == "/ol") out += "\n\n";
            i = e + 1;
            continue;
        }
        out += s[i++];
    }
    // Tidy: each line collapsed, runs of blank lines one
    std::stringstream in(unescape(out));
    std::string line, result;
    int blank = 0;
    while (std::getline(in, line)) {
        line = collapse(line);
        if (line.empty()) {
            if (++blank == 1 && !result.empty()) result += "\n";
            continue;
        }
        blank = 0;
        result += line + "\n";
    }
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

std::vector<CatalogApp> parseCatalog() {
    std::vector<CatalogApp> out;
    std::ifstream in(std::string(kAppstreamDir) + "/appstream.xml");
    if (!in) return out;
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (size_t at = 0; (at = xml.find("<component", at)) != std::string::npos;) {
        const size_t end = xml.find("</component>", at);
        if (end == std::string::npos) break;
        std::string c = xml.substr(at, end - at);
        at = end;
        const std::string head = c.substr(0, c.find('>'));
        if (head.find("desktop") == std::string::npos) continue; // runtimes, fonts, extensions
        CatalogApp a;
        // Screenshots first (then out of the way): the source image of each
        if (const size_t s = c.find("<screenshots>"); s != std::string::npos) {
            const size_t e = c.find("</screenshots>", s);
            const std::string shots = c.substr(s, e == std::string::npos ? std::string::npos : e - s);
            for (size_t p = 0; (p = shots.find("<screenshot", p)) != std::string::npos && a.screenshots.size() < 5;) {
                const size_t q = shots.find("</screenshot>", p);
                const std::string one = shots.substr(p, q == std::string::npos ? std::string::npos : q - p);
                p = q == std::string::npos ? shots.size() : q;
                // The source, else the widest thumbnail
                std::string best;
                int bestW = 0;
                for (size_t i = 0; (i = one.find("<image", i)) != std::string::npos;) {
                    const size_t gt = one.find('>', i), lt = one.find("</image>", i);
                    if (gt == std::string::npos || lt == std::string::npos) break;
                    const std::string attrs = one.substr(i, gt - i), url = unescape(one.substr(gt + 1, lt - gt - 1));
                    i = lt;
                    int w = 0;
                    if (const size_t wa = attrs.find("width=\""); wa != std::string::npos) w = std::atoi(attrs.c_str() + wa + 7);
                    if (attrs.find("type=\"source\"") != std::string::npos) w = 100000;
                    if (w > 1400 && w != 100000) w = 1; // huge thumbnails: last resort
                    if (w > bestW || best.empty()) best = url, bestW = w;
                }
                if (!best.empty()) a.screenshots.push_back(best);
            }
        }
        a.verified = c.find("flathub::verification::verified\">true") != std::string::npos;
        // The newest release: its version and when
        if (const size_t rs = c.find("<releases"); rs != std::string::npos) {
            const size_t re = c.find("</releases>", rs);
            const std::string rel = c.substr(rs, re == std::string::npos ? std::string::npos : re - rs);
            for (size_t i = 0; (i = rel.find("<release ", i)) != std::string::npos; ++i) {
                const std::string tag = rel.substr(i, rel.find('>', i) - i);
                auto attr = [&](const char* k) {
                    const std::string key = std::string(k) + "=\"";
                    const size_t a0 = tag.find(key);
                    if (a0 == std::string::npos) return std::string();
                    const size_t a1 = tag.find('"', a0 + key.size());
                    return tag.substr(a0 + key.size(), a1 - a0 - key.size());
                };
                std::time_t when = std::atoll(attr("timestamp").c_str());
                if (!when) {
                    std::tm tm{};
                    const std::string d = attr("date");
                    if (!d.empty() && strptime(d.c_str(), "%Y-%m-%d", &tm)) when = timegm(&tm);
                }
                if (when >= a.released || a.version.empty()) {
                    a.released = when;
                    a.version = unescape(attr("version"));
                }
            }
        }
        if (const size_t d = c.find("<developer"); d != std::string::npos) {
            const size_t e = c.find("</developer>", d);
            if (e != std::string::npos) a.developer = tagText(c.substr(d, e - d), "name");
        }
        if (a.developer.empty()) a.developer = tagText(c, "developer_name");
        for (const char* t : {"screenshots", "releases", "developer", "content_rating", "custom", "requires", "recommends", "supports", "branding"}) strip(c, t);
        a.id = tagText(c, "id");
        if (a.id.ends_with(".desktop")) a.id.resize(a.id.size() - 8);
        a.name = tagText(c, "name");
        a.summary = tagText(c, "summary");
        if (a.id.empty() || a.name.empty()) continue;
        if (const size_t d = c.find("<description>"); d != std::string::npos) {
            const size_t e = c.find("</description>", d);
            if (e != std::string::npos) a.description = descriptionText(c.substr(d + 13, e - d - 13));
        }
        a.license = tagText(c, "project_license");
        if (const size_t u = c.find("<url type=\"homepage\">"); u != std::string::npos) {
            const size_t e = c.find("</url>", u);
            if (e != std::string::npos) a.homepage = unescape(c.substr(u + 21, e - u - 21));
        }
        // The cached icon, 128 px when there is one
        for (const char* h : {"128", "64"}) {
            for (size_t i = 0; (i = c.find("<icon ", i)) != std::string::npos; ++i) {
                const size_t gt = c.find('>', i), lt = c.find("</icon>", i);
                if (gt == std::string::npos || lt == std::string::npos) break;
                const std::string attrs = c.substr(i, gt - i);
                if (attrs.find("type=\"cached\"") == std::string::npos || attrs.find("scale=") != std::string::npos ||
                    attrs.find(std::string("height=\"") + h + "\"") == std::string::npos)
                    continue;
                const std::string file = std::string(kAppstreamDir) + "/icons/" + h + "x" + h + "/" + c.substr(gt + 1, lt - gt - 1);
                std::error_code ec;
                if (fs::exists(file, ec)) a.icon = file;
                break;
            }
            if (!a.icon.empty()) break;
        }
        if (const size_t k = c.find("<categories>"); k != std::string::npos) {
            const size_t e = c.find("</categories>", k);
            const std::string cats = c.substr(k, e == std::string::npos ? std::string::npos : e - k);
            for (size_t p = 0; (p = cats.find("<category>", p)) != std::string::npos;) {
                const size_t q = cats.find("</category>", p);
                if (q == std::string::npos) break;
                a.categories.push_back(cats.substr(p + 10, q - p - 10));
                p = q;
            }
        }
        if (const size_t k = c.find("<keywords>"); k != std::string::npos) {
            const size_t e = c.find("</keywords>", k);
            std::string kw = c.substr(k, e == std::string::npos ? std::string::npos : e - k);
            strip(kw, "keyword xml:lang");
            for (size_t p = 0; (p = kw.find("<keyword>", p)) != std::string::npos;) {
                const size_t q = kw.find("</keyword>", p);
                if (q == std::string::npos) break;
                a.keywords += " " + lower(kw.substr(p + 9, q - p - 9));
                p = q;
            }
        }
        out.push_back(std::move(a));
    }
    // Verified first (the publisher is who it says), then by name
    std::ranges::sort(out, [](const CatalogApp& a, const CatalogApp& b) {
        if (a.verified != b.verified) return a.verified;
        return lower(a.name) < lower(b.name);
    });
    return out;
}

// What Explore leads with: well-known apps, the first six in the banner
const std::vector<std::string> kFeatured = {"org.mozilla.firefox",   "com.spotify.Client",     "org.videolan.VLC",      "com.obsproject.Studio",
                                            "org.libreoffice.LibreOffice", "org.gimp.GIMP",     "com.discordapp.Discord", "com.valvesoftware.Steam",
                                            "org.blender.Blender",   "org.inkscape.Inkscape",  "org.kde.krita",         "org.telegram.desktop",
                                            "org.mozilla.Thunderbird", "org.audacityteam.Audacity", "org.kde.kdenlive",  "com.visualstudio.code"};

struct Category {
    const char* key;
    const char* name;
    const char* icon;
    const char* hue; // its tile's colour
};
const Category kCategories[] = {
    {"AudioVideo", "Audio & video", "movie", "#e8608c"}, {"Development", "Developer tools", "code", "#5c8de8"},
    {"Education", "Education", "school", "#e8a33c"},     {"Game", "Games", "sports_esports", "#9a6be8"},
    {"Graphics", "Graphics & photos", "palette", "#e8743c"}, {"Network", "Internet", "language", "#3cb4e8"},
    {"Office", "Productivity", "work", "#4caf7c"},       {"Science", "Science", "science", "#3cc4b0"},
    {"System", "System", "settings", "#8a9aa8"},         {"Utility", "Utilities", "build", "#c08a5c"},
};

const Category* categoryOf(const std::string& key) {
    for (const auto& c : kCategories)
        if (key == c.key) return &c;
    return nullptr;
}

// ── The store's state ──

struct NativeHit {
    std::string name, version, description, repo;
    Source source;
};

struct StoreState {
    std::vector<CatalogApp> catalog;
    bool loaded = false;
    std::set<std::string> flatpaks, native; // installed
    std::map<std::string, std::string> flatpakVersions;
    int tab = 0;                            // 0 Explore, 1 Installed, 2 Updates
    std::string category, query, page;      // page: an app's id (its page shown)
    std::string filter = "all";             // search: all, flathub, repo, aur
    bool alphabetical = false;              // a category's order
    size_t shown = 60;                      // a category's apps shown
    std::vector<NativeHit> repoHits, aurHits;
    bool searchingRepo = false, searchingAur = false;
    int token = 0;
    std::map<std::string, std::string> shots; // url → file (fetched)
    std::set<std::string> fetching;
    std::map<std::string, Color> tints;       // icon → its colour
    std::string installing;                   // the app an install is for
    int hero = 0, shot = 0;
    bool readMore = false;
    std::map<std::string, int> shelfPage;
    Flex *rail = nullptr, *body = nullptr;
    ScrollView* scroll = nullptr;
    TextField* search = nullptr;
    std::function<void()> show, heroNext;
    EventLoop::TimerId searchTimer = 0, refreshTimer = 0, heroTimer = 0;
    std::string packagesSig;
    bool wasBusy = false;
    std::vector<Connection> conns;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    const CatalogApp* find(const std::string& id) const {
        for (const auto& a : catalog)
            if (a.id == id) return &a;
        return nullptr;
    }
};

class StoreKeeper : public Node {
public:
    explicit StoreKeeper(std::shared_ptr<StoreState> st) : m_st(std::move(st)) { floating = true; }
    ~StoreKeeper() override {
        *m_st->alive = false;
        for (auto t : {m_st->searchTimer, m_st->refreshTimer, m_st->heroTimer})
            if (t) EventLoop::get().cancel(t);
        m_st->conns.clear();
        m_st->show = m_st->heroNext = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<StoreState> m_st;
};

void refresh(std::shared_ptr<StoreState> st) {
    if (st->show) st->show();
}

void readInstalled(std::shared_ptr<StoreState> st) {
    m3::run({"flatpak", "list", "--app", "--columns=application,version"}, [st](int, const std::string& out) {
        if (!*st->alive) return;
        std::set<std::string> now;
        std::map<std::string, std::string> versions;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
            if (l.empty()) continue;
            const size_t tab = l.find('\t');
            const std::string id = l.substr(0, tab);
            now.insert(id);
            if (tab != std::string::npos) versions[id] = l.substr(tab + 1);
        }
        if (now == st->flatpaks && versions == st->flatpakVersions) return;
        st->flatpaks = std::move(now);
        st->flatpakVersions = std::move(versions);
        refresh(st);
    });
    m3::run({"pacman", "-Qq"}, [st](int, const std::string& out) {
        if (!*st->alive) return;
        std::set<std::string> now;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);)
            if (!l.empty()) now.insert(l);
        if (now == st->native) return;
        st->native = std::move(now);
        refresh(st);
    });
}

bool safeName(const std::string& s) {
    return !s.empty() && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@._+-") == std::string::npos;
}

std::string busyLabel() {
    return Packages::get().busy() ? Packages::get().status() : "";
}

// Searches beyond Flathub: the repositories (pacman) and the AUR (its RPC)
void searchNative(std::shared_ptr<StoreState> st) {
    const int token = ++st->token;
    st->repoHits.clear();
    st->aurHits.clear();
    const std::string q = st->query;
    if (q.size() < 2) return;
    st->searchingRepo = true;
    m3::run({"pacman", "-Ss", "--color", "never", "--", q}, [st, token](int, const std::string& out) {
        if (!*st->alive || token != st->token) return;
        st->searchingRepo = false;
        std::stringstream ss(out);
        std::string line, desc;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == ' ') continue;
            std::getline(ss, desc);
            const size_t slash = line.find('/'), sp = line.find(' ');
            if (slash == std::string::npos || sp == std::string::npos || slash > sp) continue;
            const size_t sp2 = line.find(' ', sp + 1);
            st->repoHits.push_back({line.substr(slash + 1, sp - slash - 1), line.substr(sp + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp - 1),
                                    collapse(desc), line.substr(0, slash), Source::Repo});
            if (st->repoHits.size() >= 30) break;
        }
        refresh(st);
    });
    if (!Config::get().value<bool>("/store/aur", true)) return;
    st->searchingAur = true;
    std::string enc;
    for (unsigned char c : q) enc += std::isalnum(c) || c == '-' || c == '_' || c == '.' ? std::string(1, static_cast<char>(c)) : std::format("%{:02X}", c);
    m3::run({"curl", "-fsS", "--max-time", "10", "https://aur.archlinux.org/rpc/v5/search/" + enc + "?by=name-desc"}, [st, token](int code, const std::string& out) {
        if (!*st->alive || token != st->token) return;
        st->searchingAur = false;
        try {
            if (code == 0) {
                auto results = nlohmann::json::parse(out).value("results", nlohmann::json::array());
                std::vector<std::pair<int, NativeHit>> hits;
                for (const auto& p : results)
                    hits.push_back({p.value("NumVotes", 0),
                                    {p.value("Name", ""), p.value("Version", ""), p["Description"].is_string() ? p["Description"].get<std::string>() : "", "aur", Source::Aur}});
                std::ranges::sort(hits, std::greater{}, &std::pair<int, NativeHit>::first);
                for (auto& h : hits)
                    if (st->aurHits.size() < 20) st->aurHits.push_back(std::move(h.second));
            }
        } catch (...) {
        }
        refresh(st);
    });
}

// A screenshot from Flathub, fetched once into the cache
std::string screenshot(std::shared_ptr<StoreState> st, const std::string& url) {
    if (auto it = st->shots.find(url); it != st->shots.end()) return it->second;
    const std::string dir = paths::home() + "/.cache/material3ui/store";
    std::string ext = fs::path(url).extension().string();
    if (ext.size() > 5 || ext.empty()) ext = ".png";
    const std::string file = std::format("{}/{:016x}{}", dir, std::hash<std::string>{}(url), ext);
    std::error_code ec;
    if (fs::exists(file, ec) && fs::file_size(file, ec) > 0) return st->shots[url] = file;
    if (st->fetching.contains(url)) return "";
    st->fetching.insert(url);
    fs::create_directories(dir, ec);
    m3::run({"curl", "-fsSL", "--max-time", "30", "-o", file + ".part", url}, [st, url, file](int code, const std::string&) {
        std::error_code ec2;
        if (code == 0) fs::rename(file + ".part", file, ec2);
        else fs::remove(file + ".part", ec2);
        if (!*st->alive) return;
        st->fetching.erase(url);
        if (code == 0) {
            st->shots[url] = file;
            refresh(st);
        }
    });
    return "";
}

// ── Colour ──

bool darkTheme() {
    const Color s = Theme::get().color("surface");
    return 0.2126f * s.r + 0.7152f * s.g + 0.0722f * s.b < 0.5f;
}

// A colour as a container behind text: deep in the dark, pale in the light
Color tone(const Color& c) {
    return darkTheme() ? Color::mix(Color{0.07f, 0.07f, 0.09f, 1}, c, 0.34f) : Color::mix(Color{1, 1, 1, 1}, c, 0.24f);
}
// The same colour, legible on that container
Color accent(const Color& c) {
    return darkTheme() ? Color::mix(c, Color{1, 1, 1, 1}, 0.35f) : Color::mix(c, Color{0, 0, 0, 1}, 0.35f);
}

// An app's colour, from its icon: the saturated pixels' average
Color tintOf(std::shared_ptr<StoreState> st, const std::string& icon) {
    if (auto it = st->tints.find(icon); it != st->tints.end()) return it->second;
    Color out = Theme::get().color("primary");
    if (GdkPixbuf* pb = icon.empty() ? nullptr : gdk_pixbuf_new_from_file_at_size(icon.c_str(), 48, 48, nullptr)) {
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), rs = gdk_pixbuf_get_rowstride(pb), n = gdk_pixbuf_get_n_channels(pb);
        const bool alpha = gdk_pixbuf_get_has_alpha(pb);
        const guchar* px = gdk_pixbuf_read_pixels(pb);
        double r = 0, g = 0, b = 0, total = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const guchar* p = px + static_cast<size_t>(y) * rs + static_cast<size_t>(x) * n;
                const double a = alpha ? p[3] / 255.0 : 1.0;
                const double R = p[0] / 255.0, G = p[1] / 255.0, B = p[2] / 255.0;
                const double mx = std::max({R, G, B}), mn = std::min({R, G, B});
                const double sat = mx > 0 ? (mx - mn) / mx : 0;
                const double weight = a * (0.05 + sat * sat) * (mx > 0.15 ? 1 : 0.1);
                r += R * weight, g += G * weight, b += B * weight, total += weight;
            }
        if (total > 0) out = Color{static_cast<float>(r / total), static_cast<float>(g / total), static_cast<float>(b / total), 1};
        g_object_unref(pb);
    }
    return st->tints[icon] = out;
}

// ── Pieces ──

ImageView* appIcon(Node* parent, const std::string& icon, float size) {
    return parent->add<ImageView>(size, [icon, size](float scale, int& w, int& h) -> GLuint {
        if (icon.empty()) return 0;
        const auto* t = icon.front() == '/' ? Images::get().picture(icon, static_cast<int>(size * scale), static_cast<int>(size * scale), false)
                                            : Images::get().icon(icon, static_cast<int>(std::lround(size * scale)));
        if (!t) return 0;
        w = t->width, h = t->height;
        return t->id;
    });
}

void iconOrGlyph(Node* parent, const std::string& icon, float size) {
    if (!icon.empty()) {
        appIcon(parent, icon, size);
        return;
    }
    auto* box = parent->add<Flex>(Flex::Direction::Row);
    box->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(size * 0.28f))->setBackground("surface_container_highest", false);
    box->fixedWidth = size, box->fixedHeight = size;
    box->add<Icon>("apps", size * 0.55f)->setColor("on_surface_variant", false);
}

void openApp(const std::string& id) {
    if (safeName(id)) m3::spawnArgs({"flatpak", "run", id});
}

void installApp(std::shared_ptr<StoreState> st, const std::string& id) {
    if (!safeName(id) || Packages::get().busy()) return;
    st->installing = id;
    Packages::get().install({id, "", "", "flathub", Source::Flatpak});
    refresh(st);
}

// The one button a Flathub app has where it's listed: Install, Installing…, Open
Button* actionButton(std::shared_ptr<StoreState> st, Node* row, const CatalogApp& a, bool big) {
    const bool here = st->flatpaks.contains(a.id);
    const bool mine = Packages::get().busy() && st->installing == a.id;
    Button* b;
    if (mine) b = pillButton(row, "Installing…", false, [] {}, big ? "downloading" : "");
    else if (here) b = pillButton(row, "Open", !big ? false : true, [id = a.id] { openApp(id); }, big ? "open_in_new" : "");
    else {
        b = pillButton(row, "Install", true, [st, id = a.id] { installApp(st, id); }, big ? "download" : "");
        if (Packages::get().busy()) b->setOpacity(0.5f, false), b->tooltip = "Waiting for " + Packages::get().status();
    }
    if (big) b->setPadding({24, 12});
    return b;
}

void verifiedMark(Node* parent, float size = 15) {
    parent->add<Icon>("verified", size)->setFill(true)->setColor("primary", false);
}

void openPage(std::shared_ptr<StoreState> st, const std::string& id) {
    st->page = id;
    st->shot = 0;
    st->readMore = false;
    refresh(st);
    if (st->scroll) st->scroll->scrollTo(0, false);
}

void openCategory(std::shared_ptr<StoreState> st, const std::string& key) {
    st->category = key;
    st->page.clear();
    st->shown = 60;
    st->tab = 0;
    refresh(st);
    if (st->scroll) st->scroll->scrollTo(0, false);
}

constexpr float kTile = 226;

// A tile: the icon, the name, what it does, and its button
void appTile(std::shared_ptr<StoreState> st, Node* parent, const CatalogApp& a, float width = kTile) {
    auto* b = parent->add<Button>(Flex::Direction::Column);
    b->setPadding(Insets(16))->setSpacing(10)->setRadius(Radii(26))->setAlignItems(Align::Stretch);
    b->setBackground(ColorRef("surface_container", 0.85f), false);
    b->fixedWidth = width;
    b->pressedRadius = 18;
    auto* top = b->add<Flex>(Flex::Direction::Row);
    top->setAlignItems(Align::Start);
    iconOrGlyph(top, a.icon, 56);
    top->add<Node>()->grow = true;
    if (st->flatpaks.contains(a.id)) {
        auto* chip = top->add<Flex>(Flex::Direction::Row);
        pill(chip->setBackground(ColorRef("primary", 0.14f), false))->setPadding({8, 3})->setSpacing(3)->setAlignItems(Align::Center);
        chip->add<Icon>("check", 13)->setColor("primary", false);
        chip->add<Label>("Installed")->setSize(11)->setWeight(500)->setColor("primary", false);
    }
    auto* nameRow = b->add<Flex>(Flex::Direction::Row);
    nameRow->setSpacing(4)->setAlignItems(Align::Center);
    nameRow->add<Label>(a.name)->setSize(15)->setWeight(600)->setMaxWidth(width - 56);
    if (a.verified) verifiedMark(nameRow, 14);
    auto* sum = b->add<Label>(a.summary.empty() ? " " : a.summary);
    sum->setSize(12)->setMaxWidth(width - 32)->setMaxLines(2)->setColor("on_surface_variant", false);
    sum->fixedHeight = 32;
    auto* foot = b->add<Flex>(Flex::Direction::Row);
    foot->setAlignItems(Align::Center)->setSpacing(6);
    auto* dev = foot->add<Label>(a.developer);
    dev->setSize(11)->setMaxWidth(width - 120)->setColor("on_surface_variant", false);
    dev->grow = true;
    dev->givesWay = true;
    actionButton(st, foot, a, false);
    b->onClicked = [st, id = a.id](uint32_t button) {
        if (button == BTN_LEFT) openPage(st, id);
    };
}

// A section's head: its title, what it is, arrows and See all
Flex* sectionHead(Node* parent, const std::string& title, const std::string& sub = {}) {
    auto* h = parent->add<Flex>(Flex::Direction::Row);
    h->setSpacing(6)->setAlignItems(Align::Center)->setPadding({4, 8, 0, 0});
    auto* t = h->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(1);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(title)->setSize(22)->setWeight(500);
    if (!sub.empty()) t->add<Label>(sub)->setSize(13)->setColor("on_surface_variant", false);
    return h;
}

// A shelf: four apps at a time, arrows for more
void shelf(std::shared_ptr<StoreState> st, Flex* p, const std::string& key, const std::string& title, const std::string& sub,
           std::vector<const CatalogApp*> apps, const std::string& seeAll = {}) {
    if (apps.empty()) return;
    // As many as the page is wide for (four in a full window)
    const float width = st->body->rect().w > 0 ? st->body->rect().w - 20 : 960;
    const size_t kPer = std::max<size_t>(2, static_cast<size_t>((width + 12) / (kTile + 12)));
    auto* head = sectionHead(p, title, sub);
    auto* row = p->add<Flex>(Flex::Direction::Row);
    row->setSpacing(12)->setAlignItems(Align::Stretch);
    const size_t pages = (apps.size() + kPer - 1) / kPer;
    auto fill = std::make_shared<std::function<void()>>();
    Button *prev = nullptr, *next = nullptr;
    if (pages > 1) {
        prev = iconButton(head, "chevron_left", [] {}, 22);
        next = iconButton(head, "chevron_right", [] {}, 22);
        prev->setBackground("surface_container_high", false);
        next->setBackground("surface_container_high", false);
    }
    if (!seeAll.empty()) {
        auto* all = head->add<Button>(Flex::Direction::Row);
        pill(all)->setPadding({12, 6})->setSpacing(2)->setAlignItems(Align::Center);
        all->add<Label>("See all")->setSize(13)->setWeight(600)->setColor("primary", false);
        all->add<Icon>("arrow_forward", 16)->setColor("primary", false);
        all->onClicked = [st, seeAll](uint32_t b) {
            if (b == BTN_LEFT) openCategory(st, seeAll);
        };
    }
    *fill = [st, row, apps, key, pages, prev, next, kPer] {
        row->clearChildren();
        int& page = st->shelfPage[key];
        page = std::clamp(page, 0, static_cast<int>(pages) - 1);
        for (size_t i = static_cast<size_t>(page) * kPer; i < apps.size() && i < (static_cast<size_t>(page) + 1) * kPer; ++i) appTile(st, row, *apps[i]);
        if (prev) prev->setOpacity(page > 0 ? 1.0f : 0.35f, false);
        if (next) next->setOpacity(page + 1 < static_cast<int>(pages) ? 1.0f : 0.35f, false);
    };
    if (prev)
        prev->onClicked = [st, key, fill](uint32_t b) {
            if (b != BTN_LEFT || st->shelfPage[key] == 0) return;
            --st->shelfPage[key];
            (*fill)();
        };
    if (next)
        next->onClicked = [st, key, fill, pages](uint32_t b) {
            if (b != BTN_LEFT || st->shelfPage[key] + 1 >= static_cast<int>(pages)) return;
            ++st->shelfPage[key];
            (*fill)();
        };
    (*fill)();
}

// A shelf's apps: verified ones with an icon, the most recently updated
// first (maintained apps, in place of a popularity Flathub doesn't publish)
std::vector<const CatalogApp*> appsIn(std::shared_ptr<StoreState> st, const std::string& category, size_t max, bool verifiedOnly = true,
                                      const std::string& except = {}) {
    std::vector<const CatalogApp*> out;
    for (const auto& a : st->catalog) {
        if (verifiedOnly && !a.verified) continue;
        if (a.id == except || a.icon.empty() || a.screenshots.empty()) continue;
        if (!category.empty() && std::ranges::find(a.categories, category) == a.categories.end()) continue;
        if (std::ranges::find(kFeatured, a.id) != kFeatured.end() && category.empty()) continue;
        out.push_back(&a);
    }
    std::ranges::stable_sort(out, std::greater{}, &CatalogApp::released);
    if (out.size() > max) out.resize(max);
    return out;
}

std::string dateText(std::time_t t) {
    if (!t) return "";
    char b[32];
    std::strftime(b, sizeof b, "%-d %B %Y", std::localtime(&t));
    return b;
}

// ── Explore ──

// The banner: one featured app at a time, in its own colour
void hero(std::shared_ptr<StoreState> st, Flex* p) {
    std::vector<const CatalogApp*> apps;
    for (const auto& id : kFeatured)
        if (const auto* a = st->find(id); a && apps.size() < 6) apps.push_back(a);
    if (apps.empty()) return;
    auto* box = p->add<Flex>(Flex::Direction::Column);
    box->setAlignItems(Align::Stretch);
    auto fill = std::make_shared<std::function<void()>>();
    *fill = [st, box, apps, fill] {
        box->clearChildren();
        st->hero = (st->hero % static_cast<int>(apps.size()) + static_cast<int>(apps.size())) % static_cast<int>(apps.size());
        const CatalogApp& a = *apps[static_cast<size_t>(st->hero)];
        const Color c = tintOf(st, a.icon);
        auto* banner = box->add<Button>(Flex::Direction::Row);
        banner->setPadding({36, 30, 40, 30})->setSpacing(24)->setRadius(Radii(34))->setAlignItems(Align::Center);
        banner->setBackground(ColorRef(tone(c)), false);
        banner->pressedRadius = 26;
        auto* t = banner->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(8);
        t->grow = true;
        t->givesWay = true;
        auto* kicker = t->add<Flex>(Flex::Direction::Row);
        kicker->setSpacing(6)->setAlignItems(Align::Center);
        kicker->add<Icon>("auto_awesome", 16)->setFill(true)->setColor(ColorRef(accent(c)), false);
        kicker->add<Label>("FEATURED")->setSize(12)->setWeight(700)->setColor(ColorRef(accent(c)), false);
        auto* name = t->add<Flex>(Flex::Direction::Row);
        name->setSpacing(8)->setAlignItems(Align::Center);
        name->add<Label>(a.name)->setSize(34)->setWeight(500)->setMaxWidth(520);
        if (a.verified) verifiedMark(name, 22);
        t->add<Label>(a.summary)->setSize(16)->setMaxWidth(520)->setMaxLines(2);
        if (!a.developer.empty()) t->add<Label>(a.developer)->setSize(13)->setColor("on_surface_variant", false);
        auto* actions = t->add<Flex>(Flex::Direction::Row);
        actions->setSpacing(8)->setPadding({0, 8, 0, 0});
        actionButton(st, actions, a, true);
        appIcon(banner, a.icon, 148);
        banner->onClicked = [st, id = a.id](uint32_t b) {
            if (b == BTN_LEFT) openPage(st, id);
        };
        // Which one, and the way to the others
        auto* nav = box->add<Flex>(Flex::Direction::Row);
        nav->setSpacing(6)->setAlignItems(Align::Center)->setJustify(Align::Center)->setPadding({0, 10, 0, 0});
        iconButton(nav, "chevron_left", [st, fill] {
            --st->hero;
            (*fill)();
        }, 20);
        for (size_t i = 0; i < apps.size(); ++i) {
            const bool on = static_cast<int>(i) == st->hero;
            auto* dot = nav->add<Button>();
            dot->setRadius(Radii(4))->setBackground(on ? ColorRef("primary") : ColorRef("on_surface", 0.25f), false);
            dot->fixedWidth = on ? 24 : 8, dot->fixedHeight = 8;
            dot->onClicked = [st, fill, i](uint32_t b) {
                if (b != BTN_LEFT) return;
                st->hero = static_cast<int>(i);
                (*fill)();
            };
        }
        iconButton(nav, "chevron_right", [st, fill] {
            ++st->hero;
            (*fill)();
        }, 20);
    };
    st->heroNext = [st, fill] {
        ++st->hero;
        (*fill)();
    };
    (*fill)();
}

void categoryTiles(std::shared_ptr<StoreState> st, Flex* p) {
    sectionHead(p, "Categories");
    auto* grid = p->add<Flow>();
    grid->spacing = 10;
    for (const auto& c : kCategories) {
        const Color hue = *Color::fromHex(c.hue);
        auto* b = grid->add<Button>(Flex::Direction::Column);
        b->setPadding({18, 16})->setSpacing(14)->setRadius(Radii(24))->setAlignItems(Align::Start);
        b->setBackground(ColorRef(tone(hue)), false);
        b->fixedWidth = 182;
        b->pressedRadius = 16;
        b->add<Icon>(c.icon, 28)->setFill(true)->setColor(ColorRef(accent(hue)), false);
        b->add<Label>(c.name)->setSize(15)->setWeight(600);
        b->onClicked = [st, k = std::string(c.key)](uint32_t button) {
            if (button == BTN_LEFT) openCategory(st, k);
        };
    }
}

void exploreHome(std::shared_ptr<StoreState> st, Flex* p) {
    hero(st, p);
    std::vector<const CatalogApp*> picks;
    for (size_t i = 6; i < kFeatured.size(); ++i)
        if (const auto* a = st->find(kFeatured[i])) picks.push_back(a);
    shelf(st, p, "picks", "Editor's picks", "Apps people love", picks);
    shelf(st, p, "new", "New & updated", "Verified apps with a fresh release", appsIn(st, "", 16));
    categoryTiles(st, p);
    shelf(st, p, "games", "Games", "Verified on Flathub", appsIn(st, "Game", 16), "Game");
    shelf(st, p, "graphics", "Creative tools", "Draw, edit photos, make 3D", appsIn(st, "Graphics", 16), "Graphics");
    shelf(st, p, "office", "Get things done", "Documents, notes and email", appsIn(st, "Office", 16), "Office");
    shelf(st, p, "dev", "For developers", "Editors, IDEs and tools", appsIn(st, "Development", 16), "Development");
    shelf(st, p, "av", "Watch and listen", "Players, editors and streaming", appsIn(st, "AudioVideo", 16), "AudioVideo");
}

void categoryPage(std::shared_ptr<StoreState> st, Flex* p) {
    const Category* cat = categoryOf(st->category);
    const Color hue = cat ? *Color::fromHex(cat->hue) : Theme::get().color("primary");
    std::vector<const CatalogApp*> apps;
    for (const auto& a : st->catalog)
        if (std::ranges::find(a.categories, st->category) != a.categories.end()) apps.push_back(&a);
    if (st->alphabetical)
        std::ranges::sort(apps, [](const CatalogApp* a, const CatalogApp* b) { return lower(a->name) < lower(b->name); });

    auto* banner = p->add<Flex>(Flex::Direction::Row);
    banner->setPadding({20, 22, 32, 22})->setSpacing(16)->setRadius(Radii(32))->setAlignItems(Align::Center)->setBackground(ColorRef(tone(hue)), false);
    iconButton(banner, "arrow_back", [st] { openCategory(st, ""); }, 22)->tooltip = "Explore (Escape)";
    banner->add<Icon>(cat ? cat->icon : "apps", 44)->setFill(true)->setColor(ColorRef(accent(hue)), false);
    auto* t = banner->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(2);
    t->grow = true;
    t->add<Label>(cat ? cat->name : st->category)->setSize(30)->setWeight(500);
    t->add<Label>(std::format("{} apps", apps.size()))->setSize(14)->setColor("on_surface_variant", false);
    auto* sort = banner->add<Flex>(Flex::Direction::Row);
    sort->setSpacing(6);
    for (const bool alpha : {false, true}) {
        const bool on = st->alphabetical == alpha;
        auto* c = sort->add<Button>(Flex::Direction::Row);
        pill(c->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface", 0.5f), false))->setPadding({12, 7})->setSpacing(6)->setAlignItems(Align::Center);
        c->add<Icon>(alpha ? "sort_by_alpha" : "verified", 16)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
        c->add<Label>(alpha ? "A to Z" : "Verified first")->setSize(13)->setWeight(500)->setColor(on ? "on_secondary_container" : "on_surface", false);
        c->onClicked = [st, alpha](uint32_t b) {
            if (b != BTN_LEFT) return;
            st->alphabetical = alpha;
            refresh(st);
        };
    }

    auto* grid = p->add<Flow>();
    grid->spacing = 12;
    for (size_t i = 0; i < apps.size() && i < st->shown; ++i) appTile(st, grid, *apps[i]);
    if (apps.size() > st->shown) {
        auto* more = p->add<Flex>(Flex::Direction::Row);
        more->setJustify(Align::Center);
        pillButton(more, std::format("Show more  ·  {} left", apps.size() - st->shown), false, [st] {
            st->shown += 60;
            refresh(st);
        }, "expand_more");
    }
}

void nativeRow(std::shared_ptr<StoreState> st, Node* parent, const NativeHit& h) {
    auto* r = parent->add<Flex>(Flex::Direction::Row);
    r->setPadding({18, 12})->setSpacing(16)->setAlignItems(Align::Center);
    auto* glyph = r->add<Flex>(Flex::Direction::Row);
    glyph->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(14))->setBackground(h.source == Source::Aur ? "tertiary_container" : "secondary_container", false);
    glyph->fixedWidth = 44, glyph->fixedHeight = 44;
    glyph->add<Icon>(h.source == Source::Aur ? "deployed_code_account" : "deployed_code", 24)->setColor(h.source == Source::Aur ? "on_tertiary_container" : "on_secondary_container", false);
    auto* t = r->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(2);
    t->grow = true;
    t->givesWay = true;
    auto* nameRow = t->add<Flex>(Flex::Direction::Row);
    nameRow->setSpacing(8)->setAlignItems(Align::Center);
    nameRow->add<Label>(h.name)->setSize(15)->setWeight(600)->setMaxWidth(420);
    auto* badge = nameRow->add<Flex>(Flex::Direction::Row);
    pill(badge->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({8, 2});
    badge->add<Label>(h.repo + "  " + h.version)->setSize(11)->setWeight(500)->setColor("on_surface_variant", false)->setMaxWidth(220);
    t->add<Label>(h.description)->setSize(12)->setMaxWidth(600)->setMaxLines(2)->setColor("on_surface_variant", false);
    if (st->native.contains(h.name)) {
        pillButton(r, "Remove", false, [h] {
            if (safeName(h.name) && !Packages::get().busy()) Packages::get().uninstall(h.source, h.name);
        });
    } else {
        const bool noHelper = h.source == Source::Aur && Packages::get().aurHelper().empty();
        auto* b = pillButton(r, noHelper ? "Get yay first" : "Install", !noHelper, [h, noHelper] {
            if (!safeName(h.name) || Packages::get().busy()) return;
            if (noHelper) return Packages::get().installAurHelper();
            Packages::get().install({h.name, h.version, h.description, h.repo, h.source});
        });
        if (Packages::get().busy()) b->setOpacity(0.5f, false);
    }
}

void nativeList(std::shared_ptr<StoreState> st, Flex* p, const std::vector<NativeHit>& list, size_t max) {
    auto* g = p->add<Flex>(Flex::Direction::Column);
    g->setRadius(Radii(24))->setBackground(ColorRef("surface_container", 0.85f), false)->setAlignItems(Align::Stretch)->setPadding({0, 4});
    for (size_t i = 0; i < list.size() && i < max; ++i) nativeRow(st, g, list[i]);
}

void searchPage(std::shared_ptr<StoreState> st, Flex* p) {
    const std::string q = lower(st->query);
    std::vector<const CatalogApp*> hits;
    for (const auto& a : st->catalog)
        if (lower(a.name).find(q) != std::string::npos || lower(a.summary).find(q) != std::string::npos || a.keywords.find(q) != std::string::npos ||
            lower(a.id).find(q) != std::string::npos)
            hits.push_back(&a);
    std::ranges::stable_sort(hits, [&](const CatalogApp* a, const CatalogApp* b) {
        auto rank = [&](const CatalogApp* x) {
            const std::string n = lower(x->name);
            return n == q ? 0 : n.starts_with(q) ? 1 : n.find(q) != std::string::npos ? 2 : 3;
        };
        return rank(a) < rank(b);
    });
    const bool aur = Config::get().value<bool>("/store/aur", true);

    sectionHead(p, "Results for “" + st->query + "”");
    auto* chips = p->add<Flow>();
    chips->spacing = 6;
    struct F {
        const char* key;
        std::string label;
    };
    std::vector<F> filters = {{"all", "All"},
                              {"flathub", std::format("Flathub  {}", hits.size())},
                              {"repo", st->searchingRepo ? std::string("Repositories  …") : std::format("Repositories  {}", st->repoHits.size())}};
    if (aur) filters.push_back({"aur", st->searchingAur ? std::string("AUR  …") : std::format("AUR  {}", st->aurHits.size())});
    for (const auto& f : filters) {
        const bool on = st->filter == f.key;
        auto* c = chips->add<Button>(Flex::Direction::Row);
        pill(c->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_high"), false))->setPadding({14, 7})->setSpacing(4)->setAlignItems(Align::Center);
        if (on) c->add<Icon>("check", 16)->setColor("on_secondary_container", false);
        c->add<Label>(f.label)->setSize(13)->setWeight(500)->setColor(on ? "on_secondary_container" : "on_surface", false);
        c->onClicked = [st, k = std::string(f.key)](uint32_t b) {
            if (b != BTN_LEFT) return;
            st->filter = k;
            refresh(st);
        };
    }
    const bool all = st->filter == "all";
    if (all || st->filter == "flathub") {
        if (all) sectionHead(p, "Flathub", "Sandboxed apps, the same on every distribution");
        auto* grid = p->add<Flow>();
        grid->spacing = 12;
        const size_t max = all ? 8 : 60;
        for (size_t i = 0; i < hits.size() && i < max; ++i) appTile(st, grid, *hits[i]);
        if (hits.empty()) caption(p, "No Flathub app matches");
    }
    if (all || st->filter == "repo") {
        if (all) sectionHead(p, "Your system's repositories", "Arch Linux's packages, updated with the system");
        if (st->searchingRepo) caption(p, "Searching…");
        else if (st->repoHits.empty()) caption(p, "No package matches");
        else nativeList(st, p, st->repoHits, all ? 5 : 30);
    }
    if (aur && (all || st->filter == "aur")) {
        if (all) sectionHead(p, "The AUR", "Packages the community builds from source: check what you install");
        if (st->searchingAur) caption(p, "Searching…");
        else if (st->aurHits.empty()) caption(p, "No package matches (or the AUR is unreachable)");
        else nativeList(st, p, st->aurHits, all ? 5 : 20);
    }
}

// ── An app's page ──

void infoChip(Node* parent, const std::string& icon, const std::string& text, std::function<void()> fn = {}) {
    auto* c = parent->add<Button>(Flex::Direction::Row);
    pill(c->setBackground("surface_container_high", false))->setPadding({12, 7})->setSpacing(6)->setAlignItems(Align::Center);
    c->add<Icon>(icon, 16)->setColor("on_surface_variant", false);
    c->add<Label>(text)->setSize(13)->setWeight(500)->setMaxWidth(260);
    if (fn)
        c->onClicked = [fn](uint32_t b) {
            if (b == BTN_LEFT) fn();
        };
}

void appPage(std::shared_ptr<StoreState> st, Flex* p, const CatalogApp& a) {
    const Color c = tintOf(st, a.icon);
    // The head, in the app's colour
    auto* head = p->add<Flex>(Flex::Direction::Column);
    head->setPadding({20, 16, 32, 28})->setSpacing(10)->setRadius(Radii(34))->setAlignItems(Align::Stretch)->setBackground(ColorRef(tone(c)), false);
    auto* bar = head->add<Flex>(Flex::Direction::Row);
    bar->setAlignItems(Align::Center)->setSpacing(6);
    iconButton(bar, "arrow_back", [st] {
        st->page.clear();
        refresh(st);
    }, 22)->tooltip = "Back (Escape)";
    if (!a.categories.empty())
        if (const auto* cat = categoryOf(a.categories.front()))
            bar->add<Label>(cat->name)->setSize(13)->setWeight(500)->setColor("on_surface_variant", false);
    auto* main = head->add<Flex>(Flex::Direction::Row);
    main->setSpacing(28)->setAlignItems(Align::Center)->setPadding({16, 4, 0, 0});
    iconOrGlyph(main, a.icon, 128);
    auto* t = main->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(6);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(a.name)->setSize(34)->setWeight(500)->setMaxWidth(560);
    auto* dev = t->add<Flex>(Flex::Direction::Row);
    dev->setSpacing(6)->setAlignItems(Align::Center);
    if (!a.developer.empty()) dev->add<Label>(a.developer)->setSize(14)->setWeight(500)->setColor(ColorRef(accent(c)), false);
    if (a.verified) {
        verifiedMark(dev, 16);
        dev->add<Label>("Verified")->setSize(13)->setColor("on_surface_variant", false);
    }
    t->add<Label>(a.summary)->setSize(16)->setMaxWidth(560)->setMaxLines(2);
    // Its version: the newest release, and the one installed when they differ
    const auto mine = st->flatpakVersions.find(a.id);
    const bool installedHere = st->flatpaks.contains(a.id);
    if (!a.version.empty() || installedHere) {
        auto* ver = t->add<Flex>(Flex::Direction::Row);
        ver->setSpacing(8)->setAlignItems(Align::Center);
        ver->add<Icon>("new_releases", 16)->setColor("on_surface_variant", false);
        std::string text = a.version.empty() ? "" : "Version " + a.version;
        if (a.released) text += "  ·  released " + dateText(a.released);
        if (installedHere && mine != st->flatpakVersions.end() && !mine->second.empty() && mine->second != a.version)
            text += (text.empty() ? "" : "  ·  ") + std::string("you have ") + mine->second;
        ver->add<Label>(text)->setSize(13)->setMaxWidth(560)->setColor("on_surface_variant", false);
        if (installedHere && mine != st->flatpakVersions.end() && !a.version.empty() && !mine->second.empty() && mine->second != a.version) {
            auto* chip = ver->add<Button>(Flex::Direction::Row);
            pill(chip->setBackground("tertiary_container", false))->setPadding({10, 3})->setSpacing(4)->setAlignItems(Align::Center);
            chip->add<Icon>("upgrade", 14)->setColor("on_tertiary_container", false);
            chip->add<Label>("Update available")->setSize(11)->setWeight(600)->setColor("on_tertiary_container", false);
            chip->onClicked = [st](uint32_t b) {
                if (b != BTN_LEFT) return;
                st->tab = 2;
                st->page.clear();
                refresh(st);
            };
        }
    }
    auto* actions = t->add<Flex>(Flex::Direction::Row);
    actions->setSpacing(8)->setPadding({0, 10, 0, 0})->setAlignItems(Align::Center);
    actionButton(st, actions, a, true);
    if (st->flatpaks.contains(a.id))
        pillButton(actions, "Remove", false, [id = a.id] {
            if (safeName(id) && !Packages::get().busy()) Packages::get().uninstall(Source::Flatpak, id);
        }, "delete");
    if (!a.homepage.empty())
        iconButton(actions, "language", [url = a.homepage] { m3::run({"xdg-open", url}, nullptr); }, 22)->tooltip = "Website";
    if (const std::string b = busyLabel(); !b.empty()) caption(t, b);

    // At a glance
    auto* chips = p->add<Flow>();
    chips->spacing = 8;
    if (a.verified) infoChip(chips, "verified", "Verified publisher");
    infoChip(chips, "inventory_2", "Flathub");
    if (!a.version.empty()) infoChip(chips, "new_releases", "Version " + a.version);
    if (!a.license.empty()) infoChip(chips, "gavel", a.license.find("LicenseRef-proprietary") != std::string::npos ? "Proprietary" : a.license);
    for (const auto& k : a.categories)
        if (const auto* cat = categoryOf(k)) infoChip(chips, cat->icon, cat->name, [st, key = k] { openCategory(st, key); });

    // Screenshots: one large, the rest below
    if (!a.screenshots.empty()) {
        st->shot = std::clamp(st->shot, 0, static_cast<int>(a.screenshots.size()) - 1);
        auto* viewer = p->add<Flex>(Flex::Direction::Row);
        viewer->setPadding(Insets(12))->setSpacing(8)->setRadius(Radii(28))->setAlignItems(Align::Center)->setBackground(ColorRef("surface_container_lowest", 0.7f), false);
        const bool many = a.screenshots.size() > 1;
        auto* prev = iconButton(viewer, "chevron_left", [st] {
            if (st->shot > 0) --st->shot, refresh(st);
        }, 26);
        prev->setOpacity(many && st->shot > 0 ? 1.0f : 0.25f, false);
        const std::string url = a.screenshots[static_cast<size_t>(st->shot)];
        const std::string file = screenshot(st, url);
        if (file.empty()) {
            auto* ph = viewer->add<Flex>(Flex::Direction::Column);
            ph->setJustify(Align::Center)->setAlignItems(Align::Center)->setSpacing(8);
            ph->fixedHeight = 440;
            ph->grow = true;
            ph->add<Icon>("image", 40)->setColor("on_surface_variant", false);
            caption(ph, "Loading the screenshot…");
        } else {
            auto* v = viewer->add<ImageView>(780, 440, [file](float scale, int& w, int& h) -> GLuint {
                auto* tx = Images::get().picture(file, static_cast<int>(780 * scale), static_cast<int>(440 * scale), false);
                if (!tx) return 0;
                w = tx->width, h = tx->height;
                return tx->id;
            });
            v->radius = 14;
            v->grow = true;
        }
        auto* next = iconButton(viewer, "chevron_right", [st, n = a.screenshots.size()] {
            if (st->shot + 1 < static_cast<int>(n)) ++st->shot, refresh(st);
        }, 26);
        next->setOpacity(many && st->shot + 1 < static_cast<int>(a.screenshots.size()) ? 1.0f : 0.25f, false);
        if (many) {
            auto* thumbs = p->add<Flex>(Flex::Direction::Row);
            thumbs->setSpacing(10)->setJustify(Align::Center);
            for (size_t i = 0; i < a.screenshots.size(); ++i) {
                const std::string tf = screenshot(st, a.screenshots[i]);
                auto* b = thumbs->add<Button>();
                b->setPadding(Insets(3))->setRadius(Radii(12));
                if (static_cast<int>(i) == st->shot) b->setBorder(2, ColorRef("primary"));
                if (tf.empty()) {
                    auto* ph = b->add<Flex>(Flex::Direction::Row);
                    ph->setRadius(Radii(10))->setBackground("surface_container_high", false);
                    ph->fixedWidth = 112, ph->fixedHeight = 66;
                } else {
                    auto* v = b->add<ImageView>(112, 66, [tf](float scale, int& w, int& h) -> GLuint {
                        auto* tx = Images::get().picture(tf, static_cast<int>(112 * scale), static_cast<int>(66 * scale), true);
                        if (!tx) return 0;
                        w = tx->width, h = tx->height;
                        return tx->id;
                    });
                    v->cover = true;
                    v->radius = 10;
                    if (static_cast<int>(i) != st->shot) v->setOpacity(0.6f, false);
                }
                b->onClicked = [st, i](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    st->shot = static_cast<int>(i);
                    refresh(st);
                };
            }
        }
    }

    // What it is
    if (!a.description.empty()) {
        auto* d = p->add<Flex>(Flex::Direction::Column);
        d->setSpacing(8)->setAlignItems(Align::Start)->setPadding({4, 8, 4, 0});
        d->add<Label>("About this app")->setSize(22)->setWeight(500);
        const bool longText = a.description.size() > 520;
        auto* text = d->add<Label>(a.description);
        text->setSize(15)->setMaxWidth(820)->setMaxLines(longText && !st->readMore ? 6 : kAllLines)->setColor("on_surface", false);
        if (longText) {
            auto* more = d->add<Button>(Flex::Direction::Row);
            pill(more)->setPadding({12, 6})->setSpacing(4)->setAlignItems(Align::Center);
            more->add<Label>(st->readMore ? "Less" : "Read more")->setSize(14)->setWeight(600)->setColor("primary", false);
            more->add<Icon>(st->readMore ? "expand_less" : "expand_more", 18)->setColor("primary", false);
            more->onClicked = [st](uint32_t b) {
                if (b != BTN_LEFT) return;
                st->readMore = !st->readMore;
                refresh(st);
            };
        }
    }

    // More of its kind
    if (!a.categories.empty()) {
        std::string key;
        for (const auto& k : a.categories)
            if (categoryOf(k)) key = k;
        if (!key.empty()) shelf(st, p, "like:" + a.id, "More like this", "", appsIn(st, key, 12, true, a.id), key);
    }

    auto* info = group(p, "Details");
    auto row = [&](const char* icon, const std::string& title, const std::string& value) {
        auto* r = info->add<Flex>(Flex::Direction::Row);
        r->setPadding({16, 10})->setSpacing(14)->setAlignItems(Align::Center);
        r->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
        auto* l = r->add<Label>(title);
        l->setSize(14);
        l->grow = true;
        r->add<Label>(value)->setSize(13)->setMaxWidth(460)->setColor("on_surface_variant", false);
    };
    row("inventory_2", "Source", "Flathub (Flatpak, sandboxed)");
    row("badge", "App ID", a.id);
    if (!a.version.empty()) row("new_releases", "Latest version", a.version);
    if (a.released) row("event", "Released", dateText(a.released));
    if (installedHere) row("download_done", "Installed version", mine != st->flatpakVersions.end() && !mine->second.empty() ? mine->second : "Installed");
    if (!a.developer.empty()) row("person", "Developer", a.developer);
    if (!a.license.empty()) row("gavel", "License", a.license);
    if (!a.homepage.empty()) row("language", "Website", a.homepage);
}

// ── Installed, Updates ──

// A page's summary: a glyph in a tonal circle, what's what, its buttons
Flex* summaryCard(Flex* p, const std::string& icon, const std::string& title, const std::string& sub) {
    auto* card = p->add<Flex>(Flex::Direction::Row);
    card->setPadding({24, 22})->setSpacing(18)->setRadius(Radii(32))->setAlignItems(Align::Center)->setBackground("primary_container", false);
    auto* glyph = card->add<Flex>(Flex::Direction::Row);
    glyph->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(28))->setBackground(ColorRef("on_primary_container", 0.12f), false);
    glyph->fixedWidth = 56, glyph->fixedHeight = 56;
    glyph->add<Icon>(icon, 30)->setFill(true)->setColor("on_primary_container", false);
    auto* t = card->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(2);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(title)->setSize(24)->setWeight(500)->setColor("on_primary_container", false);
    if (!sub.empty()) t->add<Label>(sub)->setSize(13)->setMaxWidth(560)->setColor("on_primary_container", false);
    return card;
}

void installedPage(std::shared_ptr<StoreState> st, Flex* p) {
    std::vector<Apps::App> apps;
    for (const auto& a : Apps::get().all())
        if (a.path.starts_with("/usr/share/applications/") && a.path.find("material3ui") == std::string::npos) apps.push_back(a);
    std::ranges::sort(apps, [](const Apps::App& a, const Apps::App& b) { return lower(a.name) < lower(b.name); });
    summaryCard(p, "apps", std::format("{} apps installed", st->flatpaks.size() + apps.size()),
                std::format("{} from Flathub, {} from your system's packages", st->flatpaks.size(), apps.size()));

    sectionHead(p, "From Flathub");
    if (st->flatpaks.empty()) caption(p, "None yet: Explore has thousands");
    auto* grid = p->add<Flow>();
    grid->spacing = 12;
    for (const auto& id : st->flatpaks) {
        if (const auto* a = st->find(id)) appTile(st, grid, *a);
        else {
            CatalogApp x;
            x.id = x.name = id;
            x.summary = "Flatpak";
            appTile(st, grid, x);
        }
    }

    sectionHead(p, "From your system", "Arch Linux packages with an app");
    auto* g = p->add<Flex>(Flex::Direction::Column);
    g->setRadius(Radii(24))->setBackground(ColorRef("surface_container", 0.85f), false)->setAlignItems(Align::Stretch)->setPadding({0, 4});
    for (const auto& a : apps) {
        auto* r = g->add<Flex>(Flex::Direction::Row);
        r->setPadding({18, 10})->setSpacing(16)->setAlignItems(Align::Center);
        appIcon(r, a.icon, 40);
        auto* t = r->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(1);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(a.name)->setSize(15)->setWeight(500)->setMaxWidth(440);
        if (!a.comment.empty()) t->add<Label>(a.comment)->setSize(12)->setMaxWidth(520)->setColor("on_surface_variant", false);
        pillButton(r, "Open", false, [a] { Apps::get().launch(a); });
        Button* rm = iconButton(r, "delete", [] {}, 20);
        rm->tooltip = "Remove";
        rm->onClicked = [st, rm, path = a.path, name = a.name](uint32_t b) {
            if (b != BTN_LEFT) return;
            // Its package, asked of pacman, then asked of you
            m3::run({"pacman", "-Qqo", path}, [st, rm, name](int code, const std::string& out) {
                if (!*st->alive) return;
                const std::string pkg = out.substr(0, out.find('\n'));
                if (code != 0 || !safeName(pkg)) return void(m3::run({"notify-send", "-a", "Store", "No package owns " + name}, nullptr));
                floatingMenu(rm, {MenuItem("delete", "Remove " + name, "the " + pkg + " package", [pkg] {
                                      if (!Packages::get().busy()) Packages::get().uninstall(Source::Repo, pkg);
                                  }, false, true)});
            });
        };
    }
}

void updatesPage(std::shared_ptr<StoreState> st, Flex* p) {
    auto& pk = Packages::get();
    const auto& ups = pk.updates();
    std::string sub = pk.lastChecked() ? "Checked " + fileops::humanTime(pk.lastChecked()) : "Not checked yet";
    if (const std::string b = busyLabel(); !b.empty()) sub = b;
    auto* card = summaryCard(p, pk.checking() ? "sync" : ups.empty() ? "check_circle" : "system_update",
                             pk.checking() ? "Checking for updates…" : ups.empty() ? "You're up to date" : std::format("{} update{} ready", ups.size(), ups.size() == 1 ? "" : "s"), sub);
    pillButton(card, "Check", false, [] { Packages::get().check(); }, "refresh");
    if (!ups.empty()) {
        auto* u = pillButton(card, "Update all", true, [] {
            if (!Packages::get().busy()) Packages::get().upgrade();
        }, "system_update");
        if (pk.busy()) u->setOpacity(0.5f, false);
    }
    if (ups.empty()) return;
    // By where they come from
    for (const auto& [source, title] : {std::pair{Source::Shell, "Material3UI"}, {Source::Flatpak, "Flathub"}, {Source::Repo, "System packages"}, {Source::Aur, "The AUR"}}) {
        std::vector<const Packages::Update*> list;
        for (const auto& u : ups)
            if (u.source == source) list.push_back(&u);
        if (list.empty()) continue;
        sectionHead(p, std::format("{}  ·  {}", title, list.size()));
        auto* g = p->add<Flex>(Flex::Direction::Column);
        g->setRadius(Radii(24))->setBackground(ColorRef("surface_container", 0.85f), false)->setAlignItems(Align::Stretch)->setPadding({0, 4});
        for (const auto* u : list) {
            auto* r = g->add<Flex>(Flex::Direction::Row);
            r->setPadding({18, 10})->setSpacing(16)->setAlignItems(Align::Center);
            const CatalogApp* a = source == Source::Flatpak ? st->find(u->name) : nullptr;
            if (a) iconOrGlyph(r, a->icon, 36);
            else r->add<Icon>(source == Source::Aur ? "deployed_code_account" : source == Source::Shell ? "auto_awesome" : "deployed_code", 26)->setColor("on_surface_variant", false);
            auto* l = r->add<Label>(a ? a->name : u->name);
            l->setSize(15)->setWeight(500)->setMaxWidth(380);
            l->grow = true;
            auto* v = r->add<Flex>(Flex::Direction::Row);
            pill(v->setBackground(ColorRef("on_surface", 0.06f), false))->setPadding({10, 4});
            v->add<Label>(u->from.empty() ? u->to : u->from + "  →  " + u->to)->setSize(12)->setMaxWidth(320)->setColor("on_surface_variant", false);
        }
    }
}

// ── The rail and the page ──

void railItem(std::shared_ptr<StoreState> st, Flex* rail, int index, const char* icon, const char* label, int badge) {
    const bool on = st->tab == index && st->page.empty() && (index != 0 || true);
    auto* b = rail->add<Button>(Flex::Direction::Column);
    b->setPadding({0, 6})->setSpacing(4)->setAlignItems(Align::Center);
    b->fixedWidth = 84;
    auto* ind = b->add<Flex>(Flex::Direction::Row);
    ind->setJustify(Align::Center)->setAlignItems(Align::Center)->setSpacing(4)->setRadius(Radii(16));
    ind->setBackground(on ? ColorRef("secondary_container") : ColorRef("on_surface", 0.0f), false);
    ind->fixedWidth = 58, ind->fixedHeight = 32;
    ind->add<Icon>(icon, 22)->setFill(on)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
    if (badge > 0) {
        auto* dot = ind->add<Flex>(Flex::Direction::Row);
        pill(dot->setBackground("error", false))->setPadding({5, 0});
        dot->add<Label>(badge > 99 ? "99+" : std::to_string(badge))->setSize(10)->setWeight(700)->setColor("on_error", false);
    }
    b->add<Label>(label)->setSize(12)->setWeight(on ? 700 : 500)->setColor(on ? "on_surface" : "on_surface_variant", false);
    b->onClicked = [st0 = st, index](uint32_t button) {
        if (button != BTN_LEFT) return;
        // A copy: the rebuild below destroys this button and what it holds
        auto st = st0;
        st->tab = index;
        st->page.clear();
        if (index == 0) st->category.clear(), st->query.clear(), st->search->setText("");
        refresh(st);
        if (st->scroll) st->scroll->scrollTo(0, false);
    };
}

void build(std::shared_ptr<StoreState> st) {
    if (!Packages::get().busy()) st->installing.clear();
    auto* r = st->rail;
    r->clearChildren();
    r->add<Node>()->fixedHeight = 6;
    railItem(st, r, 0, "storefront", "Explore", 0);
    railItem(st, r, 1, "apps", "Installed", 0);
    railItem(st, r, 2, "system_update", "Updates", static_cast<int>(Packages::get().updates().size()));

    st->heroNext = nullptr;
    auto* p = st->body;
    p->clearChildren();
    if (!st->page.empty() && st->loaded) {
        if (const auto* a = st->find(st->page)) return appPage(st, p, *a);
        st->page.clear();
    }
    if (st->tab == 1) return installedPage(st, p);
    if (st->tab == 2) return updatesPage(st, p);
    if (!st->loaded) {
        auto* none = p->add<Flex>(Flex::Direction::Column);
        none->setAlignItems(Align::Center)->setSpacing(12)->setPadding({0, 140});
        none->add<Icon>("storefront", 64)->setColor("primary", false);
        none->add<Label>("Opening the store…")->setSize(18)->setColor("on_surface_variant", false);
        return;
    }
    if (!st->query.empty()) return searchPage(st, p);
    if (!st->category.empty()) return categoryPage(st, p);
    exploreHome(st, p);
}

} // namespace

std::function<void(const std::string&)> storeGo;

void storeApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<StoreState>();
    card->setPadding({6, 6, 10, 10})->setSpacing(0);
    card->add<StoreKeeper>(st);

    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setAlignItems(Align::Stretch)->setSpacing(8);
    columns->grow = true;

    // The rail: Explore, Installed, Updates
    st->rail = columns->add<Flex>(Flex::Direction::Column);
    st->rail->setSpacing(14)->setAlignItems(Align::Center);
    st->rail->fixedWidth = 88;

    auto* main = columns->add<Flex>(Flex::Direction::Column);
    main->setAlignItems(Align::Stretch)->setSpacing(10);
    main->grow = true;

    // Search, wide and first
    auto* top = main->add<Flex>(Flex::Direction::Row);
    top->setJustify(Align::Center)->setPadding({0, 2});
    auto* searchBox = top->add<Flex>(Flex::Direction::Row);
    pill(searchBox->setBackground("surface_container_high", false))->setPadding({20, 8})->setSpacing(12)->setAlignItems(Align::Center);
    searchBox->fixedWidth = 560;
    searchBox->add<Icon>("search", 22)->setColor("on_surface_variant", false);
    st->search = searchBox->add<TextField>();
    st->search->bare = true;
    st->search->grow = true;
    st->search->setPlaceholder("Search apps, games and packages");
    auto* clear = iconButton(searchBox, "close", [st] {
        st->search->setText("");
        st->query.clear();
        refresh(st);
    }, 18);
    clear->tooltip = "Clear";
    clear->setVisible(false);
    st->search->onChanged = [st, clear](const std::string& text) {
        clear->setVisible(!text.empty());
        st->query = text;
        st->tab = 0;
        st->page.clear();
        st->filter = "all";
        EventLoop::get().debounce(st->searchTimer, 300, [st] {
            if (!*st->alive) return;
            searchNative(st);
            refresh(st);
            if (st->scroll) st->scroll->scrollTo(0, false);
        });
    };

    st->scroll = main->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->scroll->blurEdges = true;
    st->body = st->scroll->add<Flex>(Flex::Direction::Column);
    st->body->setSpacing(16)->setAlignItems(Align::Stretch)->setPadding({6, 4, 10, 24});
    st->show = [st] { build(st); };

    // Only when what's shown changes: a task starting or ending, an update
    // check's count (the page isn't rebuilt under the pointer otherwise)
    st->conns.push_back(Packages::get().changed.connect([st] {
        auto& pk = Packages::get();
        const std::string sig = std::format("{}|{}|{}|{}", pk.busy(), pk.status(), pk.checking(), pk.updates().size());
        if (sig == st->packagesSig) return;
        const bool finished = st->wasBusy && !pk.busy();
        st->wasBusy = pk.busy();
        st->packagesSig = sig;
        EventLoop::get().debounce(st->refreshTimer, 150, [st, finished] {
            if (!*st->alive) return;
            if (finished) readInstalled(st);
            refresh(st);
        });
    }));
    // The banner turns every eight seconds
    st->heroTimer = EventLoop::get().addTimer(8000, [st] {
        if (*st->alive && st->heroNext) st->heroNext();
    }, true);

    popup.onKey = [st](const KeyEvent& e) {
        if (e.ctrl() && (e.sym == XKB_KEY_f || e.sym == XKB_KEY_F)) return st->search->focus(), true;
        if (e.sym == XKB_KEY_Escape) {
            if (!st->page.empty()) st->page.clear();
            else if (!st->category.empty()) st->category.clear();
            else return false;
            refresh(st);
            return true;
        }
        return false;
    };
    // "search:<words>": a search, as if typed
    auto startSearch = [st, clear](const std::string& q) {
        clear->setVisible(!q.empty());
        st->query = q;
        st->tab = 0;
        st->page.clear();
        st->filter = "all";
        st->search->setText(q);
        searchNative(st);
    };
    storeGo = [st, startSearch](const std::string& what) {
        if (!*st->alive) return;
        if (what.starts_with("search:")) startSearch(what.substr(7));
        else if (what.starts_with("category:")) st->tab = 0, st->category = what.substr(9), st->page.clear();
        else if (what == "updates") st->tab = 2, st->page.clear();
        else if (!what.empty()) {
            st->tab = 0;
            return openPage(st, what);
        }
        refresh(st);
        if (st->scroll) st->scroll->scrollTo(0, false);
    };

    // The catalogue, read in the background (Flathub's is large)
    auto alive = st->alive;
    auto result = std::make_shared<std::vector<CatalogApp>>();
    std::thread([alive, result, st] {
        *result = parseCatalog();
        EventLoop::get().post([alive, result, st] {
            if (!*alive) return;
            st->catalog = std::move(*result);
            st->loaded = true;
            refresh(st);
        });
    }).detach();

    if (arg.starts_with("search:")) startSearch(arg.substr(7));
    else if (arg.starts_with("category:")) st->category = arg.substr(9);
    else if (arg == "updates") st->tab = 2;
    else if (!arg.empty()) st->page = arg;
    readInstalled(st);
    if (Packages::get().updates().empty() && !Packages::get().checking()) Packages::get().check();
    build(st);
}

void storeClosed() {
    storeGo = nullptr;
}

// Settings → Store
void storeSettings(Flex* page) {
    auto* g = group(page, "Searching");
    switchRow(g, "deployed_code_account", "Search the AUR", "Packages the community builds from source, beside Flathub and your repositories", "/store/aur", true);
    caption(page, "  Updates, repositories and mirrors are in Settings → Software & updates");
}

} // namespace m3::shell::panels
