// Files: the shell's own file manager, an app window in its Material 3
// look. Places down the side (your folders, drives, the trash); back,
// forward, up and the path along the top, with a search of the folder and
// the view; the folder as a grid (pictures as previews, videos as their
// first frame) or a list. Click selects (Ctrl adds, Shift a range), a
// double click opens: folders here, pictures in the image viewer, the rest
// in their app. A right click has the rest: open with, cut, copy, paste,
// rename, the trash, Quick Share, the wallpaper. The folder follows what
// changes in it as it changes. The work is services/FileOps.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/FileWatcher.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/FileOps.h"
#include "services/Search.h"
#include "services/Wallpaper.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <format>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <set>
#include <fstream>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;
using fileops::Entry;
using fileops::Kind;

namespace {

using Clock = std::chrono::steady_clock;

// Cut or copied files, for Paste in any folder
struct {
    std::vector<std::string> paths;
    bool cut = false;
} g_clip;

// Folders watched for changes: each once, its changes said to whichever
// window shows it now
std::set<std::string> g_watched;
std::string g_changedDir;
Signal<> g_dirChanged;

void watch(const std::string& dir) {
    if (!g_watched.insert(dir).second) return;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    FileWatcher::get().watchDir(dir, [dir] {
        g_changedDir = dir;
        g_dirChanged.emit();
    });
}

std::string quoted(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string fileUri(const std::string& path) {
    std::string out = "file://";
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

// "file10" after "file9": names with numbers in them, as people count
bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const auto ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t ei = i, ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            const std::string na = a.substr(i, ei - i), nb = b.substr(j, ej - j);
            const std::string ta = na.substr(std::min(na.find_first_not_of('0'), na.size())), tb = nb.substr(std::min(nb.find_first_not_of('0'), nb.size()));
            if (ta.size() != tb.size()) return ta.size() < tb.size();
            if (ta != tb) return ta < tb;
            i = ei, j = ej;
            continue;
        }
        const int la = ca < 0x80 ? std::tolower(ca) : ca, lb = cb < 0x80 ? std::tolower(cb) : cb;
        if (la != lb) return la < lb;
        ++i, ++j;
    }
    return a.size() - i < b.size() - j;
}

// Empty space round the files: a click there clears the selection, a right
// click has the folder's menu
class Backdrop : public Flex {
public:
    explicit Backdrop(ScrollView* scroll) : Flex(Direction::Column), m_scroll(scroll) {}
    std::function<void(uint32_t)> onClicked;
    float pressX = 0, pressY = 0; // where the last press was: a right click's menu goes there
    bool interactive() const override { return true; }
    void onPress(uint32_t, float x, float y) override {
        pressX = x;
        pressY = y;
    }
    void onClick(uint32_t button) override {
        if (onClicked) onClicked(button);
    }

protected:
    // At least the height of the view, so the space under a few files is
    // its too (clicks there clear the selection, right clicks have a menu)
    SizeF doMeasure(SizeF available) override {
        SizeF s = Flex::doMeasure(available);
        s.h = std::max(s.h, m_scroll->rect().h - 1);
        return s;
    }

private:
    ScrollView* m_scroll;
};

struct State;

// The path: short of room, the folders above the current one go from the
// left (they're behind "…" already), never the current one
class Crumbs : public Flex {
public:
    Crumbs() : Flex(Direction::Row) {}

protected:
    SizeF doMeasure(SizeF available) override {
        SizeF s = Flex::doMeasure(available);
        s.w = std::min(s.w, available.w); // it gives way: what doesn't fit is hidden in doArrange
        return s;
    }
    void doArrange() override {
        auto& kids = children();
        for (auto& c : kids) c->setVisible(true);
        const float room = rect().w - 12;
        auto natural = [&] {
            float w = 0;
            int n = 0;
            for (auto& c : kids)
                if (c->visible()) w += c->measure({1e6f, rect().h}).w, ++n;
            return w + 2.0f * static_cast<float>(std::max(0, n - 1)); // its spacing
        };
        // [more] chevron, step, chevron, step … the last step stays
        for (size_t i = 0; natural() > room && i + 1 < kids.size(); ++i) {
            if (kids[i]->tooltip == "Folders above") continue; // "…" stays
            if (i + 2 >= kids.size()) break; // the current folder (and its chevron)
            kids[i]->setVisible(false);
        }
        Flex::doArrange();
    }
};

// The window's columns: the side bar, the folder, the pane. Short of room,
// the side bar folds away first (the pane is what you opened), then the pane
class Columns : public Flex {
public:
    Columns() : Flex(Direction::Row) {}
    Node *side = nullptr, *pane = nullptr;
    bool paneWanted = true;

protected:
    void doArrange() override {
        const float w = rect().w;
        const bool paneOn = paneWanted && w >= 760, sideOn = !(paneOn && w < 1000) && w >= 560;
        if (pane && pane->visible() != paneOn) pane->setVisible(paneOn);
        if (side && side->visible() != sideOn) side->setVisible(sideOn);
        Flex::doArrange();
    }
};

// Goes with the window: everything the state holds that could call back
// into it, or into nodes now gone, let go
class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override;

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

struct State {
    std::string shownView; // the folder (and view) last shown: a new one's files enter in turn
    fs::path dir;
    bool inTrash = false;
    std::vector<std::string> back, forward;
    std::vector<Entry> entries;               // shown: filtered and sorted
    std::map<std::string, std::string> origin; // the trash: where each came from
    std::vector<std::string> selected;         // in the order picked
    std::string anchor;                        // the last clicked, for Shift
    std::string filter;
    std::string lastClick;
    Clock::time_point lastClickAt;
    std::string error; // the folder couldn't be read
    // Nodes
    Flex *side = nullptr, *crumbs = nullptr, *footer = nullptr, *prompt = nullptr;
    Label* status = nullptr;
    Backdrop* content = nullptr;
    ScrollView* scroll = nullptr;
    TextField* search = nullptr;
    Flex* searchRow = nullptr;
    Icon* viewIcon = nullptr;
    Button* emptyTrash = nullptr;
    std::map<std::string, Button*> tiles;
    std::function<void()> load, render, sidebar, paint, showStatus, details;
    Flex* pane = nullptr; // the preview and properties, on the right
    std::string paneFor;  // what it shows now
    EventLoop::TimerId detailsTimer = 0, driveTimer = 0;
    std::string driveSig; // the drives and servers the side bar shows
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId thumbTimer = 0;
    std::vector<Connection> conns;
    // The window's: a folder in a new tab (a middle click), and the tabs'
    // labels to update when this one moves
    std::function<void(const std::string&)> openTab;
    std::function<void()> moved;
};

Keeper::~Keeper() {
    *m_st->alive = false;
    for (auto t : {m_st->thumbTimer, m_st->detailsTimer, m_st->driveTimer})
        if (t) EventLoop::get().cancel(t);
    m_st->conns.clear();
    m_st->load = m_st->render = m_st->sidebar = m_st->paint = m_st->showStatus = m_st->details = nullptr;
}

bool listView() {
    return Config::get().value<bool>("/files/listView", false);
}
bool showHidden() {
    return Config::get().value<bool>("/files/hidden", false);
}
std::string sortBy() {
    return Config::get().value<std::string>("/files/sort", "name");
}

bool isSelected(const State& st, const std::string& p) {
    return std::ranges::find(st.selected, p) != st.selected.end();
}

void go(const std::shared_ptr<State>& st, const fs::path& to, bool remember = true) {
    struct Moved {
        std::shared_ptr<State> st;
        ~Moved() {
            if (st->moved) EventLoop::get().defer([s = st] {
                if (*s->alive && s->moved) s->moved();
            });
        }
    } moved{st};
    std::error_code ec;
    if (!fs::is_directory(to, ec)) return;
    if (remember && (st->dir != to || st->inTrash)) {
        st->back.push_back(st->inTrash ? "trash:" : st->dir.string());
        st->forward.clear();
    }
    st->dir = fs::weakly_canonical(to, ec);
    st->inTrash = false;
    st->selected.clear();
    st->anchor.clear();
    st->filter.clear();
    if (st->search) st->search->setText("");
    watch(st->dir.string());
    st->load();
    if (st->scroll) st->scroll->scrollTo(0, false);
}

void goTrash(const std::shared_ptr<State>& st) {
    if (!st->inTrash) {
        st->back.push_back(st->dir.string());
        st->forward.clear();
    }
    st->inTrash = true;
    st->selected.clear();
    st->filter.clear();
    if (st->search) st->search->setText("");
    watch(fileops::trashFiles());
    st->load();
    if (st->scroll) st->scroll->scrollTo(0, false);
}

void stepHistory(const std::shared_ptr<State>& st, bool backwards) {
    auto& from = backwards ? st->back : st->forward;
    auto& to = backwards ? st->forward : st->back;
    if (from.empty()) return;
    const std::string where = from.back();
    from.pop_back();
    to.push_back(st->inTrash ? "trash:" : st->dir.string());
    if (where == "trash:") {
        st->inTrash = true;
        st->selected.clear();
        st->load();
    } else {
        go(st, where, false);
    }
}

