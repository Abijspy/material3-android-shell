// Books (material3ui-books): an EPUB reader. A library of the books in
// Books, Documents and Downloads (and any opened from Files), with covers
// and how far each is read; reading chapter by chapter in a calm column,
// with the contents, text size, a serif or sans face, paper, night or the
// theme's colours, and every book reopened where it was left.
//
// An EPUB is a zip of XHTML: unpacked once (bsdtar) into the cache, its
// package (OPF) read for the title, author, cover, chapters and contents,
// and each chapter drawn with the shell's widgets.
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

#include <glib.h>
#include <nlohmann/json.hpp>
#include <pango/pango.h>

#include <algorithm>
#include <cstring>
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

std::function<void(const std::string&)> booksGo;

namespace {

std::string cacheDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.cache") + "/material3ui/books";
}
std::string stateFile() {
    return paths::stateDir() + "/books.json";
}
std::string md5(const std::string& s) {
    gchar* h = g_compute_checksum_for_string(G_CHECKSUM_MD5, s.c_str(), static_cast<gssize>(s.size()));
    std::string out = h;
    g_free(h);
    return out;
}
std::string readAll(const std::string& f) {
    std::ifstream in(f, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
std::string lowerOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ── XML, leniently: attributes and the text of the first matching tag

std::string attr(const std::string& tag, const std::string& name) {
    for (const char q : {'"', '\''}) {
        const std::string key = name + "=" + q;
        size_t at = 0;
        while ((at = tag.find(key, at)) != std::string::npos) {
            if (at == 0 || tag[at - 1] == ' ' || tag[at - 1] == '\t' || tag[at - 1] == '\n') {
                const size_t v = at + key.size(), e = tag.find(q, v);
                if (e != std::string::npos) return tag.substr(v, e - v);
            }
            at += key.size();
        }
    }
    return "";
}
// Every <name …> tag (also with a namespace prefix, dc:title), as its text
std::vector<std::string> tags(const std::string& xml, const std::string& name) {
    std::vector<std::string> out;
    size_t at = 0;
    while ((at = xml.find('<', at)) != std::string::npos) {
        size_t n = at + 1;
        const size_t end = xml.find('>', n);
        if (end == std::string::npos) break;
        std::string head = xml.substr(n, std::min(end, xml.find_first_of(" \t\n/>", n)) - n);
        if (const size_t colon = head.find(':'); colon != std::string::npos) head = head.substr(colon + 1);
        if (head == name) out.push_back(xml.substr(at, end - at + 1));
        at = end;
    }
    return out;
}
std::string innerText(const std::string& xml, const std::string& name) {
    for (const std::string pre : {"", "dc:"}) {
        const size_t a = xml.find("<" + pre + name);
        if (a == std::string::npos) continue;
        const size_t s = xml.find('>', a), e = xml.find("</" + pre + name, s);
        if (s == std::string::npos || e == std::string::npos) continue;
        std::string t = xml.substr(s + 1, e - s - 1);
        // Entities, roughly
        for (const auto& [from, to] : std::initializer_list<std::pair<const char*, const char*>>{{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}})
            for (size_t p; (p = t.find(from)) != std::string::npos;) t.replace(p, std::strlen(from), to);
        return t;
    }
    return "";
}
std::string joinPath(const std::string& base, const std::string& href) {
    std::string h = href.substr(0, href.find('#'));
    gchar* u = g_uri_unescape_string(h.c_str(), nullptr);
    if (u) h = u, g_free(u);
    return fs::weakly_canonical(fs::path(base) / h).string();
}

struct Chapter {
    std::string file, title;
};
struct Book {
    std::string path, title, author, cover, dir; // dir: where it's unpacked
    std::vector<Chapter> chapters;
    bool opened = false;
};

// Unpacked once, then read: its title, author, cover and chapters
bool openBook(Book& b) {
    std::error_code ec;
    b.dir = cacheDir() + "/" + md5(b.path + std::to_string(fs::file_size(b.path, ec)));
    if (!fs::exists(b.dir + "/.done", ec)) {
        fs::create_directories(b.dir, ec);
        const int rc = std::system(("bsdtar -xf '" + [&] {
            std::string q;
            for (char c : b.path) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
            return q;
        }() + "' -C '" + b.dir + "' 2>/dev/null").c_str());
        if (rc != 0) return false;
        std::ofstream(b.dir + "/.done") << "1";
    }
    const std::string container = readAll(b.dir + "/META-INF/container.xml");
    const auto roots = tags(container, "rootfile");
    if (roots.empty()) return false;
    const std::string opfPath = b.dir + "/" + attr(roots.front(), "full-path");
    const std::string opf = readAll(opfPath), base = fs::path(opfPath).parent_path().string();
    b.title = innerText(opf, "title");
    b.author = innerText(opf, "creator");
    // The manifest: id → file (and the cover, and the contents)
    std::map<std::string, std::string> files;
    std::string coverId, navFile, ncxFile;
    for (const auto& m : tags(opf, "meta"))
        if (attr(m, "name") == "cover") coverId = attr(m, "content");
    for (const auto& item : tags(opf, "item")) {
        const std::string id = attr(item, "id"), href = attr(item, "href"), props = attr(item, "properties"), type = attr(item, "media-type");
        files[id] = joinPath(base, href);
        if (props.find("cover-image") != std::string::npos || (id == coverId && type.starts_with("image/"))) b.cover = files[id];
        if (props.find("nav") != std::string::npos) navFile = files[id];
        if (type == "application/x-dtbncx+xml") ncxFile = files[id];
    }
    // Chapter titles from the contents (EPUB 3 nav, or EPUB 2 NCX)
    std::map<std::string, std::string> titleOf;
    if (!navFile.empty()) {
        const std::string nav = readAll(navFile);
        size_t at = 0;
        while ((at = nav.find("<a ", at)) != std::string::npos) {
            const size_t gt = nav.find('>', at), end = nav.find("</a>", gt);
            if (gt == std::string::npos || end == std::string::npos) break;
            std::string text = nav.substr(gt + 1, end - gt - 1), plain;
            bool in = false;
            for (char c : text) in = c == '<' ? true : c == '>' ? false : in, plain += (!in && c != '>') ? std::string(1, c) : "";
            const std::string file = joinPath(fs::path(navFile).parent_path().string(), attr(nav.substr(at, gt - at), "href"));
            if (!titleOf.contains(file)) titleOf[file] = plain;
            at = end;
        }
    } else if (!ncxFile.empty()) {
        const std::string ncx = readAll(ncxFile);
        size_t at = 0;
        while ((at = ncx.find("<navPoint", at)) != std::string::npos) {
            const size_t next = ncx.find("<navPoint", at + 9);
            const std::string point = ncx.substr(at, next == std::string::npos ? std::string::npos : next - at);
            const auto content = tags(point, "content");
            if (!content.empty()) {
                const std::string file = joinPath(fs::path(ncxFile).parent_path().string(), attr(content.front(), "src"));
                if (!titleOf.contains(file)) titleOf[file] = innerText(innerText(point, "navLabel"), "text");
            }
            at = next == std::string::npos ? ncx.size() : next;
        }
    }
    b.chapters.clear();
    for (const auto& ref : tags(opf, "itemref")) {
        if (attr(ref, "linear") == "no") continue;
        auto it = files.find(attr(ref, "idref"));
        if (it == files.end()) continue;
        b.chapters.push_back({it->second, titleOf.contains(it->second) ? titleOf[it->second] : ""});
    }
    if (b.title.empty()) b.title = fs::path(b.path).stem().string();
    b.opened = true;
    return !b.chapters.empty();
}

// ── A chapter's XHTML as blocks of Pango markup

struct Block {
    enum Kind { Para, Heading, Quote, Item, Pre, Rule, Image } kind = Para;
    std::string text; // markup (Pre: plain)
    int level = 0;    // a heading's
    std::string image;
};

std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
    return out;
}
std::string entity(const std::string& e) {
    if (e == "amp") return "&amp;";
    if (e == "lt") return "&lt;";
    if (e == "gt") return "&gt;";
    if (e == "quot") return "\"";
    if (e == "apos") return "'";
    if (e == "nbsp") return " ";
    if (e == "mdash") return "—";
    if (e == "ndash") return "–";
    if (e == "hellip") return "…";
    if (e == "rsquo") return "’";
    if (e == "lsquo") return "‘";
    if (e == "rdquo") return "”";
    if (e == "ldquo") return "“";
    if (!e.empty() && e[0] == '#') {
        const gunichar c = static_cast<gunichar>(e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16) : std::strtoul(e.c_str() + 1, nullptr, 10));
        char buf[8];
        std::string s(buf, static_cast<size_t>(g_unichar_to_utf8(c, buf)));
        return esc(s);
    }
    return "";
}

