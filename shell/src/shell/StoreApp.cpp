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

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
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

const std::vector<std::string> kFeatured = {"org.mozilla.firefox",   "com.spotify.Client",     "org.videolan.VLC",      "com.obsproject.Studio",
                                            "org.libreoffice.LibreOffice", "org.gimp.GIMP",     "com.discordapp.Discord", "com.valvesoftware.Steam",
                                            "org.blender.Blender",   "org.inkscape.Inkscape",  "org.kde.krita",         "org.telegram.desktop",
                                            "org.mozilla.Thunderbird", "org.audacityteam.Audacity", "org.kde.kdenlive",  "com.visualstudio.code"};

struct Category {
    const char* key;
    const char* name;
    const char* icon;
};
const Category kCategories[] = {
    {"", "Featured", "star"},          {"AudioVideo", "Audio & video", "movie"}, {"Development", "Developer tools", "code"},
    {"Education", "Education", "school"}, {"Game", "Games", "sports_esports"}, {"Graphics", "Graphics & photos", "palette"},
    {"Network", "Internet", "language"}, {"Office", "Productivity", "work"},  {"Science", "Science", "science"},
    {"System", "System", "settings"},  {"Utility", "Utilities", "build"},
};

// ── The store's state ──

struct NativeHit {
    std::string name, version, description, repo;
    Source source;
};

