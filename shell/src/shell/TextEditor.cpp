// The text editor (material3ui-text): plain text and code, in tabs. Only
// the lines in view are drawn (through the shell's text engine), so big
// files stay quick. Line numbers, the current line, selection by keyboard
// and pointer, auto-indent, find and replace, go to a line, undo, line
// endings and encodings kept as they were, text size.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <glib.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <sstream>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> textGo;

namespace {

// Settings → Text Editor
int tabWidth() {
    return std::clamp(Config::get().value<int>("/text/tabWidth", 4), 1, 16);
}
bool lineNumbers() {
    return Config::get().value<bool>("/text/lineNumbers", true);
}

struct Pos {
    int line = 0;
    size_t col = 0; // bytes into the line
    auto operator<=>(const Pos&) const = default;
};

struct Doc {
    std::vector<std::string> lines{""};
    std::string path;       // "" until saved
    bool crlf = false, finalNewline = true, latin1 = false, modified = false;
    std::vector<std::pair<std::vector<std::string>, Pos>> undo, redo;
    EventLoop::Clock::time_point lastEdit{};

    std::string name() const { return path.empty() ? "Untitled" : fs::path(path).filename().string(); }
    std::string text() const {
        std::string s;
        const char* nl = crlf ? "\r\n" : "\n";
        for (size_t i = 0; i < lines.size(); ++i) {
            s += lines[i];
            if (i + 1 < lines.size() || finalNewline) s += nl;
        }
        return s;
    }
    size_t bytes() const {
        size_t n = 0;
        for (const auto& l : lines) n += l.size() + 1;
        return n;
    }
};

bool loadDoc(Doc& d, const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Couldn't open it";
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    if (s.size() > 64ull * 1024 * 1024) {
        error = "It's over 64 MB";
        return false;
    }
    if (s.find('\0') != std::string::npos) {
        error = "It isn't text (it has binary data)";
        return false;
    }
    d.latin1 = false;
    if (!g_utf8_validate(s.c_str(), static_cast<gssize>(s.size()), nullptr)) {
        // Not UTF-8: read as Latin-1 (every byte a character), written back so
        gsize n = 0;
        gchar* u = g_convert(s.c_str(), static_cast<gssize>(s.size()), "UTF-8", "ISO-8859-1", nullptr, &n, nullptr);
        if (u) s.assign(u, n), g_free(u), d.latin1 = true;
    }
    if (s.starts_with("\xEF\xBB\xBF")) s.erase(0, 3);
    d.crlf = s.find("\r\n") != std::string::npos;
    d.finalNewline = s.empty() || s.back() == '\n';
    d.lines.clear();
    std::string line;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            d.lines.push_back(std::move(line));
            line.clear();
        } else {
            line += s[i];
        }
    }
    if (!line.empty() || d.lines.empty() || !d.finalNewline) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        d.lines.push_back(line);
    }
    if (d.finalNewline && d.lines.size() > 1 && s.ends_with('\n')) {
        // The split leaves no empty last line for a final newline; one line
        // still shows for the caret after it
    }
    d.path = path;
    d.modified = false;
    d.undo.clear(), d.redo.clear();
    return true;
}