std::vector<Block> htmlBlocks(const std::string& html, const std::string& dir) {
    std::vector<Block> out;
    Block cur;
    std::vector<std::string> open; // inline markup tags open
    bool skip = false, pre = false;
    int quote = 0;
    auto flush = [&] {
        // Inline tags closed, spaces squeezed, empty ones dropped
        for (auto it = open.rbegin(); it != open.rend(); ++it) cur.text += "</" + *it + ">";
        std::string t;
        if (cur.kind == Block::Pre) t = cur.text;
        else
            for (char c : cur.text)
                if (!((c == ' ' || c == '\n') && (t.empty() || t.back() == ' '))) t += c == '\n' ? ' ' : c;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (!t.empty() && t.find_first_not_of(" ") != std::string::npos) {
            cur.text = t;
            if (!pango_parse_markup(t.c_str(), -1, 0, nullptr, nullptr, nullptr, nullptr)) {
                // Not valid after all: the text alone
                std::string plain;
                bool in = false;
                for (char c : t) in = c == '<' ? true : c == '>' ? false : in, plain += (!in && c != '>') ? std::string(1, c) : "";
                cur.text = plain;
            }
            out.push_back(cur);
        }
        const Block::Kind keep = quote > 0 ? Block::Quote : Block::Para;
        cur = Block{};
        cur.kind = keep;
        std::vector<std::string> reopen = open;
        for (const auto& o : reopen) cur.text += "<" + o + ">";
    };
    auto body = html.find("<body");
    size_t i = body == std::string::npos ? 0 : html.find('>', body) + 1;
    for (; i < html.size(); ++i) {
        const char c = html[i];
        if (c == '<') {
            const size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            const std::string tag = html.substr(i + 1, end - i - 1);
            i = end;
            const bool closing = !tag.empty() && tag[0] == '/';
            std::string name = lowerOf(tag.substr(closing ? 1 : 0, tag.find_first_of(" \t\n/", closing ? 1 : 0) - (closing ? 1 : 0)));
            if (const size_t colon = name.find(':'); colon != std::string::npos) name = name.substr(colon + 1);
            if (name == "script" || name == "style") {
                skip = !closing;
                continue;
            }
            if (skip) continue;
            if (name == "p" || name == "div" || name == "section" || name == "br" || name == "tr" || name == "dd" || name == "dt" || name == "figcaption") {
                if (name == "br" && !closing && cur.kind == Block::Pre) cur.text += "\n";
                else flush();
            } else if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                flush();
                if (!closing) cur.kind = Block::Heading, cur.level = name[1] - '0';
            } else if (name == "blockquote") {
                flush();
                quote += closing ? -1 : 1;
                quote = std::max(0, quote);
                cur.kind = quote > 0 ? Block::Quote : Block::Para;
            } else if (name == "li") {
                flush();
                if (!closing) cur.kind = Block::Item;
            } else if (name == "pre") {
                flush();
                pre = !closing;
                if (pre) cur.kind = Block::Pre;
            } else if (name == "hr") {
                flush();
                out.push_back({Block::Rule, "", 0, ""});
            } else if ((name == "img" || name == "image") && !closing) {
                std::string src = attr(tag, "src");
                if (src.empty()) src = attr(tag, "xlink:href");
                if (src.empty()) src = attr(tag, "href");
                if (!src.empty()) {
                    flush();
                    out.push_back({Block::Image, esc(attr(tag, "alt")), 0, joinPath(dir, src)});
                }
            } else {
                // Inline: bold, italic, code, small capitals; links underlined
                static const std::map<std::string, std::string> inl = {{"b", "b"}, {"strong", "b"}, {"i", "i"}, {"em", "i"}, {"cite", "i"}, {"code", "tt"},
                                                                       {"tt", "tt"}, {"sup", "sup"}, {"sub", "sub"}, {"u", "u"}, {"a", "u"}, {"s", "s"}, {"del", "s"}, {"small", "small"}};
                auto it = inl.find(name);
                if (it == inl.end() || tag.ends_with("/")) continue;
                if (!closing) {
                    cur.text += "<" + it->second + ">";
                    open.push_back(it->second);
                } else if (auto o = std::find(open.rbegin(), open.rend(), it->second); o != open.rend()) {
                    // Closed, with whatever opened inside it
                    const size_t idx = static_cast<size_t>(open.rend() - o) - 1;
                    for (size_t k = open.size(); k > idx; --k) cur.text += "</" + open[k - 1] + ">";
                    open.erase(open.begin() + static_cast<long>(idx));
                    for (size_t k = idx; k < open.size(); ++k) cur.text += "<" + open[k] + ">";
                }
            }
            continue;
        }
        if (skip) continue;
        if (c == '&') {
            const size_t semi = html.find(';', i);
            if (semi != std::string::npos && semi - i < 12) {
                cur.text += entity(html.substr(i + 1, semi - i - 1));
                i = semi;
                continue;
            }
        }
        if (cur.kind == Block::Pre) cur.text += c;
        else cur.text += c == '\r' || c == '\t' ? ' ' : c;
    }
    flush();
    return out;
}