struct StoreState {
    std::vector<CatalogApp> catalog;
    bool loaded = false;
    std::set<std::string> flatpaks, native; // installed
    int tab = 0;                            // 0 Explore, 1 Installed, 2 Updates
    std::string category, query, page;      // page: an app's id (its page shown)
    std::vector<NativeHit> repoHits, aurHits;
    bool searchingRepo = false, searchingAur = false;
    int token = 0;
    std::map<std::string, std::string> shots; // url → file (fetched)
    std::set<std::string> fetching;
    Flex *tabs = nullptr, *body = nullptr;
    ScrollView* scroll = nullptr;
    TextField* search = nullptr;
    Label* status = nullptr;
    std::function<void()> show;
    EventLoop::TimerId searchTimer = 0, refreshTimer = 0;
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
        if (m_st->searchTimer) EventLoop::get().cancel(m_st->searchTimer);
        if (m_st->refreshTimer) EventLoop::get().cancel(m_st->refreshTimer);
        m_st->conns.clear();
        m_st->show = nullptr;
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
    m3::run({"flatpak", "list", "--app", "--columns=application"}, [st](int, const std::string& out) {
        if (!*st->alive) return;
        std::set<std::string> now;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);)
            if (!l.empty()) now.insert(l);
        if (now == st->flatpaks) return;
        st->flatpaks = std::move(now);
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

// Install, Open, Remove: for a Flathub app
void flatpakButtons(std::shared_ptr<StoreState> st, Node* row, const CatalogApp& a, bool big) {
    const bool here = st->flatpaks.contains(a.id);
    const bool busy = Packages::get().busy();
    if (here) {
        pillButton(row, "Open", true, [id = a.id] { openApp(id); }, big ? "open_in_new" : "");
        if (big)
            pillButton(row, "Remove", false, [st, id = a.id] {
                if (!safeName(id) || Packages::get().busy()) return;
                Packages::get().uninstall(Source::Flatpak, id);
            }, "delete");
    } else {
        auto* b = pillButton(row, busy ? "Busy…" : "Install", !busy, [st, id = a.id] {
            if (!safeName(id) || Packages::get().busy()) return;
            Packages::get().install({id, "", "", "flathub", Source::Flatpak});
        }, big ? "download" : "");
        if (busy) b->setOpacity(0.5f, false);
    }
}

// A card: icon, name, summary; a click opens its page
void appCard(std::shared_ptr<StoreState> st, Node* parent, const CatalogApp& a, bool wide = false) {
    auto* b = parent->add<Button>(Flex::Direction::Row);
    b->setPadding(Insets(14))->setSpacing(14)->setRadius(Radii(22))->setAlignItems(Align::Center);
    b->setBackground(ColorRef("surface_container", 0.8f), false);
    b->fixedWidth = wide ? 398 : 262;
    b->pressedRadius = 14;
    iconOrGlyph(b, a.icon, wide ? 64 : 52);
    auto* t = b->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(3);
    t->grow = true;
    t->givesWay = true;
    auto* nameRow = t->add<Flex>(Flex::Direction::Row);
    nameRow->setSpacing(4)->setAlignItems(Align::Center);
    nameRow->add<Label>(a.name)->setSize(wide ? 16 : 15)->setWeight(600)->setMaxWidth(wide ? 270 : 160);
    if (a.verified) nameRow->add<Icon>("verified", 15)->setFill(true)->setColor("primary", false);
    t->add<Label>(a.summary)->setSize(12)->setMaxWidth(wide ? 280 : 166)->setMaxLines(2)->setColor("on_surface_variant", false);
    if (st->flatpaks.contains(a.id)) {
        auto* chip = t->add<Flex>(Flex::Direction::Row);
        chip->setSpacing(3)->setAlignItems(Align::Center);
        chip->add<Icon>("check", 13)->setColor("primary", false);
        chip->add<Label>("Installed")->setSize(11)->setWeight(500)->setColor("primary", false);
    }
    b->onClicked = [st, id = a.id](uint32_t button) {
        if (button != BTN_LEFT) return;
        st->page = id;
        refresh(st);
    };
}

// ── Pages ──

void appPage(std::shared_ptr<StoreState> st, Flex* p, const CatalogApp& a) {
    auto* bar = p->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(8)->setAlignItems(Align::Center);
    iconButton(bar, "arrow_back", [st] {
        st->page.clear();
        refresh(st);
    }, 22)->tooltip = "Back (Escape)";

    auto* head = p->add<Flex>(Flex::Direction::Row);
    head->setSpacing(20)->setAlignItems(Align::Center)->setPadding({8, 4});
    iconOrGlyph(head, a.icon, 96);
    auto* t = head->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(4);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(a.name)->setSize(28)->setWeight(500)->setMaxWidth(520);
    auto* dev = t->add<Flex>(Flex::Direction::Row);
    dev->setSpacing(6)->setAlignItems(Align::Center);
    if (!a.developer.empty()) dev->add<Label>(a.developer)->setSize(13)->setColor("on_surface_variant", false);
    if (a.verified) {
        dev->add<Icon>("verified", 15)->setFill(true)->setColor("primary", false);
        dev->add<Label>("Verified")->setSize(12)->setWeight(500)->setColor("primary", false);
    }
    t->add<Label>(a.summary)->setSize(14)->setMaxWidth(520)->setMaxLines(2);
    auto* actions = t->add<Flex>(Flex::Direction::Row);
    actions->setSpacing(8)->setPadding({0, 6, 0, 0});
    flatpakButtons(st, actions, a, true);
    if (const std::string b = busyLabel(); !b.empty()) caption(t, b);

    // Screenshots, side by side
    if (!a.screenshots.empty()) {
        auto* row = p->add<Flow>();
        row->spacing = 10;
        for (const auto& url : a.screenshots) {
            const std::string file = screenshot(st, url);
            if (file.empty()) {
                auto* ph = row->add<Flex>(Flex::Direction::Row);
                ph->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(16))->setBackground("surface_container_high", false);
                ph->fixedWidth = 372, ph->fixedHeight = 232;
                ph->add<Icon>("image", 32)->setColor("on_surface_variant", false);
                continue;
            }
            auto* v = row->add<ImageView>(372, 232, [file](float scale, int& w, int& h) -> GLuint {
                auto* tx = Images::get().picture(file, static_cast<int>(372 * scale), static_cast<int>(232 * scale), false);
                if (!tx) return 0;
                w = tx->width, h = tx->height;
                return tx->id;
            });
            v->radius = 16;
        }
    }

    if (!a.description.empty()) {
        auto* d = p->add<Flex>(Flex::Direction::Column);
        d->setSpacing(6)->setAlignItems(Align::Start)->setPadding({4, 0});
        d->add<Label>("About this app")->setSize(16)->setWeight(600);
        d->add<Label>(a.description)->setSize(14)->setMaxWidth(760)->setMaxLines(kAllLines)->setColor("on_surface", false);
    }