// Opening one: a folder here, a picture in the viewer, the rest in its app
void openPath(const std::shared_ptr<State>& st, const std::string& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) return go(st, path);
    if (fileops::isPicture(path) && Config::get().value<bool>("/files/viewer", true)) {
        if (open) open("viewer " + path);
        return;
    }
    // Subtitles go to the video playing (the player adds them to it)
    {
        std::string e = fs::path(path).extension().string();
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e == ".srt" || e == ".ass" || e == ".ssa" || e == ".vtt" || e == ".sub" || e == ".sup" || e == ".idx") {
            // Videos takes it: with something playing, to go with it
            if (open) open("player " + path);
            return;
        }
    }
    // Text and code in the text editor, PDFs in the PDF viewer (when they're installed)
    {
        const auto k = fileops::kindOf(path);
        std::string e = fs::path(path).extension().string();
        for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e == ".pdf" && !ownCommand("material3ui-pdf").empty() && Config::get().value<bool>("/files/pdf", true)) {
            if (open) open("pdf " + path);
            return;
        }
        if ((e == ".ttf" || e == ".otf" || e == ".ttc" || e == ".otc" || e == ".woff" || e == ".woff2") && !ownCommand("material3ui-fonts").empty() &&
            Config::get().value<bool>("/files/fonts", true)) {
            if (open) open("fonts " + path);
            return;
        }
        if ((k == fileops::Kind::Text || k == fileops::Kind::Code) && !ownCommand("material3ui-text").empty() && Config::get().value<bool>("/files/textEditor", true)) {
            if (open) open("text " + path);
            return;
        }
    }
    // Songs in Music, when it's installed
    if (fileops::kindOf(path) == fileops::Kind::Audio && !ownCommand("material3ui-music").empty() && Config::get().value<bool>("/files/music", true)) {
        if (open) open("music " + path);
        return;
    }
    // Videos and songs in the shell's own player
    if (const auto k = fileops::kindOf(path); (k == fileops::Kind::Video || k == fileops::Kind::Audio) && Config::get().value<bool>("/files/player", true)) {
        if (open) open("player " + path);
        return;
    }
    m3::run({"xdg-open", path}, nullptr);
}

void setClipboard(const std::vector<std::string>& paths, bool cut) {
    g_clip.paths = paths;
    g_clip.cut = cut;
    // Other apps can paste them too
    std::string uris;
    for (const auto& p : paths) uris += fileUri(p) + "\r\n";
    m3::run({"wl-copy", "--type", "text/uri-list"}, nullptr, uris);
}

// Files dropped on a folder (the one shown, or one in it). A drag from
// Files itself moves them when they stay on the same drive, as file managers
// do (Ctrl copies); from other apps they're copied
void dropInto(std::vector<std::string> paths, const std::string& dir) {
    const fs::path to(dir);
    std::erase_if(paths, [&](const std::string& p) { return fs::path(p).parent_path() == to || p == dir || dir.starts_with(p + "/"); });
    if (paths.empty()) return;
    struct stat a {}, b {};
    const bool sameDrive = ::stat(dir.c_str(), &b) == 0 && ::stat(paths.front().c_str(), &a) == 0 && a.st_dev == b.st_dev;
    const bool move = wl::Display::get().dragging() && sameDrive && !(wl::Display::get().heldModifiers() & ModCtrl);
    auto done = [](const std::string& err) {
        if (!err.empty()) m3::run({"notify-send", "-a", "Files", "-i", "folder", "Files", err}, nullptr);
    };
    if (move) fileops::move(paths, dir, done);
    else fileops::copy(paths, dir, done);
}

void paste(const std::shared_ptr<State>& st) {
    const std::string dir = st->dir.string();
    auto done = [](const std::string& err) {
        if (!err.empty()) m3::run({"notify-send", "-a", "Files", "-i", "folder", "Files", err}, nullptr);
    };
    if (!g_clip.paths.empty()) {
        if (g_clip.cut) {
            fileops::move(g_clip.paths, dir, done);
            g_clip = {};
        } else {
            fileops::copy(g_clip.paths, dir, done);
        }
        return;
    }
    // Copied in another app (a file manager, a browser's download list)
    if (!haveCommand("wl-paste")) return;
    m3::run({"wl-paste", "--no-newline", "--type", "text/uri-list"}, [dir, done](int code, const std::string& out) {
        if (code != 0) return;
        std::vector<std::string> paths;
        std::stringstream ss(out);
        for (std::string line; std::getline(ss, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.starts_with("file://")) continue;
            std::string p;
            for (size_t i = 7; i < line.size(); ++i) {
                if (line[i] == '%' && i + 2 < line.size()) {
                    p += static_cast<char>(std::strtol(line.substr(i + 1, 2).c_str(), nullptr, 16));
                    i += 2;
                } else {
                    p += line[i];
                }
            }
            paths.push_back(p);
        }
        if (!paths.empty()) fileops::copy(paths, dir, done);
    });
}

// The footer's question: a field (rename, a new folder's name) or a yes/no
void ask(const std::shared_ptr<State>& st, const std::string& question, const std::string& initial, const std::string& ok, bool danger,
         std::function<std::string(const std::string&)> answer, bool field = true) {
    auto* p = st->prompt;
    p->clearChildren();
    p->setVisible(true);
    p->add<Label>(question)->setSize(14)->setWeight(500);
    TextField* f = nullptr;
    if (field) {
        f = p->add<TextField>();
        f->setText(initial);
        f->grow = true;
        f->minWidth = 200;
    } else {
        p->add<Node>()->grow = true;
    }
    auto* err = p->add<Label>("");
    err->setSize(12)->setColor("error", false)->setVisible(false);
    auto finish = [st, f, err, answer](const std::string&) {
        const std::string e = answer(f ? f->text() : std::string());
        if (!e.empty()) {
            err->setText(e)->setVisible(true);
            return;
        }
        st->prompt->setVisible(false);
        EventLoop::get().defer([st, alive = st->alive] {
            if (*alive) st->prompt->clearChildren();
        });
    };
    pillButton(p, "Cancel", false, [st] { st->prompt->setVisible(false); });
    auto* yes = pillButton(p, ok, true, [finish] { finish({}); });
    if (danger) yes->setBackground("error", false);
    if (f) {
        f->onSubmit = finish;
        f->onCancel = [st] { st->prompt->setVisible(false); };
        f->focus();
        // The name selected short of its extension, as file managers do
    }
}

void renameOne(const std::shared_ptr<State>& st, const std::string& path) {
    ask(st, "Rename", fs::path(path).filename().string(), "Rename", false, [st, path](const std::string& name) {
        const std::string e = fileops::rename(path, name);
        if (e.empty()) {
            st->selected = {(fs::path(path).parent_path() / name).string()};
            st->load();
        }
        return e;
    });
}

void newFolder(const std::shared_ptr<State>& st) {
    ask(st, "New folder", fileops::freeName(st->dir.string(), "New folder"), "Create", false, [st](const std::string& name) {
        std::string made;
        const std::string e = fileops::makeFolder(st->dir.string(), name, &made);
        if (e.empty()) {
            st->selected = {made};
            st->load();
        }
        return e;
    });
}

void deleteForGood(const std::shared_ptr<State>& st, std::vector<std::string> paths) {
    const std::string what = paths.size() == 1 ? "\"" + fs::path(paths.front()).filename().string() + "\"" : std::format("{} items", paths.size());
    ask(st, "Delete " + what + " for good? This can't be undone", "", "Delete", true, [st, paths](const std::string&) {
        fileops::remove(paths);
        st->selected.clear();
        return std::string();
    }, false);
}

void toTrash(const std::shared_ptr<State>& st, const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    fileops::trash(paths, [](const std::string& err) {
        if (!err.empty()) m3::run({"notify-send", "-a", "Files", "-i", "delete", "Files", err}, nullptr);
    });
    st->selected.clear();
}

// "Open with": the apps that say they open this kind of file
void openWith(const std::shared_ptr<State>& st, Node* from, const std::string& path) {
    auto alive = st->alive;
    m3::run({"xdg-mime", "query", "filetype", path}, [st, alive, from, path](int, std::string mime) {
        if (!*alive) return;
        while (!mime.empty() && std::isspace(static_cast<unsigned char>(mime.back()))) mime.pop_back();
        const std::string major = mime.substr(0, mime.find('/'));
        std::vector<MenuItem> items;
        std::vector<const Apps::App*> fits, more;
        for (const auto& a : Apps::get().all()) {
            const std::string types = ";" + a.mimeTypes + ";";
            if (!mime.empty() && types.find(";" + mime + ";") != std::string::npos) fits.push_back(&a);
            else if (!major.empty() && types.find(";" + major + "/") != std::string::npos) more.push_back(&a);
        }
        if (major == "text" || mime == "application/json" || mime == "application/x-shellscript")
            for (const auto& a : Apps::get().all())
                if ((";" + a.categories + ";").find(";TextEditor;") != std::string::npos && std::ranges::find(fits, &a) == fits.end()) fits.push_back(&a);
        auto add = [&](const Apps::App* a, bool sep) {
            MenuItem m;
            m.label = a->name;
            m.separatorBefore = sep;
            const std::string icon = a->icon;
            m.lead = [icon](Node* row) {
                row->add<ImageView>(22, [icon](float scale, int& w, int& h) -> GLuint {
                    auto* t = Images::get().icon(icon.empty() ? "application-x-executable" : icon, static_cast<int>(std::lround(22 * scale)));
                    if (!t) return 0;
                    w = t->width, h = t->height;
                    return t->id;
                });
            };
            const std::string exec = a->exec;
            const bool term = a->terminal;
            m.action = [exec, term, path] {
                const char* t = std::getenv("TERMINAL");
                const std::string cmd = exec + " " + quoted(path);
                spawn(term ? std::format("{} -e {}", t && *t ? t : "alacritty", cmd) : cmd);
            };
            items.push_back(std::move(m));
        };
        for (size_t i = 0; i < fits.size(); ++i) add(fits[i], false);
        for (size_t i = 0; i < more.size() && i < 8; ++i) add(more[i], i == 0 && !fits.empty());
        if (items.empty()) items.push_back({"block", "No app opens this kind of file", "", {}, false, false, false});
        // Still on screen: the menu goes where the file is
        if (st->tiles.contains(path)) floatingMenu(st->tiles[path], items);
        else if (from) floatingMenu(st->content, items);
    });
}