bool saveDoc(Doc& d, const std::string& path, std::string& error) {
    std::string out = d.text();
    if (d.latin1) {
        gsize n = 0;
        gchar* c = g_convert(out.c_str(), static_cast<gssize>(out.size()), "ISO-8859-1", "UTF-8", nullptr, &n, nullptr);
        if (c) out.assign(c, n), g_free(c);
        else d.latin1 = false; // something only UTF-8 holds was typed: UTF-8 it is
    }
    const std::string tmp = path + ".saving~";
    {
        std::ofstream f(tmp, std::ios::binary);
        if (!f || !(f << out)) {
            error = "Couldn't write it";
            return false;
        }
    }
    std::error_code ec;
    // Its permissions kept (a script stays runnable)
    if (fs::exists(path, ec)) fs::permissions(tmp, fs::status(path, ec).permissions(), ec);
    fs::rename(tmp, path, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    d.path = path;
    d.modified = false;
    return true;
}

// Tabs shown as spaces to the next stop
std::string expand(const std::string& s, size_t upto = std::string::npos) {
    std::string o;
    size_t col = 0;
    const size_t end = std::min(upto, s.size());
    for (size_t i = 0; i < end; ++i) {
        if (s[i] == '\t') {
            const size_t n = tabWidth() - col % tabWidth();
            o.append(n, ' ');
            col += n;
        } else {
            o += s[i];
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++col;
        }
    }
    return o;
}

bool wordChar(gunichar c) {
    return g_unichar_isalnum(c) || c == '_';
}

class Editor : public Node {
public:
    std::shared_ptr<Doc> doc;
    Pos cursor, anchor;
    float fontSize = 14;
    std::function<void()> onChanged, onMoved; // text, caret
    std::function<bool(const KeyEvent&)> onOtherKey;
    std::vector<std::pair<Pos, Pos>> highlights; // find's matches
    int currentMatch = -1;

    explicit Editor(std::shared_ptr<Doc> d) : doc(std::move(d)) {
        fontSize = static_cast<float>(Config::get().value<double>("/text/fontSize", 14.0));
        // Settings apply to the open files at once
        m_configConn = Config::get().changed.connect([this] {
            fontSize = static_cast<float>(Config::get().value<double>("/text/fontSize", 14.0));
            requestRedraw();
        });
    }
    ~Editor() override {
        *m_alive = false;
        if (m_blink) EventLoop::get().cancel(m_blink);
    }

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    bool keyboardNavigable() const override { return true; }

    TextStyle style() const {
        TextStyle s;
        s.family = Theme::get().monoFamily();
        s.size = fontSize;
        return s;
    }
    float lineH() const { return std::round(fontSize * 1.6f); }
    float gutter() const {
        if (!lineNumbers()) return 12;
        const int digits = std::max(3, static_cast<int>(std::to_string(doc->lines.size()).size()));
        return 20 + static_cast<float>(digits) * fontSize * 0.62f;
    }
    float xOf(int line, size_t col) const {
        const auto& l = doc->lines[static_cast<size_t>(line)];
        if (col == 0) return 0;
        return TextEngine::get().measure(expand(l, col), style()).width;
    }
    size_t colAt(int line, float x) const {
        const auto& l = doc->lines[static_cast<size_t>(line)];
        // Binary search over character boundaries
        std::vector<size_t> bounds{0};
        for (const char* p = l.c_str(); *p;) {
            p = g_utf8_next_char(p);
            bounds.push_back(static_cast<size_t>(p - l.c_str()));
        }
        size_t lo = 0, hi = bounds.size() - 1;
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            const float a = xOf(line, bounds[mid]), b = xOf(line, bounds[mid + 1]);
            if (x < (a + b) / 2) hi = mid;
            else lo = mid + 1;
        }
        return bounds[lo];
    }

    void setFontSize(float s) {
        fontSize = std::clamp(s, 8.0f, 36.0f);
        Config::get().set("/text/fontSize", fontSize);
        reveal();
        requestRedraw();
    }

    bool hasSelection() const { return cursor != anchor; }
    std::pair<Pos, Pos> ordered() const { return cursor < anchor ? std::pair{cursor, anchor} : std::pair{anchor, cursor}; }
    std::string selected() const {
        if (!hasSelection()) return "";
        auto [a, b] = ordered();
        if (a.line == b.line) return doc->lines[static_cast<size_t>(a.line)].substr(a.col, b.col - a.col);
        std::string s = doc->lines[static_cast<size_t>(a.line)].substr(a.col);
        for (int i = a.line + 1; i < b.line; ++i) s += "\n" + doc->lines[static_cast<size_t>(i)];
        s += "\n" + doc->lines[static_cast<size_t>(b.line)].substr(0, b.col);
        return s;
    }

    void snapshot(bool coalesce) {
        const auto now = EventLoop::Clock::now();
        if (!coalesce || doc->undo.empty() || now - doc->lastEdit > std::chrono::milliseconds(800)) {
            doc->undo.push_back({doc->lines, cursor});
            // Big files keep fewer steps
            const size_t cap = doc->bytes() > 4'000'000 ? 10 : doc->bytes() > 400'000 ? 40 : 200;
            while (doc->undo.size() > cap) doc->undo.erase(doc->undo.begin());
        }
        doc->redo.clear();
        doc->lastEdit = now;
    }

    void eraseSelection() {
        if (!hasSelection()) return;
        auto [a, b] = ordered();
        auto& la = doc->lines[static_cast<size_t>(a.line)];
        const std::string tail = doc->lines[static_cast<size_t>(b.line)].substr(b.col);
        la = la.substr(0, a.col) + tail;
        doc->lines.erase(doc->lines.begin() + a.line + 1, doc->lines.begin() + b.line + 1);
        cursor = anchor = a;
    }

    void insert(const std::string& text, bool coalesce = false) {
        snapshot(coalesce && text.size() == 1 && text != " ");
        eraseSelection();
        auto& line = doc->lines[static_cast<size_t>(cursor.line)];
        const std::string tail = line.substr(cursor.col);
        line.erase(cursor.col);
        size_t start = 0;
        std::string t = text;
        std::erase(t, '\r');
        for (size_t nl; (nl = t.find('\n', start)) != std::string::npos; start = nl + 1) {
            doc->lines[static_cast<size_t>(cursor.line)] += t.substr(start, nl - start);
            doc->lines.insert(doc->lines.begin() + cursor.line + 1, "");
            ++cursor.line;
        }
        doc->lines[static_cast<size_t>(cursor.line)] += t.substr(start);
        cursor.col = doc->lines[static_cast<size_t>(cursor.line)].size();
        doc->lines[static_cast<size_t>(cursor.line)] += tail;
        anchor = cursor;
        edited();
    }

    void edited() {
        doc->modified = true;
        if (onChanged) onChanged();
        moved();
    }
    void moved() {
        restartBlink();
        reveal();
        requestRedraw();
        if (onMoved) onMoved();
    }
    void moveTo(Pos p, bool extend) {
        p.line = std::clamp(p.line, 0, static_cast<int>(doc->lines.size()) - 1);
        p.col = std::min(p.col, doc->lines[static_cast<size_t>(p.line)].size());
        cursor = p;
        if (!extend) anchor = p;
        m_goalX = -1;
        moved();
    }
    void selectAll() {
        anchor = {0, 0};
        cursor = {static_cast<int>(doc->lines.size()) - 1, doc->lines.back().size()};
        moved();
    }
    void gotoLine(int line) {
        moveTo({line, 0}, false);
        // Centred
        m_scrollY = std::max(0.0f, static_cast<float>(cursor.line) * lineH() - rect().h / 2);
        requestRedraw();
    }
    void doUndo(bool redo) {
        auto& from = redo ? doc->redo : doc->undo;
        auto& to = redo ? doc->undo : doc->redo;
        if (from.empty()) return;
        to.push_back({doc->lines, cursor});
        auto [lines, pos] = from.back();
        from.pop_back();
        doc->lines = std::move(lines);
        cursor = anchor = pos;
        moveTo(pos, false);
        edited();
    }

    void reveal() {
        const float lh = lineH(), y = static_cast<float>(cursor.line) * lh, h = rect().h > 0 ? rect().h : 400;
        if (y < m_scrollY) m_scrollY = y;
        else if (y + lh > m_scrollY + h - 8) m_scrollY = y + lh - h + 8;
        const float x = xOf(cursor.line, cursor.col), w = rect().w - gutter() - 24;
        if (x < m_scrollX) m_scrollX = std::max(0.0f, x - 40);
        else if (w > 0 && x > m_scrollX + w) m_scrollX = x - w + 40;
    }

    bool onKey(const KeyEvent& e) override {
        if (onOtherKey && e.ctrl() && onOtherKey(e)) return true;
        const bool ctrl = e.ctrl(), shift = e.shift(), alt = e.mods & ModAlt;
        auto& lines = doc->lines;
        const std::string& cur = lines[static_cast<size_t>(cursor.line)];
        auto prev = [&](const std::string& l, size_t c) { return c == 0 ? 0 : static_cast<size_t>(g_utf8_prev_char(l.c_str() + c) - l.c_str()); };
        auto next = [&](const std::string& l, size_t c) { return c >= l.size() ? l.size() : static_cast<size_t>(g_utf8_next_char(l.c_str() + c) - l.c_str()); };
        auto wordEdge = [&](bool fwd) {
            Pos p = cursor;
            const std::string& l = lines[static_cast<size_t>(p.line)];
            if (fwd) {
                if (p.col >= l.size()) return p.line + 1 < static_cast<int>(lines.size()) ? Pos{p.line + 1, 0} : p;
                const char* s = l.c_str();
                const char* q = s + p.col;
                while (*q && !wordChar(g_utf8_get_char(q))) q = g_utf8_next_char(q);
                while (*q && wordChar(g_utf8_get_char(q))) q = g_utf8_next_char(q);
                p.col = static_cast<size_t>(q - s);
            } else {
                if (p.col == 0) return p.line > 0 ? Pos{p.line - 1, lines[static_cast<size_t>(p.line - 1)].size()} : p;
                const char* s = l.c_str();
                const char* q = s + p.col;
                while (q > s && !wordChar(g_utf8_get_char(g_utf8_prev_char(q)))) q = g_utf8_prev_char(q);
                while (q > s && wordChar(g_utf8_get_char(g_utf8_prev_char(q)))) q = g_utf8_prev_char(q);
                p.col = static_cast<size_t>(q - s);
            }
            return p;
        };
        auto vertical = [&](int by) {
            if (m_goalX < 0) m_goalX = xOf(cursor.line, cursor.col);
            const float goal = m_goalX;
            const int l = std::clamp(cursor.line + by, 0, static_cast<int>(lines.size()) - 1);
            cursor = {l, colAt(l, goal)};
            if (!shift) anchor = cursor;
            moved();
            m_goalX = goal;
        };
        switch (e.sym) {
        case XKB_KEY_Left:
            if (hasSelection() && !shift) return moveTo(ordered().first, false), true;
            if (ctrl) return moveTo(wordEdge(false), shift), true;
            if (cursor.col == 0 && cursor.line > 0) return moveTo({cursor.line - 1, lines[static_cast<size_t>(cursor.line - 1)].size()}, shift), true;
            return moveTo({cursor.line, prev(cur, cursor.col)}, shift), true;
        case XKB_KEY_Right:
            if (hasSelection() && !shift) return moveTo(ordered().second, false), true;
            if (ctrl) return moveTo(wordEdge(true), shift), true;
            if (cursor.col >= cur.size() && cursor.line + 1 < static_cast<int>(lines.size())) return moveTo({cursor.line + 1, 0}, shift), true;
            return moveTo({cursor.line, next(cur, cursor.col)}, shift), true;
        case XKB_KEY_Up:
            if (alt) return moveLine(-1), true;
            if (ctrl) return m_scrollY = std::max(0.0f, m_scrollY - lineH()), requestRedraw(), true;
            return vertical(-1), true;
        case XKB_KEY_Down:
            if (alt) return moveLine(1), true;
            if (ctrl) return m_scrollY += lineH(), requestRedraw(), true;
            return vertical(1), true;
        case XKB_KEY_Page_Up: return vertical(-std::max(1, static_cast<int>(rect().h / lineH()) - 2)), true;
        case XKB_KEY_Page_Down: return vertical(std::max(1, static_cast<int>(rect().h / lineH()) - 2)), true;
        case XKB_KEY_Home: {
            if (ctrl) return moveTo({0, 0}, shift), true;
            // To the first character, then the line's start
            const size_t indent = cur.find_first_not_of(" \t") == std::string::npos ? cur.size() : cur.find_first_not_of(" \t");
            return moveTo({cursor.line, cursor.col == indent ? 0 : indent}, shift), true;
        }
        case XKB_KEY_End:
            if (ctrl) return moveTo({static_cast<int>(lines.size()) - 1, lines.back().size()}, shift), true;
            return moveTo({cursor.line, cur.size()}, shift), true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: {
            // The line's indent carried on
            const std::string indent = cur.substr(0, std::min(cursor.col, cur.find_first_not_of(" \t") == std::string::npos ? cur.size() : cur.find_first_not_of(" \t")));
            insert("\n" + indent);
            return true;
        }
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab:
            if (ctrl) return false;
            if (shift || e.sym == XKB_KEY_ISO_Left_Tab || (hasSelection() && ordered().first.line != ordered().second.line)) return indentLines(!(shift || e.sym == XKB_KEY_ISO_Left_Tab)), true;
            insert(usesTabs() ? "\t" : std::string(tabWidth() - expand(cur, cursor.col).size() % tabWidth(), ' '));
            return true;
        case XKB_KEY_BackSpace:
            if (!hasSelection()) {
                if (ctrl) anchor = wordEdge(false);
                else if (cursor.col > 0) anchor = {cursor.line, prev(cur, cursor.col)};
                else if (cursor.line > 0) anchor = {cursor.line - 1, lines[static_cast<size_t>(cursor.line - 1)].size()};
            }
            if (hasSelection()) {
                snapshot(false);
                eraseSelection();
                edited();
            }
            return true;
        case XKB_KEY_Delete:
            if (!hasSelection()) {
                if (shift) return deleteLine(), true;
                if (ctrl) anchor = wordEdge(true);
                else if (cursor.col < cur.size()) anchor = {cursor.line, next(cur, cursor.col)};
                else if (cursor.line + 1 < static_cast<int>(lines.size())) anchor = {cursor.line + 1, 0};
            }
            if (hasSelection()) {
                snapshot(false);
                eraseSelection();
                edited();
            }
            return true;
        case XKB_KEY_Escape:
            if (hasSelection()) return moveTo(cursor, false), true;
            return onOtherKey && onOtherKey(e);
        default: break;
        }
        if (ctrl) {
            const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
            switch (k) {
            case 'a': selectAll(); return true;
            case 'c':
            case 'x': {
                // Nothing selected: the whole line
                const std::string t = hasSelection() ? selected() : cur + "\n";
                m3::run({"wl-copy"}, nullptr, t);
                if (k == 'x') {
                    if (hasSelection()) {
                        snapshot(false);
                        eraseSelection();
                        edited();
                    } else {
                        deleteLine();
                    }
                }
                return true;
            }
            case 'v':
                m3::run({"wl-paste", "--no-newline", "--type", "text"}, [this, alive = m_alive](int code, const std::string& out) {
                    if (*alive && code == 0 && !out.empty()) insert(out);
                });
                return true;
            case 'z': doUndo(shift); return true;
            case 'y': doUndo(true); return true;
            case 'd': {
                snapshot(false);
                lines.insert(lines.begin() + cursor.line + 1, cur);
                ++cursor.line, ++anchor.line;
                edited();
                return true;
            }
            case 'l':
                anchor = {cursor.line, 0};
                cursor = cursor.line + 1 < static_cast<int>(lines.size()) ? Pos{cursor.line + 1, 0} : Pos{cursor.line, cur.size()};
                moved();
                return true;
            case 'k':
                if (shift) return deleteLine(), true;
                break;
            case '/': toggleComment(); return true;
            default: break;
            }
            return onOtherKey && onOtherKey(e);
        }
        if (!e.text.empty() && static_cast<unsigned char>(e.text[0]) >= 0x20 && e.text[0] != 0x7f) {
            insert(e.text, true);
            return true;
        }
        return onOtherKey && onOtherKey(e);
    }

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, std::max(100.0f, available.h)}; }

    void onFocus(bool f) override {
        m_focused = f;
        if (f) restartBlink();
        else if (m_blink) EventLoop::get().cancel(m_blink), m_blink = 0;
        requestRedraw();
    }
    void onPress(uint32_t button, float x, float y) override {
        focus();
        if (button != BTN_LEFT) return;
        const Pos p = posAt(x, y);
        const auto now = EventLoop::Clock::now();
        const bool dbl = now - m_lastPress < std::chrono::milliseconds(400) && p.line == m_lastPos.line;
        m_lastPress = now;
        m_lastPos = p;
        if (dbl) {
            // The word under it
            const std::string& l = doc->lines[static_cast<size_t>(p.line)];
            const char* s = l.c_str();
            const char* a = s + p.col;
            const char* b = a;
            while (a > s && wordChar(g_utf8_get_char(g_utf8_prev_char(a)))) a = g_utf8_prev_char(a);
            while (*b && wordChar(g_utf8_get_char(b))) b = g_utf8_next_char(b);
            anchor = {p.line, static_cast<size_t>(a - s)};
            cursor = {p.line, static_cast<size_t>(b - s)};
            moved();
            return;
        }
        moveTo(p, wl::Display::get().heldModifiers() & ModShift);
        m_dragging = true;
    }
    void onDrag(float x, float y) override {
        if (!m_dragging) return;
        cursor = posAt(x, y);
        moved();
    }
    void onRelease() override {
        m_dragging = false;
        if (hasSelection()) m3::run({"wl-copy", "--primary"}, nullptr, selected());
    }
    bool onScroll(int steps) override {
        if (wl::Display::get().heldModifiers() & ModCtrl) return setFontSize(fontSize - static_cast<float>(steps)), true;
        if (wl::Display::get().heldModifiers() & ModShift) m_scrollX = std::max(0.0f, m_scrollX + static_cast<float>(steps) * 60);
        else m_scrollY += static_cast<float>(steps) * lineH() * 3;
        clampScroll();
        requestRedraw();
        return true;
    }
    bool onScrollPixels(float dy, bool) override {
        m_scrollY += dy;
        clampScroll();
        requestRedraw();
        return true;
    }

    void doPaint(Renderer& r) override {
        const auto& b = rect();
        clampScroll();
        auto& th = Theme::get();
        r.rect(b, th.color("surface_container_lowest"), Radii(16));
        r.pushClip(b);
        const float lh = lineH(), g = gutter(), textX = b.x + g + 8;
        const int first = std::max(0, static_cast<int>(m_scrollY / lh));
        const int last = std::min(static_cast<int>(doc->lines.size()) - 1, static_cast<int>((m_scrollY + b.h) / lh) + 1);
        const TextStyle st = style();
        TextStyle numStyle = st;
        numStyle.size = fontSize * 0.85f;
        const Color ink = th.color("on_surface"), dim = th.color("outline"), sel = th.color("primary").withAlpha(m_focused ? 0.32f : 0.18f);
        auto [sa, sb] = ordered();
        for (int i = first; i <= last; ++i) {
            const float y = b.y + static_cast<float>(i) * lh - m_scrollY;
            if (i == cursor.line && !hasSelection() && Config::get().value<bool>("/text/currentLine", true)) r.rect({b.x + g, y, b.w - g, lh}, th.color("on_surface").withAlpha(0.05f));
            const std::string& l = doc->lines[static_cast<size_t>(i)];
            r.pushClip({b.x + g, b.y, b.w - g, b.h});
            // Find's matches, then the selection
            for (size_t m = 0; m < highlights.size(); ++m) {
                const auto& [ha, hb] = highlights[m];
                if (ha.line != i) continue;
                const float x0 = xOf(i, ha.col), x1 = xOf(i, hb.col);
                r.rect({textX + x0 - m_scrollX, y + 2, std::max(2.0f, x1 - x0), lh - 4}, th.color(static_cast<int>(m) == currentMatch ? "tertiary" : "secondary").withAlpha(0.35f), Radii(3));
            }
            if (hasSelection() && i >= sa.line && i <= sb.line) {
                const float x0 = i == sa.line ? xOf(i, sa.col) : 0;
                const float x1 = i == sb.line ? xOf(i, sb.col) : xOf(i, l.size()) + fontSize * 0.5f;
                r.rect({textX + x0 - m_scrollX, y, std::max(3.0f, x1 - x0), lh}, sel);
            }
            if (!l.empty()) {
                // Very long lines drawn in their first part (the texture's limit)
                const std::string shown = expand(l.size() > 4000 ? l.substr(0, 4000) : l);
                const auto& tex = TextEngine::get().texture(shown, st, r.scale());
                const float ty = y + (lh - tex.logical.height) / 2;
                if (tex.id) r.mask(tex.id, {textX - m_scrollX, ty, tex.logical.width, tex.logical.height}, ink);
                if (tex.color) r.image(tex.color, {textX - m_scrollX, ty, tex.logical.width, tex.logical.height});
            }
            if (m_focused && m_caretOn && i == cursor.line)
                r.rect({textX + xOf(i, cursor.col) - m_scrollX - 1, y + 3, 2, lh - 6}, th.color("primary"), Radii(1));
            r.popClip();
            // The line's number
            if (!lineNumbers()) continue;
            const std::string n = std::to_string(i + 1);
            const auto& nt = TextEngine::get().texture(n, numStyle, r.scale());
            if (nt.id) r.mask(nt.id, {b.x + g - 12 - nt.logical.width, y + (lh - nt.logical.height) / 2, nt.logical.width, nt.logical.height}, i == cursor.line ? ink : dim);
        }
        if (lineNumbers()) r.rect({b.x + g, b.y + 8, 1, b.h - 16}, th.color("outline_variant").withAlpha(0.5f));
        // Where in the file: a thin bar
        const float total = static_cast<float>(doc->lines.size()) * lh;
        if (total > b.h) {
            const float vh = std::max(24.0f, b.h * b.h / total), vy = b.y + (b.h - vh) * (m_scrollY / (total - b.h));
            r.rect({b.right() - 6, vy, 3, vh}, th.color("on_surface_variant").withAlpha(0.4f), Radii(1.5f));
        }
        r.popClip();
    }