    auto* info = group(p, "Details");
    auto row = [&](const char* icon, const std::string& title, const std::string& value) {
        auto* r = info->add<Flex>(Flex::Direction::Row);
        r->setPadding({16, 10})->setSpacing(14)->setAlignItems(Align::Center);
        r->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
        auto* l = r->add<Label>(title);
        l->setSize(14);
        l->grow = true;
        r->add<Label>(value)->setSize(13)->setMaxWidth(420)->setColor("on_surface_variant", false);
    };
    row("inventory_2", "Source", "Flathub (Flatpak)");
    row("badge", "App ID", a.id);
    if (!a.license.empty()) row("gavel", "License", a.license);
    if (!a.homepage.empty()) {
        auto* r = info->add<Button>(Flex::Direction::Row);
        r->setPadding({16, 10})->setSpacing(14)->setAlignItems(Align::Center);
        r->add<Icon>("language", 20)->setColor("on_surface_variant", false);
        auto* l = r->add<Label>("Website");
        l->setSize(14);
        l->grow = true;
        r->add<Label>(a.homepage)->setSize(13)->setMaxWidth(420)->setColor("primary", false);
        r->onClicked = [url = a.homepage](uint32_t b) {
            if (b == BTN_LEFT) m3::run({"xdg-open", url}, nullptr);
        };
    }
}

void nativeRow(std::shared_ptr<StoreState> st, Node* parent, const NativeHit& h) {
    auto* r = parent->add<Flex>(Flex::Direction::Row);
    r->setPadding({16, 10})->setSpacing(14)->setAlignItems(Align::Center);
    r->add<Icon>(h.source == Source::Aur ? "deployed_code_account" : "deployed_code", 26)->setColor("on_surface_variant", false);
    auto* t = r->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(1);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(h.name)->setSize(14)->setWeight(500)->setMaxWidth(480);
    t->add<Label>(std::format("{}  ·  {}  ·  {}", h.repo, h.version, h.description))->setSize(12)->setMaxWidth(560)->setMaxLines(2)->setColor("on_surface_variant", false);
    const bool here = st->native.contains(h.name);
    if (here) {
        pillButton(r, "Remove", false, [h] {
            if (safeName(h.name) && !Packages::get().busy()) Packages::get().uninstall(h.source, h.name);
        });
    } else {
        const bool noHelper = h.source == Source::Aur && Packages::get().aurHelper().empty();
        auto* b = pillButton(r, noHelper ? "Needs yay" : "Install", !noHelper, [h, noHelper] {
            if (!safeName(h.name) || Packages::get().busy()) return;
            if (noHelper) return Packages::get().installAurHelper();
            Packages::get().install({h.name, h.version, h.description, h.repo, h.source});
        });
        if (Packages::get().busy()) b->setOpacity(0.5f, false);
    }
}