// Connect to a server: an address (ftp://, sftp://, davs://), and a name
// and password unless it lets anyone in
void connectPrompt(const std::shared_ptr<State>& st) {
    auto* p = st->prompt;
    p->clearChildren();
    p->setVisible(true);
    p->add<Icon>("dns", 22)->setColor("primary", false);
    auto* url = p->add<TextField>();
    url->setPlaceholder("ftp://example.com  ·  sftp://you@host  ·  davs://host/path");
    url->grow = true;
    url->minWidth = 200;
    auto* user = p->add<TextField>();
    user->setPlaceholder("User (blank: anyone)");
    user->fixedWidth = 150;
    auto* pass = p->add<TextField>();
    pass->setPlaceholder("Password");
    pass->setSecret(true);
    pass->fixedWidth = 130;
    auto* note = p->add<Label>("");
    note->setSize(12)->setColor("error", false)->setVisible(false);
    auto go_ = [st, url, user, pass, note](const std::string&) {
        if (url->text().empty()) return;
        note->setText("Connecting…")->setColor("on_surface_variant")->setVisible(true);
        fileops::connect(url->text(), user->text(), pass->text(), [st, note, alive = st->alive](const std::string& err, const std::string& at) {
            if (!*alive) return;
            if (!err.empty()) {
                note->setText(err)->setColor("error")->setVisible(true);
                return;
            }
            st->prompt->setVisible(false);
            go(st, at);
        });
    };
    url->onSubmit = go_;
    user->onSubmit = go_;
    pass->onSubmit = go_;
    url->onCancel = [st] { st->prompt->setVisible(false); };
    pillButton(p, "Cancel", false, [st] { st->prompt->setVisible(false); });
    pillButton(p, "Connect", true, [go_] { go_({}); }, "link");
    url->focus();
}

void compressPrompt(const std::shared_ptr<State>& st, const std::vector<std::string>& paths) {
    const std::string first = paths.size() == 1 ? fileops::archiveStem(paths.front()) : "Archive";
    ask(st, "Compress to ZIP", first, "Compress", false, [st, paths](const std::string& name) {
        if (name.empty() || name.find('/') != std::string::npos) return std::string("A name can't be empty or have a / in it");
        fileops::compress(paths, name, [st, alive = st->alive](const std::string& err, const std::string& zip) {
            if (!err.empty()) return void(m3::run({"notify-send", "-a", "Files", "-i", "package-x-generic", "Files", err}, nullptr));
            if (*alive) {
                st->selected = {zip};
                st->load();
            }
        });
        return std::string();
    });
}

void extractHere(const std::shared_ptr<State>& st, const std::vector<std::string>& archives) {
    for (const auto& a : archives)
        fileops::extract(a, [st, alive = st->alive](const std::string& err, const std::string& folder) {
            if (!err.empty()) return void(m3::run({"notify-send", "-a", "Files", "-i", "package-x-generic", "Files", err}, nullptr));
            if (*alive) {
                st->selected = {folder};
                st->load();
            }
        });
}

void showPane(const std::shared_ptr<State>& /*st*/, bool on) {
    Config::get().set("/files/preview", on);
}

void fileMenu(const std::shared_ptr<State>& st, Button* tile, const std::string& path) {
    if (!isSelected(*st, path)) {
        st->selected = {path};
        st->anchor = path;
        st->paint();
    }
    const auto paths = st->selected;
    const bool one = paths.size() == 1;
    std::error_code ec;
    const bool dir = one && fs::is_directory(path, ec);
    std::vector<MenuItem> m;
    if (st->inTrash) {
        m.push_back({"restore_from_trash", "Restore", "", [paths] { fileops::restore(paths); }});
        m.push_back({"delete_forever", "Delete for good", "Del", [st, paths] { deleteForGood(st, paths); }, false, true});
        return floatingMenu(tile, m);
    }
    m.push_back({dir ? "folder_open" : "open_in_new", "Open", "Enter", [st, path] { openPath(st, path); }});
    if (one && !dir) m.push_back({"apps", "Open with…", "", [st, tile, path] { EventLoop::get().defer([st, tile, path] { openWith(st, tile, path); }); }});
    if (dir) m.push_back({"terminal", "Open a terminal here", "", [path] { spawn("cd " + quoted(path) + " && exec ${TERMINAL:-alacritty}"); }});
    m.push_back({"content_cut", "Cut", "Ctrl+X", [paths] { setClipboard(paths, true); }, false, false, true, true});
    m.push_back({"content_copy", "Copy", "Ctrl+C", [paths] { setClipboard(paths, false); }});
    if (dir) m.push_back({"content_paste", "Paste into it", "", [st, path] {
        const auto here = st->dir;
        st->dir = path;
        paste(st);
        st->dir = here;
    }, false, false, !g_clip.paths.empty()});
    if (one) m.push_back({"edit", "Rename", "F2", [st, path] { renameOne(st, path); }});
    // A disk image: onto a USB drive, to start a computer from
    if (paths.size() == 1 && !ownCommand("material3ui-imagewriter").empty()) {
        std::string n = fs::path(paths.front()).filename().string();
        for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const char* z : {".xz", ".gz", ".zst"})
            if (n.ends_with(z)) n.resize(n.size() - std::strlen(z));
        if (n.ends_with(".iso") || n.ends_with(".img"))
            m.push_back({"usb", "Write to a USB drive", "", [p = paths.front()] {
                             if (open) open("imagewriter " + p);
                         }, false, false, true, true});
    }
    if (std::ranges::all_of(paths, [](const std::string& p) { return fileops::canExtract(p); }))
        m.push_back({"unarchive", paths.size() == 1 ? "Extract here" : "Extract each here", "", [st, paths] { extractHere(st, paths); }, false, false, true, true});
    m.push_back({"folder_zip", "Compress to ZIP…", "", [st, paths] { compressPrompt(st, paths); }});
    m.push_back({"link", "Copy the path", "", [paths] {
        std::string text;
        for (const auto& p : paths) text += (text.empty() ? "" : "\n") + p;
        m3::run({"wl-copy"}, nullptr, text);
    }});
    if (one && fileops::isPicture(path)) {
        m.push_back({"wallpaper", "Set as wallpaper", "", [path] {
            if (open) open("wallpaper " + path);
        }, false, false, true, true});
        m.push_back({"brush", "Edit", "", [path] {
            if (open) open("imageeditor " + path);
        }});
    }
    bool files = std::ranges::none_of(paths, [](const std::string& p) {
        std::error_code e;
        return fs::is_directory(p, e);
    });
    if (files) m.push_back({"nearby", "Send with Quick Share", "", [paths] {
        std::string all;
        for (const auto& p : paths) all += p + "\n";
        if (open) open("quickshare-send " + all);
    }});
    m.push_back({"delete", "Move to the trash", "Del", [st, paths] { toTrash(st, paths); }, false, true, true, true});
    m.push_back({"delete_forever", "Delete for good", "Shift+Del", [st, paths] { deleteForGood(st, paths); }, false, true});
    m.push_back({"info", "Properties", "Alt+Enter", [st] {
        showPane(st, true);
        st->paneFor.clear();
        if (st->details) st->details();
    }, false, false, true, true});
    floatingMenu(tile, m);
}

void folderMenu(const std::shared_ptr<State>& st, Backdrop* from) {
    const auto show = [from](const std::vector<MenuItem>& m) { floatingMenuAt(from, from->pressX, from->pressY, m); };
    std::vector<MenuItem> m;
    if (st->inTrash) {
        m.push_back({"delete_forever", "Empty the trash", "", [st] {
            ask(st, "Empty the trash? What's in it is deleted for good", "", "Empty", true, [](const std::string&) {
                fileops::emptyTrash();
                return std::string();
            }, false);
        }, false, true, !st->entries.empty()});
        return show(m);
    }
    const std::string dir = st->dir.string();
    m.push_back({"create_new_folder", "New folder", "Ctrl+Shift+N", [st] { newFolder(st); }});
    m.push_back({"content_paste", "Paste", "Ctrl+V", [st] { paste(st); }});
    m.push_back({"select_all", "Select all", "Ctrl+A", [st] {
        st->selected.clear();
        for (const auto& e : st->entries) st->selected.push_back(e.path);
        st->paint();
    }});
    m.push_back({"terminal", "Open a terminal here", "", [dir] { spawn("cd " + quoted(dir) + " && exec ${TERMINAL:-alacritty}"); }, false, false, true, true});
    m.push_back({"visibility", "Show hidden files", "Ctrl+H", [] { Config::get().set("/files/hidden", !showHidden()); }, showHidden()});
    show(m);
}

// A click on a file: select (Ctrl adds or takes away, Shift a range);
// twice quickly opens it
void clicked(const std::shared_ptr<State>& st, Button* tile, const std::string& path, uint32_t button) {
    if (button == BTN_RIGHT) return fileMenu(st, tile, path);
    if (button == BTN_MIDDLE) {
        std::error_code ec;
        if (st->openTab && fs::is_directory(path, ec)) st->openTab(path);
        return;
    }
    if (button != BTN_LEFT) return;
    const auto now = Clock::now();
    if (st->lastClick == path && now - st->lastClickAt < std::chrono::milliseconds(450)) {
        st->lastClick.clear();
        if (!st->inTrash) openPath(st, path);
        else if (fileops::isPicture(path) && open) open("viewer " + path);
        return;
    }
    st->lastClick = path;
    st->lastClickAt = now;
    const uint32_t mods = wl::Display::get().heldModifiers();
    if (mods & ModShift && !st->anchor.empty()) {
        const auto a = std::ranges::find(st->entries, st->anchor, &Entry::path), b = std::ranges::find(st->entries, path, &Entry::path);
        if (a != st->entries.end() && b != st->entries.end()) {
            if (!(mods & ModCtrl)) st->selected.clear();
            for (auto it = std::min(a, b); it <= std::max(a, b); ++it)
                if (!isSelected(*st, it->path)) st->selected.push_back(it->path);
        }
    } else if (mods & ModCtrl) {
        if (auto it = std::ranges::find(st->selected, path); it != st->selected.end()) st->selected.erase(it);
        else st->selected.push_back(path);
        st->anchor = path;
    } else {
        st->selected = {path};
        st->anchor = path;
    }
    st->paint();
}