// ── The library and where each book was left

struct State {
    std::vector<Book> library;
    std::map<std::string, std::pair<int, float>> place; // path → chapter, how far down it (0…1)
    std::map<std::string, long> lastRead;
    int book = -1, chapter = 0;
    bool showContents = false;
    Flex *root = nullptr, *content = nullptr;
    ScrollView* scroll = nullptr;
    std::function<void()> draw;
    EventLoop::TimerId saveTimer = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void save(const std::shared_ptr<State>& st) {
    json j = json::object(), extra = json::array();
    for (const auto& [p, v] : st->place) j[p] = {{"chapter", v.first}, {"at", v.second}, {"read", st->lastRead[p]}};
    for (const auto& b : st->library) extra.push_back(b.path);
    std::error_code ec;
    fs::create_directories(paths::stateDir(), ec);
    paths::writeFile(stateFile(), json{{"books", j}, {"known", extra}}.dump());
}
void rememberPlace(const std::shared_ptr<State>& st) {
    if (st->book < 0 || !st->scroll) return;
    const float range = st->scroll->scrollRange();
    st->place[st->library[static_cast<size_t>(st->book)].path] = {st->chapter, range > 0 ? st->scroll->scrollOffset() / range : 0};
    st->lastRead[st->library[static_cast<size_t>(st->book)].path] = std::time(nullptr);
    std::weak_ptr<State> weak = st;
    EventLoop::get().debounce(st->saveTimer, 1500, [weak] {
        if (auto s = weak.lock()) save(s);
    });
}

void scan(const std::shared_ptr<State>& st) {
    std::set<std::string> paths;
    std::error_code ec;
    for (const char* sub : {"/Books", "/Documents", "/Downloads", "/Documents/Books"})
        for (auto it = fs::recursive_directory_iterator(paths::home() + sub, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 2) it.disable_recursion_pending();
            if (it->is_regular_file(ec) && lowerOf(it->path().extension().string()) == ".epub") paths.insert(it->path().string());
        }
    // Books opened from elsewhere, remembered
    try {
        std::ifstream in(stateFile());
        if (in) {
            const json j = json::parse(in);
            for (const auto& p : j.value("known", json::array()))
                if (p.is_string() && fs::exists(p.get<std::string>(), ec)) paths.insert(p.get<std::string>());
            const json books = j.value("books", json::object());
            for (const auto& [p, v] : books.items()) {
                st->place[p] = {v.value("chapter", 0), v.value("at", 0.0f)};
                st->lastRead[p] = v.value("read", 0L);
            }
        }
    } catch (...) {
    }
    st->library.clear();
    for (const auto& p : paths) {
        Book b;
        b.path = p;
        if (openBook(b)) st->library.push_back(std::move(b));
    }
    // Lately read first, then by title
    std::ranges::sort(st->library, [&](const Book& a, const Book& b) {
        const long ra = st->lastRead[a.path], rb = st->lastRead[b.path];
        return ra != rb ? ra > rb : g_utf8_collate(a.title.c_str(), b.title.c_str()) < 0;
    });
}

// ── Drawing

struct Look {
    ColorRef bg, ink, dim;
};
Look look() {
    const std::string theme = Config::get().value<std::string>("/books/theme", "paper");
    if (theme == "paper") return {ColorRef(Color{0.97f, 0.94f, 0.88f, 1}), ColorRef(Color{0.2f, 0.17f, 0.13f, 1}), ColorRef(Color{0.45f, 0.4f, 0.33f, 1})};
    if (theme == "night") return {ColorRef(Color{0.07f, 0.07f, 0.08f, 1}), ColorRef(Color{0.82f, 0.8f, 0.76f, 1}), ColorRef(Color{0.55f, 0.53f, 0.5f, 1})};
    return {ColorRef("surface_container_lowest"), ColorRef("on_surface"), ColorRef("on_surface_variant")};
}

void cover(Node* parent, const Book& b, float w, float h) {
    std::error_code ec;
    if (!b.cover.empty() && fs::exists(b.cover, ec)) {
        auto* v = parent->add<ImageView>(w, h, [file = b.cover, w, h](float scale, int& tw, int& th) -> GLuint {
            auto* t = Images::get().picture(file, static_cast<int>(w * scale), static_cast<int>(h * scale), true);
            if (!t) return 0;
            tw = t->width, th = t->height;
            return t->id;
        });
        v->cover = true;
        v->radius = 10;
        return;
    }
    auto* box = parent->add<Flex>(Flex::Direction::Column);
    box->setPadding(Insets(12))->setSpacing(6)->setRadius(Radii(10))->setBackground("tertiary_container", false)->setJustify(Align::Center);
    box->fixedWidth = w, box->fixedHeight = h;
    box->add<Label>(b.title)->setSize(14)->setWeight(600)->setMaxWidth(w - 24)->setMaxLines(4)->setColor("on_tertiary_container", false);
    box->add<Label>(b.author)->setSize(11)->setMaxWidth(w - 24)->setColor("on_tertiary_container", false);
}

void openAt(std::shared_ptr<State> st, int book, int chapter, float at);

void drawLibrary(const std::shared_ptr<State>& st) {
    auto* c = st->content;
    c->setBackground(ColorRef::none(), false);
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8)->setAlignItems(Align::Center);
    auto* t = head->add<Label>("Books");
    t->setSize(28)->setWeight(400);
    t->grow = true;
    iconButton(head, "refresh", [st] {
        scan(st);
        if (st->draw) st->draw();
    }, 22)->tooltip = "Look for books again";
    iconButton(head, "folder_open", [] { m3::run({"xdg-open", paths::home() + "/Books"}, nullptr); }, 22)->tooltip = "Your Books folder";
    if (st->library.empty()) {
        auto* e = c->add<Flex>(Flex::Direction::Column);
        e->setSpacing(10)->setPadding({0, 80})->setAlignItems(Align::Center);
        e->add<Icon>("menu_book", 56)->setColor("primary", false);
        e->add<Label>("Your books go here")->setSize(20)->setWeight(600);
        caption(e, "EPUB books in Books, Documents or Downloads show up by themselves; or open one from Files");
        return;
    }
    auto* flow = c->add<Flow>();
    flow->spacing = 18;
    for (size_t i = 0; i < st->library.size(); ++i) {
        const Book& b = st->library[i];
        auto* card = flow->add<Button>(Flex::Direction::Column);
        card->setSpacing(6)->setPadding(Insets(6))->setRadius(Radii(14))->setAlignItems(Align::Start);
        card->hoverLift = true;
        cover(card, b, 132, 196);
        card->add<Label>(b.title)->setSize(13)->setWeight(600)->setMaxWidth(132)->setMaxLines(2);
        card->add<Label>(b.author)->setSize(11)->setMaxWidth(132)->setColor("on_surface_variant", false);
        if (auto p = st->place.find(b.path); p != st->place.end() && !b.chapters.empty()) {
            const float done = (static_cast<float>(p->second.first) + p->second.second) / static_cast<float>(b.chapters.size());
            auto* m = card->add<Meter>(std::clamp(done, 0.0f, 1.0f), ColorRef("primary"), 4);
            m->fixedWidth = 132;
        }
        card->onClicked = [st, i](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            const auto p = st->place.find(st->library[i].path);
            openAt(st, static_cast<int>(i), p != st->place.end() ? p->second.first : 0, p != st->place.end() ? p->second.second : 0);
        };
    }
}