void explore(std::shared_ptr<StoreState> st, Flex* p) {
    if (!st->loaded) {
        auto* none = p->add<Flex>(Flex::Direction::Column);
        none->setAlignItems(Align::Center)->setSpacing(12)->setPadding({0, 120});
        none->add<Icon>("storefront", 56)->setColor("on_surface_variant", false);
        none->add<Label>("Reading the catalogue…")->setSize(16)->setColor("on_surface_variant", false);
        return;
    }
    const std::string q = lower(st->query);
    if (!q.empty()) {
        // Flathub's apps that match, then the repositories and the AUR
        std::vector<const CatalogApp*> hits;
        for (const auto& a : st->catalog) {
            const std::string n = lower(a.name);
            if (n.find(q) != std::string::npos || lower(a.summary).find(q) != std::string::npos || a.keywords.find(q) != std::string::npos ||
                lower(a.id).find(q) != std::string::npos)
                hits.push_back(&a);
        }
        std::ranges::stable_sort(hits, [&](const CatalogApp* a, const CatalogApp* b) {
            auto rank = [&](const CatalogApp* x) {
                const std::string n = lower(x->name);
                return n == q ? 0 : n.starts_with(q) ? 1 : n.find(q) != std::string::npos ? 2 : 3;
            };
            return rank(a) < rank(b);
        });
        p->add<Label>(std::format("Flathub  ·  {}", hits.size()))->setSize(15)->setWeight(600);
        auto* flow = p->add<Flow>();
        flow->spacing = 10;
        for (size_t i = 0; i < hits.size() && i < 24; ++i) appCard(st, flow, *hits[i]);
        if (hits.empty()) caption(p, "No Flathub app matches");
        auto native = [&](const char* title, const std::vector<NativeHit>& list, bool searching) {
            p->add<Label>(std::format("{}{}", title, searching ? "  ·  searching…" : list.empty() ? "" : std::format("  ·  {}", list.size())))->setSize(15)->setWeight(600);
            if (list.empty() && !searching) {
                caption(p, "Nothing here");
                return;
            }
            auto* g = p->add<Flex>(Flex::Direction::Column);
            g->setRadius(Radii(20))->setBackground(ColorRef("surface_container", 0.8f), false)->setAlignItems(Align::Stretch);
            for (const auto& h : list) nativeRow(st, g, h);
        };
        native("Your system's repositories", st->repoHits, st->searchingRepo);
        if (Config::get().value<bool>("/store/aur", true)) native("The AUR (packages built from source by the community)", st->aurHits, st->searchingAur);
        return;
    }

    // Categories
    auto* cats = p->add<Flow>();
    cats->spacing = 6;
    for (const auto& c : kCategories) {
        const bool on = st->category == c.key;
        auto* b = cats->add<Button>(Flex::Direction::Row);
        pill(b->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_high"), false))->setPadding({12, 7, 14, 7})->setSpacing(6)->setAlignItems(Align::Center);
        b->add<Icon>(c.icon, 16)->setFill(on)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
        b->add<Label>(c.name)->setSize(13)->setWeight(500)->setColor(on ? "on_secondary_container" : "on_surface", false);
        b->onClicked = [st, k = std::string(c.key)](uint32_t button) {
            if (button != BTN_LEFT) return;
            st->category = k;
            refresh(st);
            if (st->scroll) st->scroll->scrollTo(0, false);
        };
    }
    if (st->category.empty()) {
        p->add<Label>("Featured")->setSize(20)->setWeight(500);
        auto* flow = p->add<Flow>();
        flow->spacing = 12;
        for (const auto& id : kFeatured)
            if (const auto* a = st->find(id)) appCard(st, flow, *a, true);
        p->add<Label>("Verified on Flathub")->setSize(20)->setWeight(500);
        auto* more = p->add<Flow>();
        more->spacing = 10;
        int n = 0;
        for (const auto& a : st->catalog) {
            if (!a.verified || std::ranges::find(kFeatured, a.id) != kFeatured.end()) continue;
            if (++n > 45) break;
            appCard(st, more, a);
        }
        return;
    }
    const Category* cat = nullptr;
    for (const auto& c : kCategories)
        if (st->category == c.key) cat = &c;
    p->add<Label>(cat ? cat->name : st->category)->setSize(20)->setWeight(500);
    auto* flow = p->add<Flow>();
    flow->spacing = 10;
    int n = 0, total = 0;
    for (const auto& a : st->catalog) {
        if (std::ranges::find(a.categories, st->category) == a.categories.end()) continue;
        ++total;
        if (++n > 120) continue;
        appCard(st, flow, a);
    }
    if (total > 120) caption(p, std::format("The first 120 of {}: search to find the others", total));
}

void installedPage(std::shared_ptr<StoreState> st, Flex* p) {
    p->add<Label>("Flatpak apps")->setSize(18)->setWeight(500);
    if (st->flatpaks.empty()) caption(p, "None yet: Explore has thousands");
    auto* flow = p->add<Flow>();
    flow->spacing = 10;
    for (const auto& id : st->flatpaks) {
        if (const auto* a = st->find(id)) appCard(st, flow, *a);
        else {
            CatalogApp x;
            x.id = x.name = id;
            x.summary = "Flatpak";
            appCard(st, flow, x);
        }
    }
    // The system's own desktop apps (pacman): each removable with its package
    p->add<Label>("System apps")->setSize(18)->setWeight(500);
    auto* g = p->add<Flex>(Flex::Direction::Column);
    g->setRadius(Radii(20))->setBackground(ColorRef("surface_container", 0.8f), false)->setAlignItems(Align::Stretch);
    std::vector<Apps::App> apps;
    for (const auto& a : Apps::get().all())
        if (a.path.starts_with("/usr/share/applications/") && a.path.find("material3ui") == std::string::npos) apps.push_back(a);
    std::ranges::sort(apps, [](const Apps::App& a, const Apps::App& b) { return lower(a.name) < lower(b.name); });
    for (const auto& a : apps) {
        auto* r = g->add<Flex>(Flex::Direction::Row);
        r->setPadding({16, 8})->setSpacing(14)->setAlignItems(Align::Center);
        appIcon(r, a.icon, 32);
        auto* t = r->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(a.name)->setSize(14)->setWeight(500)->setMaxWidth(420);
        if (!a.comment.empty()) t->add<Label>(a.comment)->setSize(12)->setMaxWidth(480)->setColor("on_surface_variant", false);
        pillButton(r, "Open", false, [a] { Apps::get().launch(a); });
        Button* rm = iconButton(r, "delete", [] {}, 20);
        rm->tooltip = "Remove";
        rm->onClicked = [st, rm, path = a.path, name = a.name](uint32_t b) {
            if (b != BTN_LEFT) return;
            // Its package, asked of pacman, then asked of you
            m3::run({"pacman", "-Qqo", path}, [st, rm, name](int code, const std::string& out) {
                if (!*st->alive) return;
                std::string pkg = out.substr(0, out.find('\n'));
                if (code != 0 || !safeName(pkg)) return void(m3::run({"notify-send", "-a", "Store", "No package owns " + name}, nullptr));
                if (pkg.starts_with("material3ui")) return void(m3::run({"notify-send", "-a", "Store", name + " is part of Material3UI", "Settings → Material3UI apps brings removed ones back"}, nullptr));
                floatingMenu(rm, {MenuItem("delete", "Remove " + name, "the " + pkg + " package", [pkg] {
                                      if (!Packages::get().busy()) Packages::get().uninstall(Source::Repo, pkg);
                                  }, false, true)});
            });
        };
    }
}

void updatesPage(std::shared_ptr<StoreState> st, Flex* p) {
    auto& pk = Packages::get();
    auto* head = p->add<Flex>(Flex::Direction::Row);
    head->setSpacing(10)->setAlignItems(Align::Center);
    const auto& ups = pk.updates();
    auto* t = head->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(2);
    t->grow = true;
    t->add<Label>(pk.checking() ? "Checking…" : ups.empty() ? "Everything is up to date" : std::format("{} update{}", ups.size(), ups.size() == 1 ? "" : "s"))->setSize(20)->setWeight(500);
    if (pk.lastChecked()) caption(t, "Checked " + fileops::humanTime(pk.lastChecked()));
    if (const std::string b = busyLabel(); !b.empty()) caption(t, b);
    pillButton(head, "Check", false, [] { Packages::get().check(); }, "refresh");
    if (!ups.empty()) {
        auto* u = pillButton(head, "Update all", true, [] {
            if (!Packages::get().busy()) Packages::get().upgrade();
        }, "system_update");
        if (pk.busy()) u->setOpacity(0.5f, false);
    }
    if (ups.empty()) return;
    auto* g = p->add<Flex>(Flex::Direction::Column);
    g->setRadius(Radii(20))->setBackground(ColorRef("surface_container", 0.8f), false)->setAlignItems(Align::Stretch);
    for (const auto& u : ups) {
        auto* r = g->add<Flex>(Flex::Direction::Row);
        r->setPadding({16, 9})->setSpacing(14)->setAlignItems(Align::Center);
        const CatalogApp* a = u.source == Source::Flatpak ? st->find(u.name) : nullptr;
        if (a) iconOrGlyph(r, a->icon, 32);
        else r->add<Icon>(u.source == Source::Flatpak ? "apps" : u.source == Source::Aur ? "deployed_code_account" : u.source == Source::Shell ? "auto_awesome" : "deployed_code", 26)
                ->setColor("on_surface_variant", false);
        auto* l = r->add<Label>(a ? a->name : u.name);
        l->setSize(14)->setWeight(500)->setMaxWidth(360);
        l->grow = true;
        r->add<Label>(u.from.empty() ? u.to : u.from + "  →  " + u.to)->setSize(12)->setMaxWidth(320)->setColor("on_surface_variant", false);
    }
}

void build(std::shared_ptr<StoreState> st) {
    // Tabs
    auto* t = st->tabs;
    t->clearChildren();
    const char* names[] = {"Explore", "Installed", "Updates"};
    const char* icons[] = {"storefront", "apps", "system_update"};
    for (int i = 0; i < 3; ++i) {
        const bool on = st->tab == i && st->page.empty();
        auto* b = t->add<Button>(Flex::Direction::Row);
        pill(b->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_high"), false))->setPadding({14, 8, 16, 8})->setSpacing(8)->setAlignItems(Align::Center);
        b->add<Icon>(icons[i], 18)->setFill(on)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
        std::string label = names[i];
        if (i == 2 && !Packages::get().updates().empty()) label += std::format("  {}", Packages::get().updates().size());
        b->add<Label>(label)->setSize(14)->setWeight(on ? 600 : 500)->setColor(on ? "on_secondary_container" : "on_surface", false);
        b->onClicked = [st, i](uint32_t button) {
            if (button != BTN_LEFT) return;
            st->tab = i;
            st->page.clear();
            refresh(st);
        };
    }
    st->status->setText(busyLabel());

    auto* p = st->body;
    p->clearChildren();
    if (!st->page.empty()) {
        if (const auto* a = st->find(st->page)) return appPage(st, p, *a);
        st->page.clear();
    }
    if (st->tab == 0) explore(st, p);
    else if (st->tab == 1) installedPage(st, p);
    else updatesPage(st, p);
}

} // namespace

std::function<void(const std::string&)> storeGo;

void storeApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<StoreState>();
    card->setPadding(Insets(10))->setSpacing(10);
    card->add<StoreKeeper>(st);

    // The top: tabs, what's under way, search
    auto* top = card->add<Flex>(Flex::Direction::Row);
    top->setSpacing(10)->setAlignItems(Align::Center)->setPadding({6, 0});
    st->tabs = top->add<Flex>(Flex::Direction::Row);
    st->tabs->setSpacing(6)->setAlignItems(Align::Center);
    st->status = top->add<Label>("");
    st->status->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(220);
    st->status->grow = true;
    auto* searchBox = top->add<Flex>(Flex::Direction::Row);
    pill(searchBox->setBackground("surface_container_high", false))->setPadding({14, 4})->setSpacing(8)->setAlignItems(Align::Center);
    searchBox->fixedWidth = 300;
    searchBox->add<Icon>("search", 18)->setColor("on_surface_variant", false);
    st->search = searchBox->add<TextField>();
    st->search->bare = true;
    st->search->grow = true;
    st->search->setPlaceholder("Search apps");
    st->search->onChanged = [st](const std::string& text) {
        st->query = text;
        st->tab = 0;
        st->page.clear();
        EventLoop::get().debounce(st->searchTimer, 300, [st] {
            if (!*st->alive) return;
            searchNative(st);
            refresh(st);
            if (st->scroll) st->scroll->scrollTo(0, false);
        });
    };

    st->scroll = card->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->scroll->blurEdges = true;
    st->body = st->scroll->add<Flex>(Flex::Direction::Column);
    st->body->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({8, 4, 8, 20});
    st->show = [st] { build(st); };

    // Installs and updates finishing: what's installed, read again
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

    popup.onKey = [st](const KeyEvent& e) {
        if (e.ctrl() && (e.sym == XKB_KEY_f || e.sym == XKB_KEY_F)) return st->search->focus(), true;
        if (e.sym == XKB_KEY_Escape && !st->page.empty()) {
            st->page.clear();
            refresh(st);
            return true;
        }
        return false;
    };
    // "search:<words>": a search, as if typed
    auto startSearch = [st](const std::string& q) {
        st->query = q;
        st->tab = 0;
        st->page.clear();
        st->search->setText(q);
        searchNative(st);
    };
    storeGo = [st, startSearch](const std::string& what) {
        if (!*st->alive) return;
        if (what.starts_with("search:")) startSearch(what.substr(7));
        else if (what == "updates") st->tab = 2, st->page.clear();
        else if (!what.empty()) st->tab = 0, st->page = what;
        refresh(st);
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