// What a file shows as: its picture, a video's frame, or its kind's icon
void art(const std::shared_ptr<State>& st, Node* parent, const Entry& e, float w, float h, bool big) {
    const Kind kind = fileops::kindOf(e.path, e.dir);
    std::string picture;
    if (kind == Kind::Picture && !e.path.ends_with(".svg") && e.size < 200'000'000) {
        // Any format: those gdk-pixbuf can't read (PSD, RAW…) as ImageMagick's PNG of them
        auto alive = st->alive;
        picture = fileops::renderable(e.path, [st, alive] {
            if (!*alive) return;
            EventLoop::get().debounce(st->thumbTimer, 400, [st, alive] {
                if (*alive) st->render();
            });
        });
    }
    const bool pdf = !e.dir && fs::path(e.path).extension() == ".pdf";
    if (kind == Kind::Video || (pdf && Config::get().value<bool>("/files/pdfThumbnails", true))) {
        auto alive = st->alive;
        picture = fileops::thumbnail(e.path, big, [st, alive] {
            if (!*alive) return;
            EventLoop::get().debounce(st->thumbTimer, 400, [st, alive] {
                if (*alive) st->render();
            });
        });
    }
    if (!picture.empty()) {
        auto* v = parent->add<ImageView>(w, h, [picture, w, h](float scale, int& tw, int& th) -> GLuint {
            auto* t = Images::get().picture(picture, static_cast<int>(w * scale), static_cast<int>(h * scale), true);
            if (!t) return 0;
            tw = t->width, th = t->height;
            return t->id;
        });
        v->cover = !pdf; // a page shows whole
        v->radius = big ? 14 : 6;
        return;
    }
    auto* box = parent->add<Flex>(Flex::Direction::Row);
    box->setJustify(Align::Center)->setAlignItems(Align::Center);
    box->fixedWidth = w;
    box->fixedHeight = h;
    if (kind == Kind::Folder) {
        box->add<Icon>(e.link ? "folder_special" : "folder", big ? h * 0.82f : h)->setFill(true)->setColor("primary", false);
        return;
    }
    if (big) box->setRadius(Radii(16))->setBackground("surface_container_highest", false);
    box->add<Icon>(fileops::kindIcon(kind, e.path), big ? 38 : h)->setColor(kind == Kind::Other ? "on_surface_variant" : "tertiary", false);
}

void build(const std::shared_ptr<State>& st, Flex* card) {
    card->setPadding(Insets(12))->setSpacing(0);
    auto* columns = card->add<Columns>();
    columns->setAlignItems(Align::Stretch)->setSpacing(12);
    columns->grow = true;

    // ── The side: places, drives, the trash ──
    auto* sideScroll = columns->add<ScrollView>();
    columns->side = sideScroll;
    sideScroll->maxHeight = 1e6f;
    sideScroll->fixedWidth = 230;
    st->side = sideScroll->add<Flex>(Flex::Direction::Column);
    st->side->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8, 4, 8});
    st->sidebar = [st] {
        auto* s = st->side;
        s->clearChildren();
        auto* brand = s->add<Label>("Files");
        brand->setSize(30)->setWeight(400);
        brand->alignSelf = Align::Start;
        s->add<Node>()->fixedHeight = 6;
        // A row: the place, and maybe a button at its end (eject, disconnect)
        auto place = [&](const std::string& icon, const std::string& name, const std::string& path, bool trash,
                         std::function<void()> click = {}, const std::string& endIcon = {}, std::function<void()> end = {},
                         const std::string& endTip = {}) {
            std::error_code ec;
            const bool here = trash ? st->inTrash : !path.empty() && !st->inTrash && fs::equivalent(path, st->dir, ec);
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({16, endIcon.empty() ? 11.0f : 5.0f, endIcon.empty() ? 16.0f : 6.0f, endIcon.empty() ? 11.0f : 5.0f})->setSpacing(14);
            b->pressedRadius = 12;
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(icon, 22)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            auto* label = b->add<Label>(name);
            label->setSize(14)->setWeight(here ? 600 : 500)->setMaxWidth(endIcon.empty() ? 150 : 110)->setColor(here ? "on_secondary_container" : "on_surface", false);
            label->grow = true;
            if (!endIcon.empty()) {
                auto* e = iconButton(b, endIcon, end, 20);
                e->tooltip = endTip;
            }
            b->onClicked = [st, path, trash, click](uint32_t button) {
                if (button != BTN_LEFT) return;
                if (click) return click();
                EventLoop::get().defer([st, path, trash] { trash ? goTrash(st) : go(st, path); });
            };
        };
        for (const auto& p : fileops::places()) place(p.icon, p.name, p.path, false);

        // Drives: open ones a click away, with eject; the others opened on a click
        s->add<Node>()->fixedHeight = 8;
        caption(s, "  Devices");
        for (const auto& d : fileops::drives()) {
            const std::string name = !d.label.empty() ? d.label : d.size + " drive";
            const std::string icon = d.removable ? "usb" : "hard_drive";
            auto ejectIt = [st, d] {
                fileops::eject(d, [st, d, alive = st->alive](const std::string& err) {
                    m3::run({"notify-send", "-a", "Files", "-i", "media-eject", err.empty() ? "Safe to remove" : "Not ejected",
                             err.empty() ? (d.label.empty() ? d.device : d.label) + " can be taken out" : err},
                            nullptr);
                    if (!*alive) return;
                    // Not in a drive that's gone
                    if (!d.mountpoint.empty() && (st->dir.string() + "/").starts_with(d.mountpoint + "/")) go(st, paths::home());
                    st->driveSig.clear();
                    st->sidebar();
                });
            };
            if (!d.mountpoint.empty()) {
                place(icon, name, d.mountpoint, false, {}, "eject", ejectIt, "Eject");
            } else {
                place(icon, name, "", false, [st, d] {
                    fileops::mount(d.device, [st, alive = st->alive](const std::string& err, const std::string& at) {
                        if (!err.empty()) return void(m3::run({"notify-send", "-a", "Files", "-i", "drive-removable-media", "Files", err}, nullptr));
                        if (*alive) go(st, at);
                    });
                }, d.removable ? "eject" : "", d.removable ? std::function<void()>(ejectIt) : nullptr, "Power off");
            }
        }
        for (const auto& p : fileops::devices())
            if (p.path == "/") place(p.icon, p.name, p.path, false);

        // Servers: FTP, SFTP and WebDAV, through gvfs
        s->add<Node>()->fixedHeight = 8;
        caption(s, "  Network");
        for (const auto& sv : fileops::servers())
            place("dns", sv.name, sv.path, false, {}, "link_off", [st, sv] {
                if ((st->dir.string() + "/").starts_with(sv.path + "/")) go(st, paths::home());
                fileops::disconnect(sv, [st, alive = st->alive](const std::string&) {
                    if (*alive) st->sidebar();
                });
            }, "Disconnect");
        place("add_link", "Connect to server", "", false, [st] { connectPrompt(st); });
        s->add<Node>()->fixedHeight = 6;
        place("delete", "Trash", "", true);
    };

    // ── The folder: toolbar, files, footer ──
    auto* main = columns->add<Flex>(Flex::Direction::Column);
    main->setSpacing(10)->setAlignItems(Align::Stretch);
    main->grow = true;
    auto* bar = main->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(6)->setAlignItems(Align::Center);
    iconButton(bar, "arrow_back", [st] { stepHistory(st, true); }, 22)->tooltip = "Back (Alt+←)";
    iconButton(bar, "arrow_forward", [st] { stepHistory(st, false); }, 22)->tooltip = "Forward (Alt+→)";
    iconButton(bar, "arrow_upward", [st] {
        if (st->inTrash) return go(st, paths::home());
        if (st->dir != st->dir.root_path()) go(st, st->dir.parent_path());
    }, 22)->tooltip = "Up (Alt+↑)";
    st->crumbs = bar->add<Crumbs>();
    st->crumbs->setSpacing(2)->setAlignItems(Align::Center)->setPadding({6, 4})->setRadius(Radii(22));
    st->crumbs->setBackground("surface_container_high", false);
    st->crumbs->grow = true;
    st->crumbs->givesWay = true;
    // Search: a row under the bar, shown by its button or Ctrl+F
    auto* searchRow = main->add<Flex>(Flex::Direction::Row);
    pill(searchRow->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    searchRow->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    st->search = searchRow->add<TextField>();
    st->search->setPlaceholder("Search this folder");
    st->search->bare = true;
    st->search->grow = true;
    st->search->onChanged = [st](const std::string& t) {
        st->filter = t;
        st->load();
    };
    st->searchRow = searchRow;
    searchRow->setVisible(false);
    st->search->onCancel = [st] {
        st->search->setText("");
        st->filter.clear();
        st->searchRow->setVisible(false);
        st->load();
    };
    iconButton(bar, "search", [st] {
        const bool show = !st->searchRow->visible();
        st->searchRow->setVisible(show);
        if (show) st->search->focus();
        else if (!st->filter.empty()) st->search->onCancel();
    }, 22)->tooltip = "Search (Ctrl+F)";
    iconButton(bar, "right_panel_open", [st] { showPane(st, !Config::get().value<bool>("/files/preview", true)); }, 22)->tooltip =
        "Preview and properties (F9)";
    auto* view = iconButton(bar, listView() ? "grid_view" : "view_list", [] { Config::get().set("/files/listView", !listView()); }, 22);
    view->tooltip = "Grid or list";
    auto* more = bar->add<Button>();
    pill(more)->setPadding(Insets(8));
    more->add<Icon>("more_vert", 22)->setColor("on_surface", false);
    more->tooltip = "More";
    more->onClicked = [st, more](uint32_t button) {
        if (button != BTN_LEFT) return;
        std::vector<MenuItem> m;
        if (!st->inTrash) {
            m.push_back({"create_new_folder", "New folder", "Ctrl+Shift+N", [st] { newFolder(st); }});
            m.push_back({"content_paste", "Paste", "Ctrl+V", [st] { paste(st); }});
        }
        const std::string s = sortBy();
        m.push_back({"sort_by_alpha", "Sort by name", "", [] { Config::get().set("/files/sort", "name"); }, s == "name", false, true, true});
        m.push_back({"schedule", "Sort by date", "", [] { Config::get().set("/files/sort", "date"); }, s == "date"});
        m.push_back({"straighten", "Sort by size", "", [] { Config::get().set("/files/sort", "size"); }, s == "size"});
        m.push_back({"category", "Sort by kind", "", [] { Config::get().set("/files/sort", "kind"); }, s == "kind"});
        m.push_back({"visibility", "Show hidden files", "Ctrl+H", [] { Config::get().set("/files/hidden", !showHidden()); }, showHidden(), false, true, true});
        m.push_back({"image", "Open pictures in the viewer", "", [] { Config::get().set("/files/viewer", !Config::get().value<bool>("/files/viewer", true)); },
                     Config::get().value<bool>("/files/viewer", true)});
        floatingMenu(more, m);
    };
    st->emptyTrash = pillButton(bar, "Empty", false, [st] {
        ask(st, "Empty the trash? What's in it is deleted for good", "", "Empty", true, [](const std::string&) {
            fileops::emptyTrash();
            return std::string();
        }, false);
    }, "delete_forever");
    st->emptyTrash->setVisible(false);

    st->scroll = main->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->scroll->blurEdges = true;
    st->content = st->scroll->add<Backdrop>(st->scroll);
    st->content->setAlignItems(Align::Stretch)->setPadding({4, 4, 12, 4});
    // Files dropped in the folder shown go into it
    st->content->onDropFiles = [st](const std::vector<std::string>& paths) {
        if (!st->inTrash && *st->alive) dropInto(paths, st->dir.string());
    };
    st->content->onDropHover = [st](bool over) { st->content->setBackground(ColorRef("primary", over && !st->inTrash ? 0.07f : 0.0f), false); };
    st->content->onClicked = [st](uint32_t button) {
        if (button == BTN_RIGHT) return folderMenu(st, st->content);
        if (button == BTN_LEFT && !st->selected.empty()) {
            st->selected.clear();
            st->paint();
        }
    };

    st->footer = main->add<Flex>(Flex::Direction::Row);
    st->footer->setSpacing(10)->setAlignItems(Align::Center)->setPadding({8, 2});
    st->status = st->footer->add<Label>("");
    st->status->setSize(13)->setColor("on_surface_variant", false);
    st->status->grow = true;
    // ── The preview and properties of what's selected ──
    auto* paneScroll = columns->add<ScrollView>();
    paneScroll->maxHeight = 1e6f;
    paneScroll->fixedWidth = 300;
    columns->pane = paneScroll;
    columns->paneWanted = Config::get().value<bool>("/files/preview", true);
    st->pane = paneScroll->add<Flex>(Flex::Direction::Column);
    st->pane->setSpacing(10)->setAlignItems(Align::Stretch)->setPadding(Insets(16))->setRadius(Radii(24));
    st->pane->setBackground(ColorRef("surface_container", 0.7f), false);
    st->conns.push_back(Config::get().changed.connect([columns, st, alive = st->alive] {
        const bool on = Config::get().value<bool>("/files/preview", true);
        if (!*alive || on == columns->paneWanted) return;
        columns->paneWanted = on;
        columns->requestLayout();
        st->paneFor.clear();
        if (on && st->details) st->details();
    }));

    st->prompt = main->add<Flex>(Flex::Direction::Row);
    st->prompt->setSpacing(10)->setAlignItems(Align::Center)->setPadding({16, 10})->setRadius(Radii(24));
    st->prompt->setBackground("surface_container_high", false);
    st->prompt->setVisible(false);
}

} // namespace