void drawChapter(const std::shared_ptr<State>& st) {
    const Book& b = st->library[static_cast<size_t>(st->book)];
    const Look lk = look();
    auto* c = st->content;
    c->setBackground(lk.bg, false);
    const float width = static_cast<float>(Config::get().value<int>("/books/width", 640));
    const float size = static_cast<float>(Config::get().value<double>("/books/size", 18));
    const std::string family = Config::get().value<std::string>("/books/face", "serif") == "serif" ? "Noto Serif, DejaVu Serif, serif" : "";
    auto* col = c->add<Flex>(Flex::Direction::Column);
    col->setSpacing(size * 0.75f)->setAlignItems(Align::Start)->setPadding({0, 24});
    col->alignSelf = Align::Center;
    col->fixedWidth = width;
    const auto blocks = htmlBlocks(readAll(b.chapters[static_cast<size_t>(st->chapter)].file), fs::path(b.chapters[static_cast<size_t>(st->chapter)].file).parent_path().string());
    auto text = [&](Flex* into, const std::string& markup, float px, int weight, ColorRef ink, float w) {
        auto* l = into->add<Label>(markup);
        l->setMarkup(true)->setSize(px)->setWeight(weight)->setMaxWidth(w)->setMaxLines(kAllLines)->setColor(ink, false);
        if (!family.empty()) l->setFamily(family);
        return l;
    };
    for (const auto& bl : blocks) {
        switch (bl.kind) {
        case Block::Heading: {
            static const float scale[] = {1.7f, 1.45f, 1.25f, 1.1f, 1.0f, 1.0f};
            text(col, bl.text, size * scale[std::clamp(bl.level, 1, 6) - 1], 700, lk.ink, width);
            break;
        }
        case Block::Quote: {
            auto* row = col->add<Flex>(Flex::Direction::Row);
            row->setSpacing(14)->setAlignItems(Align::Stretch);
            auto* bar = row->add<Flex>(Flex::Direction::Column);
            bar->setBackground(lk.dim, false)->setRadius(Radii(1.5f));
            bar->fixedWidth = 3;
            text(row, bl.text, size, 400, lk.dim, width - 20);
            break;
        }
        case Block::Item: {
            auto* row = col->add<Flex>(Flex::Direction::Row);
            row->setSpacing(10)->setAlignItems(Align::Start);
            row->add<Label>("•")->setSize(size)->setColor(lk.dim, false);
            text(row, bl.text, size, 400, lk.ink, width - 24);
            break;
        }
        case Block::Pre: {
            auto* box = col->add<Flex>(Flex::Direction::Column);
            box->setPadding(Insets(12))->setRadius(Radii(10))->setBackground(ColorRef(Color{0.5f, 0.5f, 0.5f, 0.12f}), false);
            box->add<Label>(bl.text)->setFamily(Theme::get().monoFamily())->setSize(size * 0.8f)->setMaxWidth(width - 24)->setMaxLines(kAllLines)->setColor(lk.ink, false);
            break;
        }
        case Block::Rule: {
            auto* r = col->add<Flex>(Flex::Direction::Row);
            r->setBackground(lk.dim, false);
            r->fixedHeight = 1;
            r->fixedWidth = width / 3;
            r->alignSelf = Align::Center;
            break;
        }
        case Block::Image: {
            std::error_code ec;
            if (!fs::exists(bl.image, ec) || bl.image.ends_with(".svg")) break;
            const float w = std::min(width, 520.0f), h = w * 0.75f;
            auto* v = col->add<ImageView>(w, h, [file = bl.image, w, h](float scale, int& tw, int& th) -> GLuint {
                auto* t = Images::get().picture(file, static_cast<int>(w * scale), static_cast<int>(h * scale), false);
                if (!t) return 0;
                tw = t->width, th = t->height;
                return t->id;
            });
            v->alignSelf = Align::Center;
            break;
        }
        default: text(col, bl.text, size, 400, lk.ink, width);
        }
    }
    // The way on
    auto* nav = col->add<Flex>(Flex::Direction::Row);
    nav->setSpacing(10)->setPadding({0, 24});
    if (st->chapter > 0) pillButton(nav, "Previous chapter", false, [st] { openAt(st, st->book, st->chapter - 1, 0); }, "chevron_left");
    if (st->chapter + 1 < static_cast<int>(b.chapters.size())) pillButton(nav, "Next chapter", true, [st] { openAt(st, st->book, st->chapter + 1, 0); }, "chevron_right");
    else caption(nav, "The end");
}