private:
    Pos posAt(float x, float y) {
        const int line = std::clamp(static_cast<int>((y - rect().y + m_scrollY) / lineH()), 0, static_cast<int>(doc->lines.size()) - 1);
        return {line, colAt(line, x - rect().x - gutter() - 8 + m_scrollX)};
    }
    void clampScroll() {
        const float total = static_cast<float>(doc->lines.size()) * lineH();
        m_scrollY = std::clamp(m_scrollY, 0.0f, std::max(0.0f, total - rect().h + lineH() * 3));
    }
    void restartBlink() {
        m_caretOn = true;
        if (m_blink) EventLoop::get().cancel(m_blink);
        m_blink = EventLoop::get().addTimer(530, [this, alive = m_alive] {
            if (!*alive) return;
            m_caretOn = !m_caretOn;
            requestRedraw();
        }, true);
    }
    bool usesTabs() const {
        const std::string mode = Config::get().value<std::string>("/text/indent", "auto");
        if (mode == "tabs") return true;
        if (mode == "spaces") return false;
        int tabs = 0, spaces = 0;
        for (size_t i = 0; i < doc->lines.size() && i < 400; ++i) {
            const auto& l = doc->lines[i];
            if (l.starts_with('\t')) ++tabs;
            else if (l.starts_with("  ")) ++spaces;
        }
        return tabs > spaces;
    }
    void indentLines(bool in) {
        snapshot(false);
        auto [a, b] = ordered();
        if (b.col == 0 && b.line > a.line) --b.line;
        const std::string unit = usesTabs() ? "\t" : std::string(tabWidth(), ' ');
        for (int i = a.line; i <= b.line; ++i) {
            auto& l = doc->lines[static_cast<size_t>(i)];
            if (in) l.insert(0, unit);
            else if (l.starts_with('\t')) l.erase(0, 1);
            else l.erase(0, std::min<size_t>(tabWidth(), l.find_first_not_of(' ') == std::string::npos ? l.size() : l.find_first_not_of(' ')));
        }
        anchor = {a.line, 0};
        cursor = {b.line, doc->lines[static_cast<size_t>(b.line)].size()};
        edited();
    }
    void deleteLine() {
        snapshot(false);
        auto& lines = doc->lines;
        if (lines.size() == 1) lines[0].clear();
        else lines.erase(lines.begin() + cursor.line);
        cursor.line = std::min(cursor.line, static_cast<int>(lines.size()) - 1);
        cursor.col = std::min(cursor.col, lines[static_cast<size_t>(cursor.line)].size());
        anchor = cursor;
        edited();
    }
    void moveLine(int by) {
        const int to = cursor.line + by;
        if (to < 0 || to >= static_cast<int>(doc->lines.size())) return;
        snapshot(false);
        std::swap(doc->lines[static_cast<size_t>(cursor.line)], doc->lines[static_cast<size_t>(to)]);
        cursor.line = anchor.line = to;
        edited();
    }
    // Ctrl+/: "# " or "// " by the file's kind, on the lines selected
    void toggleComment() {
        const std::string ext = fs::path(doc->path).extension().string();
        const std::string mark = (ext == ".py" || ext == ".sh" || ext == ".conf" || ext == ".toml" || ext == ".yaml" || ext == ".yml" || ext == ".rb" || ext == ".pl" || ext == ".fish" || ext == ".ini" ||
                                  ext == ".cfg" || ext.empty() || ext == ".txt")
                                     ? "# "
                                 : ext == ".lua" || ext == ".sql" ? "-- "
                                                                   : "// ";
        snapshot(false);
        auto [a, b] = ordered();
        bool all = true;
        for (int i = a.line; i <= b.line; ++i) {
            const auto& l = doc->lines[static_cast<size_t>(i)];
            const size_t at = l.find_first_not_of(" \t");
            if (at != std::string::npos && l.compare(at, mark.size() - 1, mark, 0, mark.size() - 1) != 0) all = false;
        }
        for (int i = a.line; i <= b.line; ++i) {
            auto& l = doc->lines[static_cast<size_t>(i)];
            const size_t at = l.find_first_not_of(" \t");
            if (at == std::string::npos) continue;
            if (all) l.erase(at, l.compare(at, mark.size(), mark) == 0 ? mark.size() : mark.size() - 1);
            else l.insert(at, mark);
        }
        cursor.col = std::min(cursor.col, doc->lines[static_cast<size_t>(cursor.line)].size());
        anchor.col = std::min(anchor.col, doc->lines[static_cast<size_t>(anchor.line)].size());
        edited();
    }

    float m_scrollX = 0, m_scrollY = 0, m_goalX = -1;
    bool m_focused = false, m_caretOn = true, m_dragging = false;
    EventLoop::TimerId m_blink = 0;
    EventLoop::Clock::time_point m_lastPress{};
    Pos m_lastPos;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    Connection m_configConn;
};