std::function<void(const std::string&)> filesGo;

// One Files view: the folder, its side bar, the pane. A window holds one
// per tab, and two side by side in the dual view
std::shared_ptr<State> filesPane(Flex* card, Popup& popup, const std::string& start, std::function<bool(const KeyEvent&)>& keys) {
    (void)popup;
    auto st = std::make_shared<State>();
    std::error_code ec;
    st->dir = !start.empty() && fs::is_directory(start, ec) ? fs::path(start) : fs::path(paths::home());
    if (!start.empty() && !fs::is_directory(start, ec) && fs::exists(start, ec)) {
        st->dir = fs::path(start).parent_path();
        st->selected = {start};
    }
    build(st, card);
    card->add<Keeper>(st);

    st->load = [st] {
        st->entries.clear();
        st->origin.clear();
        st->error.clear();
        if (st->inTrash) {
            for (const auto& t : fileops::trashed()) {
                Entry e;
                e.path = t.path;
                e.name = t.name;
                std::error_code ec2;
                e.dir = fs::is_directory(t.path, ec2);
                e.modified = t.deleted;
                if (!e.dir) e.size = fs::file_size(t.path, ec2);
                st->origin[t.path] = t.original;
                st->entries.push_back(std::move(e));
            }
        } else {
            st->entries = fileops::list(st->dir.string(), showHidden(), &st->error);
        }
        // The search: names with what's typed in them
        if (!st->filter.empty()) {
            std::string f = st->filter;
            for (auto& c : f) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::erase_if(st->entries, [&](const Entry& e) {
                std::string n = e.name;
                for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return n.find(f) == std::string::npos;
            });
        }
        const std::string s = sortBy();
        std::ranges::sort(st->entries, [&s](const Entry& a, const Entry& b) {
            if (a.dir != b.dir) return a.dir; // folders first
            if (s == "date" && a.modified != b.modified) return a.modified > b.modified;
            if (s == "size" && a.size != b.size) return a.size > b.size;
            if (s == "kind") {
                const auto ea = fs::path(a.name).extension().string(), eb = fs::path(b.name).extension().string();
                if (ea != eb) return naturalLess(ea, eb);
            }
            return naturalLess(a.name, b.name);
        });
        // Still there: kept selected
        std::erase_if(st->selected, [&](const std::string& p) { return std::ranges::find(st->entries, p, &Entry::path) == st->entries.end(); });
        st->render();
        st->sidebar();
    };

    st->paint = [st] {
        for (auto& [path, tile] : st->tiles) {
            const bool on = isSelected(*st, path);
            tile->setBackground(on ? ColorRef("secondary_container") : ColorRef::none());
            tile->setBorder(on ? 2 : 0, "primary");
        }
        st->showStatus();
        // The pane follows the selection, a beat behind (a drag across many)
        EventLoop::get().debounce(st->detailsTimer, 60, [st, alive = st->alive] {
            if (*alive && st->details) st->details();
        });
    };

    st->showStatus = [st] {
        std::string text;
        if (!st->error.empty()) text = "Can't open this folder: " + st->error;
        else if (st->selected.size() == 1) {
            const auto it = std::ranges::find(st->entries, st->selected.front(), &Entry::path);
            if (it != st->entries.end()) {
                const Kind k = fileops::kindOf(it->path, it->dir);
                text = it->name + "  ·  " + fileops::kindName(k, it->path);
                if (!it->dir) text += "  ·  " + fileops::humanSize(it->size);
                if (it->modified) text += std::string("  ·  ") + (st->inTrash ? "deleted " : "") + fileops::humanTime(it->modified);
                if (st->inTrash && st->origin.contains(it->path)) text += "  ·  from " + fileops::displayPath(fs::path(st->origin[it->path]).parent_path().string());
            }
        } else if (!st->selected.empty()) {
            std::uintmax_t total = 0;
            for (const auto& p : st->selected)
                if (auto it = std::ranges::find(st->entries, p, &Entry::path); it != st->entries.end()) total += it->size;
            text = std::format("{} selected  ·  {}", st->selected.size(), fileops::humanSize(total));
        } else {
            text = st->entries.size() == 1 ? "1 item" : std::format("{} items", st->entries.size());
            if (!st->filter.empty()) text += " matching \"" + st->filter + "\"";
        }
        if (!fileops::status().empty()) text = fileops::status() + "    " + text;
        st->status->setText(text);
    };

    st->render = [st] {
        // The path: each folder a step back to it
        st->crumbs->clearChildren();
        if (st->inTrash) {
            st->crumbs->add<Icon>("delete", 18)->setColor("on_surface_variant", false);
            st->crumbs->add<Label>("Trash")->setSize(14)->setWeight(600);
        } else {
            std::vector<std::pair<std::string, fs::path>> steps;
            const fs::path home = paths::home();
            fs::path p = st->dir;
            for (;;) {
                if (p == home) {
                    steps.push_back({"Home", p});
                    break;
                }
                steps.push_back({p == p.root_path() ? "/" : p.filename().string(), p});
                if (p == p.root_path() || p.parent_path() == p) break;
                p = p.parent_path();
            }
            std::ranges::reverse(steps);
            // The last three; the folders above them in a menu behind "…"
            if (steps.size() > 3) {
                std::vector<std::pair<std::string, fs::path>> above(steps.begin(), steps.end() - 3);
                steps.erase(steps.begin(), steps.end() - 3);
                auto* more = st->crumbs->add<Button>();
                pill(more)->setPadding({10, 5});
                more->add<Icon>("more_horiz", 18)->setColor("on_surface_variant", false);
                more->tooltip = "Folders above";
                more->onClicked = [st, more, above](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    std::vector<MenuItem> m;
                    for (auto it = above.rbegin(); it != above.rend(); ++it) {
                        const fs::path to = it->second;
                        m.emplace_back(it->first == "Home" ? "home" : "folder", it->first, "", [st, to] { go(st, to); });
                    }
                    floatingMenu(more, m);
                };
            }
            for (size_t i = 0; i < steps.size(); ++i) {
                if (i || st->crumbs->children().size()) st->crumbs->add<Icon>("chevron_right", 16)->setColor("on_surface_variant", false);
                const bool last = i + 1 == steps.size();
                auto* b = st->crumbs->add<Button>(Flex::Direction::Row);
                pill(b)->setPadding({10, 5});
                b->givesWay = !last; // short of room, the folders above end in "…", not this one
                b->add<Label>(steps[i].first)->setSize(14)->setWeight(last ? 600 : 500)->setMaxWidth(last ? 220 : 130)->setColor(last ? "on_surface" : "on_surface_variant", false);
                const fs::path to = steps[i].second;
                b->onClicked = [st, to](uint32_t button) {
                    if (button == BTN_LEFT) EventLoop::get().defer([st, to] { go(st, to); });
                };
            }
        }
        st->emptyTrash->setVisible(st->inTrash && !st->entries.empty());

        auto* c = st->content;
        c->clearChildren();
        st->tiles.clear();
        if (st->entries.empty()) {
            auto* none = c->add<Flex>(Flex::Direction::Column);
            none->setAlignItems(Align::Center)->setSpacing(10)->setPadding({0, 80});
            none->add<Icon>(st->inTrash ? "delete" : !st->error.empty() ? "folder_off" : !st->filter.empty() ? "search_off" : "folder_open", 56)
                ->setColor("on_surface_variant", false);
            none->add<Label>(st->inTrash             ? "The trash is empty"
                             : !st->error.empty()    ? "This folder can't be opened"
                             : !st->filter.empty()   ? "Nothing matches \"" + st->filter + "\""
                                                     : "This folder is empty")
                ->setSize(16)
                ->setColor("on_surface_variant", false);
            st->showStatus();
            return;
        }
        constexpr size_t kMax = 3000;
        const bool list = listView();
        Node* host = c;
        if (list) {
            auto* head = c->add<Flex>(Flex::Direction::Row);
            head->setPadding({12, 4})->setSpacing(12);
            auto* n = head->add<Label>("Name");
            n->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
            n->grow = true;
            head->add<Label>(st->inTrash ? "Deleted" : "Modified")->setSize(12)->setWeight(600)->setColor("on_surface_variant", false)->fixedWidth = 150;
            head->add<Label>("Size")->setSize(12)->setWeight(600)->setColor("on_surface_variant", false)->fixedWidth = 80;
        } else {
            auto* flow = c->add<Flow>();
            flow->spacing = 8;
            host = flow;
        }
        size_t shown = 0;
        for (const auto& e : st->entries) {
            if (++shown > kMax) break;
            Button* tile;
            if (list) {
                tile = host->add<Button>(Flex::Direction::Row);
                tile->setPadding({12, 6})->setSpacing(12)->setRadius(Radii(14));
                art(st, tile, e, 26, 26, false);
                auto* name = tile->add<Label>(e.name);
                name->setSize(14)->setMaxWidth(600);
                name->grow = true;
                name->givesWay = true;
                tile->add<Label>(fileops::humanTime(e.modified))->setSize(13)->setColor("on_surface_variant", false)->fixedWidth = 150;
                tile->add<Label>(e.dir ? "" : fileops::humanSize(e.size))->setSize(13)->setColor("on_surface_variant", false)->fixedWidth = 80;
            } else {
                tile = host->add<Button>(Flex::Direction::Column);
                tile->setPadding({8, 8, 8, 10})->setSpacing(6)->setRadius(Radii(20))->setAlignItems(Align::Center);
                tile->fixedWidth = 132;
                tile->hoverLift = true;
                art(st, tile, e, 116, 86, true);
                auto* name = tile->add<Label>(e.name);
                name->setSize(13)->setWeight(500)->setMaxWidth(116)->setMaxLines(2)->setCentered(true);
                if (e.hidden) name->setOpacity(0.6f, false);
            }
            tile->pressedRadius = 12;
            tile->tooltip = e.name;
            const std::string path = e.path;
            tile->onClicked = [st, tile, path](uint32_t button) { clicked(st, tile, path, button); };
            // Dragged out: it, or everything selected with it, to a folder,
            // another app, a browser's upload
            tile->dragFiles = [st, path] { return isSelected(*st, path) ? st->selected : std::vector<std::string>{path}; };
            if (e.dir && !st->inTrash) {
                tile->onDropFiles = [path](const std::vector<std::string>& paths) { dropInto(paths, path); };
                tile->onDropHover = [tile](bool over) { tile->setBorder(over ? 2 : 0, ColorRef("primary")); };
            }
            st->tiles[path] = tile;
        }
        if (shown > kMax) caption(c, std::format("Showing the first {} of {}: search to narrow it down", kMax, st->entries.size()));
        // Another folder (or view): its files arrive one after another;
        // the same one refreshed (a file added) stays put
        const std::string view = std::format("{}|{}|{}|{}", st->inTrash, st->dir.string(), list, st->filter);
        if (view != st->shownView) {
            st->shownView = view;
            stagger(host, list ? 1 : 0);
        }
        st->paint();
    };

    // The pane: a preview of what's selected (or the folder), then its properties
    st->details = [st] {
        if (!st->pane || !st->pane->parentNode() || !Config::get().value<bool>("/files/preview", true)) return;
        const std::string target = st->selected.size() == 1 ? st->selected.front()
                                   : st->selected.empty() && !st->inTrash ? st->dir.string()
                                   : st->selected.empty() ? std::string() : "*" + std::to_string(st->selected.size());
        if (target == st->paneFor) return;
        st->paneFor = target;
        auto* p = st->pane;
        p->clearChildren();
        auto row = [p](const std::string& what, const std::string& value) -> Label* {
            if (value.empty()) return nullptr;
            auto* r = p->add<Flex>(Flex::Direction::Row);
            r->setSpacing(10)->setAlignItems(Align::Start);
            auto* k = r->add<Label>(what);
            k->setSize(12)->setColor("on_surface_variant", false);
            k->fixedWidth = 82;
            auto* v = r->add<Label>(value);
            v->setSize(13)->setMaxWidth(176)->setMaxLines(kAllLines);
            return v;
        };
        if (target.empty()) {
            caption(p, "The trash: select something to see where it came from");
            return;
        }
        if (target.starts_with("*")) {
            // Several: how many, how big, of what kinds
            std::uintmax_t total = 0;
            int folders = 0;
            std::map<std::string, int> kinds;
            for (const auto& path : st->selected)
                if (auto it = std::ranges::find(st->entries, path, &Entry::path); it != st->entries.end()) {
                    total += it->size;
                    if (it->dir) ++folders;
                    else ++kinds[fileops::kindName(fileops::kindOf(it->path), it->path)];
                }
            auto* icon = p->add<Flex>(Flex::Direction::Row);
            icon->setJustify(Align::Center)->setRadius(Radii(18))->setBackground("surface_container_highest", false);
            icon->fixedHeight = 150;
            icon->add<Icon>("library_add_check", 64)->setColor("primary", false);
            p->add<Label>(std::format("{} items selected", st->selected.size()))->setSize(17)->setWeight(600);
            row("Size", fileops::humanSize(total) + (folders ? " (folders not counted)" : ""));
            if (folders) row("Folders", std::to_string(folders));
            for (const auto& [k, n] : kinds) row(k, std::to_string(n));
            return;
        }
        const auto info = fileops::info(target);
        const Kind kind = fileops::kindOf(target, info.dir);

        // The preview
        auto* box = p->add<Flex>(Flex::Direction::Row);
        box->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(18))->setBackground("surface_container_highest", false);
        box->fixedHeight = 190;
        auto again = [st, alive = st->alive] {
            if (!*alive) return;
            st->paneFor.clear();
            if (st->details) st->details();
        };
        std::string picture = kind == Kind::Picture ? (target.ends_with(".svg") ? target : fileops::renderable(target, again))
                            : kind == Kind::Video || (fs::path(target).extension() == ".pdf" && Config::get().value<bool>("/files/pdfThumbnails", true))
                                ? fileops::thumbnail(target, true, [st, alive = st->alive] {
            if (!*alive) return;
            st->paneFor.clear();
            if (st->details) st->details();
        }) : std::string();
        if (!picture.empty()) {
            const bool svg = picture.ends_with(".svg");
            auto* v = box->add<ImageView>(256, 180, [picture, svg](float scale, int& w, int& h) -> GLuint {
                auto* t = svg ? Images::get().icon(picture, static_cast<int>(std::lround(180 * scale)))
                              : Images::get().picture(picture, static_cast<int>(256 * scale), static_cast<int>(180 * scale), false);
                if (!t) return 0;
                w = t->width, h = t->height;
                return t->id;
            });
            v->radius = 12;
        } else if ((kind == Kind::Text || kind == Kind::Code || info.mime.starts_with("text/")) && info.size < 4'000'000) {
            // The start of the text
            std::ifstream in(target);
            std::string text(1200, '\0');
            in.read(text.data(), static_cast<std::streamsize>(text.size()));
            text.resize(static_cast<size_t>(in.gcount()));
            for (auto& c : text)
                if (c == '\t') c = ' ';
            box->setJustify(Align::Start)->setAlignItems(Align::Start)->setPadding(Insets(12));
            auto* t = box->add<Label>(text.empty() ? "(empty)" : text);
            t->setSize(11)->setFamily(Theme::get().monoFamily())->setMaxWidth(244)->setMaxLines(11)->setColor("on_surface_variant", false);
        } else {
            box->add<Icon>(fileops::kindIcon(kind, target), 72)->setFill(kind == Kind::Folder)->setColor(kind == Kind::Folder ? "primary" : "tertiary", false);
        }

        auto* name = p->add<Label>(info.name);
        name->setSize(17)->setWeight(600)->setMaxWidth(268)->setMaxLines(3);
        caption(p, info.kind);
        // At hand: open, extract
        auto* actions = p->add<Flex>(Flex::Direction::Row);
        actions->setSpacing(8);
        if (target != st->dir.string()) pillButton(actions, "Open", true, [st, target] { openPath(st, target); }, info.dir ? "folder_open" : "open_in_new");
        if (fileops::canExtract(target)) pillButton(actions, "Extract", false, [st, target] { extractHere(st, {target}); }, "unarchive");
        if (actions->children().empty()) p->remove(actions);
        p->add<Node>()->fixedHeight = 4;

        // Properties
        if (info.dir) {
            auto* size = row("Size", "Working it out…");
            const std::string path = target;
            fileops::folderSize(path, [st, size, path, alive = st->alive](std::uintmax_t bytes, std::uintmax_t files) {
                if (!*alive || st->paneFor != path) return; // the pane moved on
                size->setText(std::format("{}, {} files", fileops::humanSize(bytes), files));
            });
            row("Contains", info.children == 1 ? "1 item" : std::format("{} items", info.children));
        } else {
            row("Size", std::format("{} ({} bytes)", fileops::humanSize(info.size), info.size));
        }
        row("Where", fileops::displayPath(fs::path(target).parent_path().string()));
        if (st->inTrash && st->origin.contains(target)) row("From", fileops::displayPath(st->origin[target]));
        row("Modified", fileops::humanTime(info.modified));
        row("Opened", fileops::humanTime(info.accessed));
        if (info.created) row("Created", fileops::humanTime(info.created));
        row("Type", info.mime);
        if (info.link) row("Link to", info.linkTarget);
        row("Owner", info.owner + (info.group.empty() || info.group == info.owner ? "" : " · group " + info.group));
        row("Access", info.permissions + (info.writable ? "" : " · read only for you"));
        if (info.totalSpace) row("Drive", std::format("{} free of {}", fileops::humanSize(info.freeSpace), fileops::humanSize(info.totalSpace)));
        // A program or script: allowed to run, or not
        if (!info.dir && info.writable && (kind == Kind::Other || kind == Kind::Code || kind == Kind::Package || info.executable)) {
            auto* r = p->add<Flex>(Flex::Direction::Row);
            r->setSpacing(10)->setAlignItems(Align::Center)->setPadding({0, 4});
            auto* l = r->add<Label>("Allow running it as a program");
            l->setSize(13)->setMaxWidth(190)->setMaxLines(2);
            l->grow = true;
            auto* sw = r->add<Switch>();
            sw->setChecked(info.executable, false);
            const std::string path = target;
            sw->onToggled = [path, sw](bool on) {
                if (const auto err = fileops::setExecutable(path, on); !err.empty()) sw->setChecked(!on);
            };
        }
        if (!info.dir && kind != Kind::Folder && target != st->dir.string())
            pillButton(p, "Open with…", false, [st, target] { openWith(st, nullptr, target); }, "apps");
    };

    // Drives plugged in or out, servers connected: the side bar follows
    st->driveTimer = EventLoop::get().addTimer(4000, [st, alive = st->alive] {
        if (!*alive) return;
        std::string sig;
        for (const auto& d : fileops::drives()) sig += d.device + d.mountpoint + ";";
        for (const auto& sv : fileops::servers()) sig += sv.path + ";";
        if (sig == st->driveSig) return;
        const bool first = st->driveSig.empty() && sig.empty();
        st->driveSig = sig;
        if (!first && st->sidebar) st->sidebar();
    }, true);

    // Kept up: the folder changing, an operation ending, the settings
    auto alive = st->alive;
    st->conns.push_back(g_dirChanged.connect([st, alive] {
        if (!*alive) return;
        if (g_changedDir == (st->inTrash ? fileops::trashFiles() : st->dir.string())) st->load();
    }));
    st->conns.push_back(fileops::changed().connect([st, alive] {
        if (*alive) st->load();
    }));
    st->conns.push_back(Config::get().changed.connect([st, alive] {
        static std::string last;
        const std::string now = std::format("{}{}{}{}", listView(), showHidden(), sortBy(), Config::get().value<bool>("/files/viewer", true));
        if (!*alive || now == last) return;
        last = now;
        st->load();
    }));

    // Keys
    keys = [st](const KeyEvent& e) {
        const bool ctrl = e.ctrl(), shift = e.shift(), alt = e.mods & ModAlt;
        auto sel = st->selected;
        if (alt && e.sym == XKB_KEY_Left) return stepHistory(st, true), true;
        if (alt && e.sym == XKB_KEY_Right) return stepHistory(st, false), true;
        if ((alt && e.sym == XKB_KEY_Up) || e.sym == XKB_KEY_BackSpace) {
            if (!st->inTrash && st->dir != st->dir.root_path()) go(st, st->dir.parent_path());
            return true;
        }
        if (ctrl && (e.baseSym == XKB_KEY_f || e.baseSym == XKB_KEY_l)) {
            st->searchRow->setVisible(true);
            st->search->focus();
            return true;
        }
        if (ctrl && e.baseSym == XKB_KEY_h) return Config::get().set("/files/hidden", !showHidden()), true;
        if (ctrl && e.baseSym == XKB_KEY_a) {
            st->selected.clear();
            for (const auto& x : st->entries) st->selected.push_back(x.path);
            st->paint();
            return true;
        }
        if (ctrl && shift && e.baseSym == XKB_KEY_n && !st->inTrash) return newFolder(st), true;
        if (ctrl && e.baseSym == XKB_KEY_v && !st->inTrash) return paste(st), true;
        if (e.sym == XKB_KEY_F9) return showPane(st, !Config::get().value<bool>("/files/preview", true)), true;
        if (sel.empty()) return false;
        // Space: a picture in the viewer; anything else, its preview
        if (e.sym == XKB_KEY_space && sel.size() == 1) {
            if (fileops::isPicture(sel.front()) && open) open("viewer " + sel.front());
            else showPane(st, true);
            return true;
        }
        if ((e.mods & ModAlt) && (e.sym == XKB_KEY_Return || e.sym == XKB_KEY_KP_Enter)) return showPane(st, true), true;
        if (ctrl && e.baseSym == XKB_KEY_c && !st->inTrash) return setClipboard(sel, false), true;
        if (ctrl && e.baseSym == XKB_KEY_x && !st->inTrash) return setClipboard(sel, true), true;
        if (e.sym == XKB_KEY_Delete) {
            if (shift || st->inTrash) deleteForGood(st, sel);
            else toTrash(st, sel);
            return true;
        }
        if (e.sym == XKB_KEY_F2 && sel.size() == 1 && !st->inTrash) return renameOne(st, sel.front()), true;
        if ((e.sym == XKB_KEY_Return || e.sym == XKB_KEY_KP_Enter) && sel.size() == 1 && !st->inTrash) return openPath(st, sel.front()), true;
        return false;
    };

    watch(st->dir.string());
    st->load();
    return st;
}