void openAt(std::shared_ptr<State> st, int book, int chapter, float at) {
    if (book < 0 || book >= static_cast<int>(st->library.size())) return;
    if (st->book >= 0) rememberPlace(st);
    const Book& b = st->library[static_cast<size_t>(book)];
    st->book = book;
    st->chapter = std::clamp(chapter, 0, static_cast<int>(b.chapters.size()) - 1);
    st->showContents = false;
    if (st->draw) st->draw();
    // Where it was left, once the chapter's laid out
    EventLoop::get().addTimer(60, [st, at, alive = st->alive] {
        if (!*alive || !st->scroll) return;
        st->scroll->scrollTo(at * st->scroll->scrollRange(), false);
        rememberPlace(st);
    });
}

class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override {
        rememberPlace(m_st);
        *m_st->alive = false;
        if (m_st->saveTimer) EventLoop::get().cancel(m_st->saveTimer);
        save(m_st);
        m_st->draw = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void booksApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    card->setPadding(Insets(10))->setSpacing(8);
    card->add<Keeper>(st);
    // The reader's bar: back to the library, the book's title, the contents, the look
    auto* bar = card->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(6)->setAlignItems(Align::Center)->setPadding({6, 0});
    auto* body = card->add<Flex>(Flex::Direction::Row);
    body->setSpacing(10)->setAlignItems(Align::Stretch);
    body->grow = true;
    auto* contents = body->add<ScrollView>();
    contents->maxHeight = 1e6f;
    contents->fixedWidth = 280;
    auto* contentsList = contents->add<Flex>(Flex::Direction::Column);
    contentsList->setSpacing(2)->setAlignItems(Align::Stretch)->setPadding({4, 4, 8, 8});
    st->scroll = body->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->content = st->scroll->add<Flex>(Flex::Direction::Column);
    st->content->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({16, 16, 24, 24})->setRadius(Radii(18));