// ── The window: tabs, the bars, the status line ──

struct Tab {
    std::shared_ptr<Doc> doc;
    Editor* editor = nullptr;
};

struct State {
    std::vector<Tab> tabs;
    int active = 0;
    Flex *tabRow = nullptr, *stack = nullptr, *findBar = nullptr, *promptBar = nullptr;
    Label* status = nullptr;
    TextField *findField = nullptr, *replaceField = nullptr;
    bool matchCase = false;
    Popup* popup = nullptr;
    std::function<void()> rebuildTabs, refreshStatus;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

Editor* activeEditor(const std::shared_ptr<State>& st) {
    return st->tabs.empty() ? nullptr : st->tabs[static_cast<size_t>(st->active)].editor;
}

std::string recoveryDir() {
    return paths::stateDir() + "/text-recovery";
}

class TextKeeper : public Node {
public:
    explicit TextKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~TextKeeper() override {
        *m_st->alive = false;
        // Unsaved work isn't lost: copies kept, and said where
        std::error_code ec;
        for (auto& t : m_st->tabs) {
            if (!t.doc->modified) continue;
            fs::create_directories(recoveryDir(), ec);
            const std::string name = fileops::freeName(recoveryDir(), t.doc->name() + ".unsaved.txt");
            paths::writeFile(recoveryDir() + "/" + name, t.doc->text());
            m3::run({"notify-send", "-a", "Text Editor", "-i", "document-save", "Unsaved changes kept", fileops::displayPath(recoveryDir() + "/" + name)}, nullptr);
        }
        m_st->rebuildTabs = m_st->refreshStatus = nullptr;
        m_st->tabs.clear();
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

void activate(const std::shared_ptr<State>& st, int i) {
    if (st->tabs.empty()) return;
    st->active = std::clamp(i, 0, static_cast<int>(st->tabs.size()) - 1);
    for (size_t k = 0; k < st->tabs.size(); ++k) st->tabs[k].editor->setVisible(static_cast<int>(k) == st->active);
    Editor* e = activeEditor(st);
    EventLoop::get().defer([st, e] {
        if (*st->alive && activeEditor(st) == e) e->focus();
    });
    if (st->rebuildTabs) st->rebuildTabs();
    if (st->refreshStatus) st->refreshStatus();
}

void prompt(const std::shared_ptr<State>& st, const std::string& icon, const std::string& placeholder, const std::string& value, const std::string& action,
            std::function<void(const std::string&)> done);

bool shortcut(const std::shared_ptr<State>& st, const KeyEvent& e);

void addTab(const std::shared_ptr<State>& st, const std::string& path) {
    // Already open: that tab
    if (!path.empty())
        for (size_t i = 0; i < st->tabs.size(); ++i)
            if (st->tabs[i].doc->path == path) return activate(st, static_cast<int>(i));
    auto doc = std::make_shared<Doc>();
    if (!path.empty()) {
        std::string err;
        std::error_code ec;
        if (fs::exists(path, ec) && !loadDoc(*doc, path, err)) {
            m3::run({"notify-send", "-a", "Text Editor", "-i", "dialog-error", "Couldn't open " + fs::path(path).filename().string(), err}, nullptr);
            if (!st->tabs.empty()) return;
        } else if (!fs::exists(path, ec)) {
            doc->path = path; // a new file, made on saving
        }
    }
    // An untouched empty tab is replaced
    if (st->tabs.size() == 1 && st->tabs[0].doc->path.empty() && !st->tabs[0].doc->modified && !path.empty()) {
        st->stack->remove(st->tabs[0].editor);
        st->tabs.clear();
    }
    auto* ed = st->stack->add<Editor>(doc);
    ed->grow = true;
    std::weak_ptr<State> weak = st;
    ed->onChanged = [weak] {
        if (auto s = weak.lock(); s && *s->alive && s->rebuildTabs) s->rebuildTabs(), s->refreshStatus();
    };
    ed->onMoved = [weak] {
        if (auto s = weak.lock(); s && *s->alive && s->refreshStatus) s->refreshStatus();
    };
    ed->onOtherKey = [weak](const KeyEvent& e) {
        auto s = weak.lock();
        return s && *s->alive && shortcut(s, e);
    };
    st->tabs.push_back({doc, ed});
    activate(st, static_cast<int>(st->tabs.size()) - 1);
}

void saveTab(const std::shared_ptr<State>& st, int i, bool as) {
    if (i < 0 || i >= static_cast<int>(st->tabs.size())) return;
    auto doc = st->tabs[static_cast<size_t>(i)].doc;
    auto write = [st, doc](const std::string& path) {
        std::string err;
        if (!saveDoc(*doc, path, err)) {
            m3::run({"notify-send", "-a", "Text Editor", "-i", "dialog-error", "Couldn't save", err}, nullptr);
            return;
        }
        if (st->rebuildTabs) st->rebuildTabs();
        if (st->refreshStatus) st->refreshStatus();
    };
    if (doc->path.empty() || as) {
        const char* docs = std::getenv("XDG_DOCUMENTS_DIR");
        const std::string base = doc->path.empty() ? (docs && *docs ? std::string(docs) : paths::home() + "/Documents") + "/Untitled.txt" : doc->path;
        prompt(st, "save_as", "Where to save it (a path)", base, "Save", [write](const std::string& p) {
            std::string path = p;
            if (path.starts_with("~/")) path = paths::home() + path.substr(1);
            std::error_code ec;
            fs::create_directories(fs::path(path).parent_path(), ec);
            write(path);
        });
        return;
    }
    write(doc->path);
}

void closeTab(const std::shared_ptr<State>& st, int i) {
    if (i < 0 || i >= static_cast<int>(st->tabs.size())) return;
    auto& t = st->tabs[static_cast<size_t>(i)];
    if (t.doc->modified && !t.doc->lines.empty() && !(t.doc->lines.size() == 1 && t.doc->lines[0].empty())) {
        // Asked first, in the bar
        auto* bar = st->promptBar;
        bar->clearChildren();
        bar->setVisible(true);
        bar->add<Icon>("warning", 20)->setColor("tertiary", false);
        auto* l = bar->add<Label>(t.doc->name() + " has unsaved changes");
        l->setSize(14);
        l->grow = true;
        pillButton(bar, "Save", true, [st, i] {
            st->promptBar->setVisible(false);
            saveTab(st, i, false);
        }, "save");
        pillButton(bar, "Close without saving", false, [st, i] {
            st->promptBar->setVisible(false);
            st->tabs[static_cast<size_t>(i)].doc->modified = false;
            closeTab(st, i);
        }, "delete");
        iconButton(bar, "close", [st] { st->promptBar->setVisible(false); }, 20);
        return;
    }
    Editor* ed = t.editor;
    st->tabs.erase(st->tabs.begin() + i);
    EventLoop::get().defer([st, ed] {
        if (*st->alive) st->stack->remove(ed);
    });
    if (st->tabs.empty()) {
        if (st->popup) st->popup->close();
        return;
    }
    activate(st, std::min(i, static_cast<int>(st->tabs.size()) - 1));
}

void prompt(const std::shared_ptr<State>& st, const std::string& icon, const std::string& placeholder, const std::string& value, const std::string& action,
            std::function<void(const std::string&)> done) {
    auto* bar = st->promptBar;
    bar->clearChildren();
    if (!done) return; // (the caller fills it)
    bar->setVisible(true);
    bar->add<Icon>(icon, 20)->setColor("primary", false);
    auto* f = bar->add<TextField>();
    f->grow = true;
    f->bare = true;
    f->setPlaceholder(placeholder);
    f->setText(value);
    auto go = [st, f, done] {
        const std::string v = f->text();
        st->promptBar->setVisible(false);
        if (Editor* e = activeEditor(st)) e->focus();
        done(v);
    };
    f->onSubmit = [go](const std::string&) { go(); };
    f->onCancel = [st] {
        st->promptBar->setVisible(false);
        if (Editor* e = activeEditor(st)) e->focus();
    };
    pillButton(bar, action, true, go);
    iconButton(bar, "close", [st] {
        st->promptBar->setVisible(false);
        if (Editor* e = activeEditor(st)) e->focus();
    }, 20);
    EventLoop::get().defer([st, f] {
        if (*st->alive) f->focus();
    });
}

// Find: every match in the open file, marked; Enter steps through them
void findAll(const std::shared_ptr<State>& st, bool step, int dir = 1) {
    Editor* e = activeEditor(st);
    if (!e) return;
    const std::string q = st->findField ? st->findField->text() : "";
    e->highlights.clear();
    e->currentMatch = -1;
    if (!q.empty()) {
        auto low = [&](std::string s) {
            if (!st->matchCase)
                for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        const std::string needle = low(q);
        for (int i = 0; i < static_cast<int>(e->doc->lines.size()) && e->highlights.size() < 20000; ++i) {
            const std::string hay = low(e->doc->lines[static_cast<size_t>(i)]);
            for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + std::max<size_t>(1, needle.size())))
                e->highlights.push_back({{i, at}, {i, at + needle.size()}});
        }
        if (!e->highlights.empty()) {
            // The next one from the caret
            int pick = dir > 0 ? 0 : static_cast<int>(e->highlights.size()) - 1;
            for (int m = 0; m < static_cast<int>(e->highlights.size()); ++m) {
                const auto& h = e->highlights[static_cast<size_t>(m)];
                if (dir > 0 && (step ? (e->cursor < h.first || (e->cursor == h.first && !e->hasSelection())) : !(h.first < e->ordered().first))) {
                    pick = m;
                    break;
                }
                if (dir < 0 && h.first < e->ordered().first) pick = m;
            }
            e->currentMatch = pick;
            const auto& h = e->highlights[static_cast<size_t>(pick)];
            e->anchor = h.first;
            e->cursor = h.second;
            e->moved();
        }
    }
    e->requestRedraw();
    if (st->refreshStatus) st->refreshStatus();
}

void replaceOne(const std::shared_ptr<State>& st, bool all) {
    Editor* e = activeEditor(st);
    if (!e || !st->findField || st->findField->text().empty()) return;
    const std::string with = st->replaceField ? st->replaceField->text() : "";
    if (all) {
        findAll(st, false);
        if (e->highlights.empty()) return;
        e->snapshot(false);
        // From the end, so the earlier positions hold
        for (auto it = e->highlights.rbegin(); it != e->highlights.rend(); ++it) {
            auto& l = e->doc->lines[static_cast<size_t>(it->first.line)];
            l.replace(it->first.col, it->second.col - it->first.col, with);
        }
        const size_t n = e->highlights.size();
        e->highlights.clear();
        e->anchor = e->cursor = {std::min(e->cursor.line, static_cast<int>(e->doc->lines.size()) - 1), 0};
        e->edited();
        m3::run({"notify-send", "-a", "Text Editor", "-i", "edit-find-replace", std::format("Replaced {}", n)}, nullptr);
        return;
    }
    if (e->hasSelection() && e->currentMatch >= 0) e->insert(with);
    findAll(st, true);
}

bool shortcut(const std::shared_ptr<State>& st, const KeyEvent& e) {
    const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
    if (e.sym == XKB_KEY_Escape) {
        if (st->findBar->visible()) {
            st->findBar->setVisible(false);
            if (Editor* ed = activeEditor(st)) ed->highlights.clear(), ed->requestRedraw(), ed->focus();
            return true;
        }
        return false;
    }
    if (e.sym == XKB_KEY_F3) return findAll(st, true, e.shift() ? -1 : 1), true;
    if (!e.ctrl()) return false;
    switch (k) {
    case 's': saveTab(st, st->active, e.shift()); return true;
    case 'n': addTab(st, ""); return true;
    case 'w': closeTab(st, st->active); return true;
    case 'o':
        if (open) open("files " + (activeEditor(st) && !activeEditor(st)->doc->path.empty() ? fs::path(activeEditor(st)->doc->path).parent_path().string() : paths::home()));
        return true;
    case 'f':
    case 'h': {
        st->findBar->setVisible(true);
        Editor* ed = activeEditor(st);
        if (ed && ed->hasSelection() && ed->ordered().first.line == ed->ordered().second.line) st->findField->setText(ed->selected());
        EventLoop::get().defer([st, k] {
            if (!*st->alive) return;
            (k == 'h' && st->replaceField ? st->replaceField : st->findField)->focus();
        });
        return true;
    }
    case 'g':
        prompt(st, "move_down", "Go to line", "", "Go", [st](const std::string& v) {
            if (Editor* ed = activeEditor(st)) ed->gotoLine(std::max(1, std::atoi(v.c_str())) - 1);
        });
        return true;
    case '=':
    case '+':
        if (Editor* ed = activeEditor(st)) ed->setFontSize(ed->fontSize + 1);
        return true;
    case '-':
        if (Editor* ed = activeEditor(st)) ed->setFontSize(ed->fontSize - 1);
        return true;
    case '0':
        if (Editor* ed = activeEditor(st)) ed->setFontSize(14);
        return true;
    default: break;
    }
    if (e.sym == XKB_KEY_Tab || e.sym == XKB_KEY_ISO_Left_Tab || e.sym == XKB_KEY_Page_Down || e.sym == XKB_KEY_Page_Up) {
        const int n = static_cast<int>(st->tabs.size());
        const int dir = (e.sym == XKB_KEY_Page_Up || e.shift() || e.sym == XKB_KEY_ISO_Left_Tab) ? -1 : 1;
        activate(st, ((st->active + dir) % n + n) % n);
        return true;
    }
    return false;
}

} // namespace

void textEditor(Flex* card, Popup& popup, const std::string& file) {
    auto st = std::make_shared<State>();
    st->popup = &popup;
    card->setPadding(Insets(8))->setSpacing(6);

    st->tabRow = card->add<Flex>(Flex::Direction::Row);
    st->tabRow->setSpacing(4)->setAlignItems(Align::Center)->setPadding({4, 0});
    st->promptBar = card->add<Flex>(Flex::Direction::Row);
    st->promptBar->setSpacing(8)->setAlignItems(Align::Center)->setPadding({14, 4, 6, 4})->setRadius(Radii(22));
    st->promptBar->setBackground("surface_container_high", false);
    st->promptBar->setVisible(false);

    st->findBar = card->add<Flex>(Flex::Direction::Row);
    st->findBar->setSpacing(6)->setAlignItems(Align::Center)->setPadding({4, 0});
    {
        auto field = [&](const char* icon, const char* ph) {
            auto* box = st->findBar->add<Flex>(Flex::Direction::Row);
            pill(box->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({14, 4, 8, 4})->setSpacing(8);
            box->grow = true;
            box->add<Icon>(icon, 18)->setColor("on_surface_variant", false);
            auto* f = box->add<TextField>();
            f->bare = true;
            f->grow = true;
            f->setPlaceholder(ph);
            return f;
        };
        st->findField = field("search", "Find");
        st->findField->onChanged = [st](const std::string&) {
            EventLoop::get().defer([st] {
                if (*st->alive) findAll(st, false), st->findField->focus();
            });
        };
        st->findField->onSubmit = [st](const std::string&) { findAll(st, true, wl::Display::get().heldModifiers() & ModShift ? -1 : 1); };
        auto* cs = iconButton(st->findBar, "match_case", [] {}, 20);
        cs->tooltip = "Match case";
        cs->onClicked = [st, cs](uint32_t b) {
            if (b != BTN_LEFT) return;
            st->matchCase = !st->matchCase;
            cs->setBackground(st->matchCase ? "secondary_container" : ColorRef::none(), false);
            findAll(st, false);
        };
        iconButton(st->findBar, "keyboard_arrow_up", [st] { findAll(st, true, -1); }, 20)->tooltip = "Previous (Shift+F3)";
        iconButton(st->findBar, "keyboard_arrow_down", [st] { findAll(st, true, 1); }, 20)->tooltip = "Next (F3)";
        st->replaceField = field("find_replace", "Replace with");
        st->replaceField->onSubmit = [st](const std::string&) { replaceOne(st, false); };
        pillButton(st->findBar, "Replace", false, [st] { replaceOne(st, false); });
        pillButton(st->findBar, "All", false, [st] { replaceOne(st, true); });
        iconButton(st->findBar, "close", [st] {
            st->findBar->setVisible(false);
            if (Editor* ed = activeEditor(st)) ed->highlights.clear(), ed->requestRedraw(), ed->focus();
        }, 20);
    }
    st->findBar->setVisible(false);

    st->stack = card->add<Flex>(Flex::Direction::Column);
    st->stack->setAlignItems(Align::Stretch);
    st->stack->grow = true;
    st->status = card->add<Label>("");
    st->status->setSize(12)->setColor("on_surface_variant", false);
    st->status->alignSelf = Align::Start;
    card->add<TextKeeper>(st);

    st->rebuildTabs = [st] {
        auto* t = st->tabRow;
        t->clearChildren();
        for (size_t i = 0; i < st->tabs.size(); ++i) {
            const auto& tab = st->tabs[i];
            const bool here = static_cast<int>(i) == st->active;
            auto* b = t->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({14, 6, 6, 6})->setSpacing(6);
            b->setBackground(here ? "secondary_container" : "surface_container_high", false);
            b->add<Icon>(tab.doc->modified ? "circle" : "description", tab.doc->modified ? 10 : 16)->setFill(true)->setColor(tab.doc->modified ? "tertiary" : "on_surface_variant", false);
            b->add<Label>(tab.doc->name())->setSize(13)->setWeight(here ? 600 : 500)->setMaxWidth(200)->setColor(here ? "on_secondary_container" : "on_surface", false);
            b->givesWay = true;
            b->tooltip = tab.doc->path.empty() ? "Not saved yet" : fileops::displayPath(tab.doc->path);
            b->onClicked = [st, i](uint32_t bt) {
                if (bt == BTN_LEFT) EventLoop::get().defer([st, i] {
                    if (*st->alive) activate(st, static_cast<int>(i));
                });
            };
            iconButton(b, "close", [st, i] {
                EventLoop::get().defer([st, i] {
                    if (*st->alive) closeTab(st, static_cast<int>(i));
                });
            }, 14)->setPadding(Insets(4));
        }
        iconButton(t, "add", [st] {
            EventLoop::get().defer([st] {
                if (*st->alive) addTab(st, "");
            });
        }, 20)->tooltip = "New (Ctrl+N)";
        t->add<Node>()->grow = true;
        iconButton(t, "search", [st] {
            KeyEvent e;
            e.baseSym = 'f';
            e.mods = ModCtrl;
            shortcut(st, e);
        }, 20)->tooltip = "Find and replace (Ctrl+F, Ctrl+H)";
        iconButton(t, "save", [st] { saveTab(st, st->active, false); }, 20)->tooltip = "Save (Ctrl+S)";
        Button* more = iconButton(t, "more_vert", [] {}, 20);
        more->onClicked = [st, more](uint32_t b) {
            if (b != BTN_LEFT) return;
            std::vector<MenuItem> items;
            items.emplace_back("save_as", "Save as…", "Ctrl+Shift+S", [st] { saveTab(st, st->active, true); });
            items.emplace_back("folder_open", "Open in Files…", "Ctrl+O", [st] {
                KeyEvent e;
                e.baseSym = 'o';
                e.mods = ModCtrl;
                shortcut(st, e);
            });
            items.emplace_back("move_down", "Go to line", "Ctrl+G", [st] {
                KeyEvent e;
                e.baseSym = 'g';
                e.mods = ModCtrl;
                shortcut(st, e);
            });
            if (Editor* ed = activeEditor(st)) {
                auto doc = ed->doc;
                items.emplace_back("keyboard_return", doc->crlf ? "Line endings: Windows (CRLF)" : "Line endings: Unix (LF)", "change", [st, doc] {
                    doc->crlf = !doc->crlf;
                    doc->modified = true;
                    if (st->rebuildTabs) st->rebuildTabs();
                    if (st->refreshStatus) st->refreshStatus();
                }, false, false, true, true);
                items.emplace_back("text_increase", "Bigger text", "Ctrl+=", [ed] { ed->setFontSize(ed->fontSize + 1); });
                items.emplace_back("text_decrease", "Smaller text", "Ctrl+−", [ed] { ed->setFontSize(ed->fontSize - 1); });
                items.emplace_back("history", "Kept unsaved changes", "", [] {
                    if (open) open("files " + recoveryDir());
                }, false, false, true, true);
            }
            floatingMenu(more, items);
        };
    };
    st->refreshStatus = [st] {
        Editor* e = activeEditor(st);
        if (!e) return;
        const auto& d = *e->doc;
        const std::string line = d.lines[static_cast<size_t>(e->cursor.line)];
        const long col = g_utf8_strlen(line.c_str(), static_cast<gssize>(e->cursor.col)) + 1;
        std::string s = std::format("Ln {}, Col {}", e->cursor.line + 1, col);
        if (e->hasSelection()) s += std::format("  ({} selected)", g_utf8_strlen(e->selected().c_str(), -1));
        s += std::format("   ·   {} lines   ·   {}   ·   {}", d.lines.size(), d.latin1 ? "Latin-1" : "UTF-8", d.crlf ? "CRLF" : "LF");
        if (!e->highlights.empty()) s += std::format("   ·   match {} of {}", e->currentMatch + 1, e->highlights.size());
        else if (st->findBar->visible() && st->findField && !st->findField->text().empty()) s += "   ·   no matches";
        st->status->setText(s);
    };

    popup.onKey = [st](const KeyEvent& e) {
        if (shortcut(st, e)) return true;
        // A key while a button had the focus: the text takes it
        if (Editor* ed = activeEditor(st)) {
            ed->focus();
            return ed->onKey(e);
        }
        return false;
    };
    textGo = [st](const std::string& f) {
        if (*st->alive) addTab(st, f);
    };
    addTab(st, file);
}

// Settings → Text Editor
void textSettings(Flex* page) {
    auto* g = group(page, "Text");
    sliderRow(g, "format_size", "Text size", "/text/fontSize", 14, 8, 32, 1, [](double v) { return std::format("{:.0f} px", v); });
    switchRow(g, "format_list_numbered", "Line numbers", "", "/text/lineNumbers", true);
    switchRow(g, "horizontal_rule", "Mark the current line", "", "/text/currentLine", true);
    auto* i = group(page, "Indentation");
    choiceRow<int>(i, "keyboard_tab", "Tab width", "How wide a tab shows, and what Tab indents by", "/text/tabWidth", 4, {{2, "2"}, {4, "4"}, {8, "8"}});
    choiceRow<std::string>(i, "format_indent_increase", "Indent with", "Tab, Shift+Tab and new lines", "/text/indent", "auto",
                           {{"auto", "Like the file"}, {"spaces", "Spaces"}, {"tabs", "Tabs"}});
    caption(page, "Unsaved changes are kept in " + fileops::displayPath(recoveryDir()) + " when the window closes");
}

void textEditorClosed() {
    textGo = nullptr;
}

} // namespace m3::shell::panels