// ── The window: tabs, and the dual view ──

namespace {

// A pane's box: the one the pointer was last over takes the keys
class PaneBox : public Flex {
public:
    PaneBox() : Flex(Direction::Column) {}
    std::function<void()> onEnter;
    void onHoverWithin(bool in) override {
        if (in && onEnter) onEnter();
    }
};

struct Window {
    struct Tab {
        std::shared_ptr<State> st;
        PaneBox* box = nullptr;
        std::function<bool(const KeyEvent&)> keys;
    };
    std::vector<Tab> tabs;
    int active = 0;
    Tab second; // the dual view's other side
    bool dual = false;
    bool secondActive = false;
    Flex *strip = nullptr, *left = nullptr, *right = nullptr;
    Popup* popup = nullptr;
    std::function<void()> rebuild;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void addTab(const std::shared_ptr<Window>& w, const std::string& dir);

void showTab(const std::shared_ptr<Window>& w, int i) {
    if (w->tabs.empty()) return;
    w->active = std::clamp(i, 0, static_cast<int>(w->tabs.size()) - 1);
    for (size_t k = 0; k < w->tabs.size(); ++k) w->tabs[k].box->setVisible(static_cast<int>(k) == w->active);
    w->secondActive = false;
    if (w->rebuild) w->rebuild();
}

void closeTab(const std::shared_ptr<Window>& w, int i) {
    if (w->tabs.size() <= 1) {
        if (w->popup) w->popup->close();
        return;
    }
    auto box = w->tabs[static_cast<size_t>(i)].box;
    w->tabs.erase(w->tabs.begin() + i);
    EventLoop::get().defer([w, box] {
        if (*w->alive) w->left->remove(box);
    });
    showTab(w, std::min(i, static_cast<int>(w->tabs.size()) - 1));
}

void hookPane(const std::shared_ptr<Window>& w, const std::shared_ptr<State>& st) {
    std::weak_ptr<Window> weak = w;
    st->openTab = [weak](const std::string& dir) {
        if (auto x = weak.lock(); x && *x->alive) EventLoop::get().defer([x, dir] {
            if (*x->alive) addTab(x, dir);
        });
    };
    st->moved = [weak] {
        if (auto x = weak.lock(); x && *x->alive && x->rebuild) x->rebuild();
    };
}

void addTab(const std::shared_ptr<Window>& w, const std::string& dir) {
    Window::Tab t;
    t.box = w->left->add<PaneBox>();
    t.box->setAlignItems(Align::Stretch);
    t.box->grow = true;
    t.st = filesPane(t.box, *w->popup, dir, t.keys);
    hookPane(w, t.st);
    std::weak_ptr<Window> weak = w;
    t.box->onEnter = [weak] {
        if (auto x = weak.lock()) x->secondActive = false;
    };
    w->tabs.push_back(std::move(t));
    showTab(w, static_cast<int>(w->tabs.size()) - 1);
}

void toggleDual(const std::shared_ptr<Window>& w) {
    w->dual = !w->dual;
    if (w->dual && !w->second.box) {
        w->second.box = w->right->add<PaneBox>();
        w->second.box->setAlignItems(Align::Stretch);
        w->second.box->grow = true;
        const std::string dir = w->tabs.empty() ? paths::home() : w->tabs[static_cast<size_t>(w->active)].st->dir.string();
        w->second.st = filesPane(w->second.box, *w->popup, dir, w->second.keys);
        hookPane(w, w->second.st);
        std::weak_ptr<Window> weak = w;
        w->second.box->onEnter = [weak] {
            if (auto x = weak.lock()) x->secondActive = true;
        };
    }
    w->right->setVisible(w->dual);
    if (w->rebuild) w->rebuild();
}

std::shared_ptr<State> activeState(const std::shared_ptr<Window>& w) {
    if (w->dual && w->secondActive && w->second.st) return w->second.st;
    return w->tabs.empty() ? nullptr : w->tabs[static_cast<size_t>(w->active)].st;
}

} // namespace

void files(Flex* card, Popup& popup, const std::string& start) {
    auto w = std::make_shared<Window>();
    w->popup = &popup;
    card->setPadding(Insets(0))->setSpacing(4);
    w->strip = card->add<Flex>(Flex::Direction::Row);
    w->strip->setSpacing(4)->setAlignItems(Align::Center)->setPadding({14, 6, 14, 0});
    auto* body = card->add<Flex>(Flex::Direction::Row);
    body->setAlignItems(Align::Stretch)->setSpacing(6);
    body->grow = true;
    w->left = body->add<Flex>(Flex::Direction::Column);
    w->left->setAlignItems(Align::Stretch);
    w->left->grow = true;
    w->left->fixedWidth = 1; // grown from nothing: two views split evenly
    w->right = body->add<Flex>(Flex::Direction::Column);
    w->right->setAlignItems(Align::Stretch);
    w->right->grow = true;
    w->right->fixedWidth = 1;
    w->right->setVisible(false);

    w->rebuild = [w] {
        auto* s = w->strip;
        s->clearChildren();
        // One tab: no strip to speak of, just its buttons
        for (size_t i = 0; i < w->tabs.size() && w->tabs.size() > 1; ++i) {
            const bool here = static_cast<int>(i) == w->active;
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({12, 5, 6, 5})->setSpacing(6);
            b->setBackground(here ? "secondary_container" : "surface_container_high", false);
            b->add<Icon>("folder", 16)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            const auto& d = w->tabs[i].st->dir;
            b->add<Label>(w->tabs[i].st->inTrash ? "Trash" : d == fs::path(paths::home()) ? "Home" : d.filename().string().empty() ? d.string() : d.filename().string())
                ->setSize(13)
                ->setWeight(here ? 600 : 500)
                ->setMaxWidth(150)
                ->setColor(here ? "on_secondary_container" : "on_surface", false);
            b->givesWay = true;
            b->onClicked = [w, i](uint32_t btn) {
                if (btn == BTN_LEFT) EventLoop::get().defer([w, i] {
                    if (*w->alive) showTab(w, static_cast<int>(i));
                });
                if (btn == BTN_MIDDLE) EventLoop::get().defer([w, i] {
                    if (*w->alive) closeTab(w, static_cast<int>(i));
                });
            };
            iconButton(b, "close", [w, i] {
                EventLoop::get().defer([w, i] {
                    if (*w->alive) closeTab(w, static_cast<int>(i));
                });
            }, 14)->setPadding(Insets(3));
        }
        iconButton(s, "add", [w] {
            EventLoop::get().defer([w] {
                if (!*w->alive) return;
                auto st = activeState(w);
                addTab(w, st ? st->dir.string() : paths::home());
            });
        }, 18)->tooltip = "New tab (Ctrl+T); a middle click on a folder opens it in one";
        s->add<Node>()->grow = true;
        auto* dual = iconButton(s, w->dual ? "view_agenda" : "view_column_2", [w] {
            EventLoop::get().defer([w] {
                if (*w->alive) toggleDual(w);
            });
        }, 18);
        dual->tooltip = w->dual ? "One view (F3)" : "Two views side by side (F3)";
        if (w->dual) dual->setBackground("secondary_container", false);
    };

    popup.onKey = [w](const KeyEvent& e) {
        const bool ctrl = e.ctrl();
        const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
        if (ctrl && k == 't') {
            auto st = activeState(w);
            addTab(w, st ? st->dir.string() : paths::home());
            return true;
        }
        if (ctrl && k == 'w') return closeTab(w, w->active), true;
        if (ctrl && (e.sym == XKB_KEY_Tab || e.sym == XKB_KEY_ISO_Left_Tab || e.sym == XKB_KEY_Page_Down || e.sym == XKB_KEY_Page_Up)) {
            const int n = static_cast<int>(w->tabs.size());
            const int dir = (e.shift() || e.sym == XKB_KEY_ISO_Left_Tab || e.sym == XKB_KEY_Page_Up) ? -1 : 1;
            showTab(w, ((w->active + dir) % n + n) % n);
            return true;
        }
        if (e.sym == XKB_KEY_F3) return toggleDual(w), true;
        if ((e.mods & ModAlt) && e.sym >= XKB_KEY_1 && e.sym <= XKB_KEY_9) {
            const int i = static_cast<int>(e.sym - XKB_KEY_1);
            if (i < static_cast<int>(w->tabs.size())) return showTab(w, i), true;
        }
        // The rest: the pane the pointer was over
        if (w->dual && w->secondActive && w->second.keys) return w->second.keys(e);
        if (!w->tabs.empty() && w->tabs[static_cast<size_t>(w->active)].keys) return w->tabs[static_cast<size_t>(w->active)].keys(e);
        return false;
    };
    // `material3ui-files <path>` with the window open: there
    filesGo = [w](const std::string& to) {
        if (!*w->alive || to.empty()) return;
        auto st = activeState(w);
        if (!st) return;
        std::error_code ec2;
        if (fs::is_directory(to, ec2)) go(st, to);
        else if (fs::exists(to, ec2)) {
            go(st, fs::path(to).parent_path());
            st->selected = {to};
            st->paint();
        }
    };
    struct WindowKeeper : Node {
        std::shared_ptr<Window> w;
        explicit WindowKeeper(std::shared_ptr<Window> x) : w(std::move(x)) { floating = true; }
        ~WindowKeeper() override {
            *w->alive = false;
            w->rebuild = nullptr;
        }
        SizeF doMeasure(SizeF) override { return {0, 0}; }
    };
    card->add<WindowKeeper>(w);
    addTab(w, start);
}


void filesClosed() {
    filesGo = nullptr;
}

// Settings → Files (the gear in its title bar): what Files reads anyway,
// applied as they change
void filesSettings(Flex* page) {
    auto* g = group(page, "Browsing");
    switchRow(g, "visibility", "Show hidden files", "Names starting with a dot (Ctrl+H)", "/files/hidden", false);
    switchRow(g, "view_list", "Show folders as a list", "With dates and sizes; off, a grid of previews", "/files/listView", false);
    choiceRow<std::string>(g, "sort", "Sort by", "Folders always come first", "/files/sort", "name",
                           {{"name", "Name"}, {"date", "Date"}, {"size", "Size"}, {"kind", "Kind"}});
    switchRow(g, "vertical_split", "Preview and properties", "The pane beside the folder (F9)", "/files/preview", true);
    switchRow(g, "picture_as_pdf", "PDF previews", "A PDF's first page as its picture in the grid (pdftoppm)", "/files/pdfThumbnails", true);
    auto* o = group(page, "Open with Material3UI's apps", "Off: the file's own default app opens it");
    switchRow(o, "image", "Pictures in Photos", "", "/files/viewer", true);
    switchRow(o, "library_music", "Songs in Music", "", "/files/music", true);
    switchRow(o, "movie", "Videos in Videos", "", "/files/player", true);
    switchRow(o, "picture_as_pdf", "PDFs in the PDF viewer", "", "/files/pdf", true);
    switchRow(o, "font_download", "Fonts in Fonts", "", "/files/fonts", true);
    switchRow(o, "edit_note", "Text and code in the Text Editor", "", "/files/textEditor", true);
}

} // namespace m3::shell::panels