    st->draw = [st, bar, contents, contentsList] {
        bar->clearChildren();
        st->content->clearChildren();
        contentsList->clearChildren();
        const bool reading = st->book >= 0;
        bar->setVisible(reading);
        contents->setVisible(reading && st->showContents);
        if (!reading) return drawLibrary(st);
        const Book& b = st->library[static_cast<size_t>(st->book)];
        iconButton(bar, "arrow_back", [st] {
            rememberPlace(st);
            st->book = -1;
            save(st);
            if (st->draw) st->draw();
        }, 22)->tooltip = "The library (Esc)";
        iconButton(bar, "toc", [st] {
            st->showContents = !st->showContents;
            if (st->draw) st->draw();
        }, 22)->tooltip = "Contents";
        auto* titles = bar->add<Flex>(Flex::Direction::Column);
        titles->setAlignItems(Align::Start)->setSpacing(1);
        titles->grow = true;
        titles->givesWay = true;
        titles->add<Label>(b.title)->setSize(14)->setWeight(600)->setMaxWidth(520);
        const std::string ch = b.chapters[static_cast<size_t>(st->chapter)].title;
        titles->add<Label>(std::format("{}{}{} of {}", ch, ch.empty() ? "Chapter " : "  ·  ", st->chapter + 1, b.chapters.size()))->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(520);
        iconButton(bar, "text_decrease", [] { Config::get().set("/books/size", std::max(12.0, Config::get().value<double>("/books/size", 18) - 1)); }, 20)->tooltip = "Smaller text (Ctrl+−)";
        iconButton(bar, "text_increase", [] { Config::get().set("/books/size", std::min(32.0, Config::get().value<double>("/books/size", 18) + 1)); }, 20)->tooltip = "Bigger text (Ctrl+=)";
        Button* lookB = iconButton(bar, "contrast", [] {}, 20);
        lookB->tooltip = "Colours and face";
        lookB->onClicked = [lookB](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            const std::string theme = Config::get().value<std::string>("/books/theme", "paper"), face = Config::get().value<std::string>("/books/face", "serif");
            std::vector<MenuItem> m;
            for (const auto& [id, name] : std::initializer_list<std::pair<const char*, const char*>>{{"paper", "Paper"}, {"night", "Night"}, {"theme", "Your theme's"}})
                m.emplace_back("palette", name, "", [id = std::string(id)] { Config::get().set("/books/theme", id); }, theme == id);
            m.emplace_back("format_shapes", "Serif", "", [] { Config::get().set("/books/face", std::string("serif")); }, face == "serif", false, true, true);
            m.emplace_back("format_shapes", "Sans serif", "", [] { Config::get().set("/books/face", std::string("sans")); }, face != "serif");
            floatingMenu(lookB, m);
        };
        // The contents
        for (size_t i = 0; i < b.chapters.size(); ++i) {
            const bool here = static_cast<int>(i) == st->chapter;
            auto* r = contentsList->add<Button>(Flex::Direction::Row);
            r->setPadding({12, 8})->setRadius(Radii(12));
            if (here) r->setBackground("secondary_container", false);
            r->add<Label>(b.chapters[i].title.empty() ? std::format("Chapter {}", i + 1) : b.chapters[i].title)->setSize(13)->setWeight(here ? 600 : 400)->setMaxWidth(240)->setMaxLines(2);
            r->onClicked = [st, i](uint32_t btn) {
                if (btn == BTN_LEFT) openAt(st, st->book, static_cast<int>(i), 0);
            };
        }
        drawChapter(st);
    };
    // Looks change at once
    auto* watcher = card->add<Live>();
    watcher->floating = true;
    watcher->sync = [st] {
        if (*st->alive && st->book >= 0 && st->draw) {
            const float range = st->scroll->scrollRange(), at = range > 0 ? st->scroll->scrollOffset() / range : 0;
            st->draw();
            EventLoop::get().addTimer(60, [st, at, alive = st->alive] {
                if (*alive && st->scroll) st->scroll->scrollTo(at * st->scroll->scrollRange(), false);
            });
        }
    };
    watcher->watch(Config::get().changed);

    popup.onKeyBeforeNavigation = [st](const KeyEvent& e) {
        if (st->book < 0) return false;
        const auto& b = st->library[static_cast<size_t>(st->book)];
        auto page = [&](float dir) {
            const float range = st->scroll->scrollRange(), at = st->scroll->scrollOffset();
            // At a chapter's end, on to the next (and back at its start)
            // (a held key stops there: only a fresh press goes on)
            if (dir > 0 && at >= range - 1) {
                if (!e.repeat && st->chapter + 1 < static_cast<int>(b.chapters.size())) openAt(st, st->book, st->chapter + 1, 0);
                return;
            }
            if (dir < 0 && at <= 1) {
                if (!e.repeat && st->chapter > 0) openAt(st, st->book, st->chapter - 1, 1);
                return;
            }
            st->scroll->scrollTo(at + dir * (st->scroll->rect().h - 80), true);
            rememberPlace(st);
        };
        if (e.ctrl()) {
            if (e.sym == XKB_KEY_equal || e.sym == XKB_KEY_plus) return Config::get().set("/books/size", std::min(32.0, Config::get().value<double>("/books/size", 18) + 1)), true;
            if (e.sym == XKB_KEY_minus) return Config::get().set("/books/size", std::max(12.0, Config::get().value<double>("/books/size", 18) - 1)), true;
            return false;
        }
        switch (e.sym) {
        case XKB_KEY_Page_Down:
        case XKB_KEY_space:
        case XKB_KEY_Right: return page(e.shift() && e.sym == XKB_KEY_space ? -1.0f : 1.0f), true;
        case XKB_KEY_Page_Up:
        case XKB_KEY_Left: return page(-1), true;
        case XKB_KEY_Escape:
            rememberPlace(st);
            st->book = -1;
            save(st);
            if (st->draw) st->draw();
            return true;
        default: return false;
        }
    };
    booksGo = [st](const std::string& file) {
        if (!*st->alive || file.empty()) return;
        auto it = std::ranges::find(st->library, file, &Book::path);
        if (it == st->library.end()) {
            Book b;
            b.path = file;
            if (!openBook(b)) {
                m3::run({"notify-send", "-a", "Books", "-i", "x-office-document", "Couldn't open this book", fs::path(file).filename().string()}, nullptr);
                return;
            }
            st->library.insert(st->library.begin(), std::move(b));
            it = st->library.begin();
            save(st);
        }
        const int i = static_cast<int>(it - st->library.begin());
        const auto p = st->place.find(file);
        openAt(st, i, p != st->place.end() ? p->second.first : 0, p != st->place.end() ? p->second.second : 0);
    };
    scan(st);
    st->draw();
    if (!arg.empty()) booksGo(fs::absolute(arg).string());
}

void booksClosed() {
    booksGo = nullptr;
}

void booksSettings(Flex* page) {
    auto* g = group(page, "Reading");
    sliderRow(g, "format_size", "Text size", "/books/size", 18, 12, 32, 1, [](double v) { return std::format("{:.0f} px", v); });
    choiceRow<std::string>(g, "format_shapes", "Face", "", "/books/face", "serif", {{"serif", "Serif"}, {"sans", "Sans serif"}});
    choiceRow<std::string>(g, "contrast", "Colours", "", "/books/theme", "paper", {{"paper", "Paper"}, {"night", "Night"}, {"theme", "Your theme's"}});
    choiceRow<int>(g, "width", "Column", "How wide the lines run", "/books/width", 640, {{520, "Narrow"}, {640, "Medium"}, {800, "Wide"}});
    caption(page, "  Books are looked for in Books, Documents and Downloads; others open from Files. Space, → and Page Down turn the page, on into the next chapter.");
}

} // namespace m3::shell::panels
