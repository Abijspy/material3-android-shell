// The terminal: the shell's own, in an app window, on libvterm (the
// emulator Neovim's terminal uses: xterm-256colour, truecolour, the
// alternate screen, bracketed paste, mouse reporting, wide characters).
//
// Each tab runs your shell on a pseudo-terminal. The screen is drawn with
// Pango into a Cairo picture a row at a time (only the rows that changed),
// handed to GL as a texture. Colours come from the wallpaper's palette.
// Scrollback, selection with copy and paste, font size, tabs.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <vterm.h>

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <climits>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&, const std::vector<std::string>&)> terminalGo;

namespace {

// Lines kept above the screen (Settings → Terminal)
size_t scrollbackLines() {
    return static_cast<size_t>(std::clamp(Config::get().value<int>("/terminal/scrollback", 10000), 500, 200000));
}
constexpr float kPad = 10;

struct Rgb {
    double r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

Rgb rgbOf(const Color& c) {
    return {c.r, c.g, c.b};
}
Rgb mix(Rgb a, Rgb b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// The 16 colours, from the palette: blue is its primary, magenta its
// tertiary, red its error colour; green, yellow and cyan fixed hues at the
// same lightness; the brights a step towards the text
struct Palette {
    Rgb fg, bg, cursor, cursorText, selection;
    Rgb ansi[16];
};

Palette palette() {
    auto& t = Theme::get();
    const Color surface = t.color("surface");
    const bool dark = 0.2126f * surface.r + 0.7152f * surface.g + 0.0722f * surface.b < 0.5f;
    Palette p;
    p.bg = rgbOf(t.color("surface_container_lowest"));
    p.fg = rgbOf(t.color("on_surface"));
    p.cursor = rgbOf(t.color("primary"));
    p.cursorText = rgbOf(t.color("on_primary"));
    p.selection = rgbOf(t.color("primary"));
    auto hex = [](const char* h) { return rgbOf(*Color::fromHex(h)); };
    p.ansi[0] = rgbOf(t.color(dark ? "surface_container_highest" : "on_surface_variant"));
    p.ansi[1] = rgbOf(t.color("error"));
    p.ansi[2] = dark ? hex("#8fd694") : hex("#2e7d32");
    p.ansi[3] = dark ? hex("#e8c872") : hex("#8a6400");
    p.ansi[4] = rgbOf(t.color("primary"));
    p.ansi[5] = rgbOf(t.color("tertiary"));
    p.ansi[6] = dark ? hex("#7fd4d8") : hex("#00687a");
    p.ansi[7] = rgbOf(t.color("on_surface_variant"));
    p.ansi[8] = rgbOf(t.color("outline"));
    for (int i = 1; i < 7; ++i) p.ansi[8 + i] = mix(p.ansi[i], p.fg, 0.3);
    p.ansi[15] = p.fg;
    return p;
}

// ── One session: a shell on a pseudo-terminal, and the screen libvterm keeps ──

class Session;

class PtySource : public EventSource {
public:
    explicit PtySource(Session* s, int fd) : m_session(s), m_fd(fd) {}
    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    Session* m_session;
    int m_fd;
};

class TermView;

class Session {
public:
    using Line = std::vector<VTermScreenCell>;

    VTerm* vt = nullptr;
    VTermScreen* screen = nullptr;
    int master = -1;
    pid_t pid = -1;
    int rows = 24, cols = 80;
    std::deque<Line> scrollback;
    std::string title = "Terminal", titleBuf;
    bool cursorVisible = true, altScreen = false;
    int cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK, mouseMode = 0;
    VTermPos cursor{0, 0};
    std::unique_ptr<PtySource> source;
    std::function<void(int, int)> damaged; // rows from, to (screen rows)
    std::function<void()> titled, exited, scrolled;
    bool dead = false;

    Session(const std::string& dir, const std::vector<std::string>& command) {
        vt = vterm_new(rows, cols);
        vterm_set_utf8(vt, 1);
        vterm_output_set_callback(vt, &Session::output, this);
        screen = vterm_obtain_screen(vt);
        static const VTermScreenCallbacks callbacks = {
            .damage = &Session::onDamage,
            .moverect = nullptr,
            .movecursor = &Session::onMoveCursor,
            .settermprop = &Session::onProp,
            .bell = nullptr,
            .resize = nullptr,
            .sb_pushline = &Session::onPushLine,
            .sb_popline = &Session::onPopLine,
            .sb_clear = &Session::onClearScrollback,
        };
        vterm_screen_set_callbacks(screen, &callbacks, this);
        vterm_screen_set_damage_merge(screen, VTERM_DAMAGE_SCROLL);
        vterm_screen_enable_altscreen(screen, 1);
        applyPalette();
        vterm_screen_reset(screen, 1);
        m_dir = dir;
        m_command = command;
        // The cursor until a program asks for another (Settings → Terminal)
        const std::string cur = Config::get().value<std::string>("/terminal/cursor", "block");
        cursorShape = cur == "beam" ? VTERM_PROP_CURSORSHAPE_BAR_LEFT : cur == "underline" ? VTERM_PROP_CURSORSHAPE_UNDERLINE : VTERM_PROP_CURSORSHAPE_BLOCK;
        // Named for what runs in it, until it names itself
        if (!command.empty()) title = fs::path(command[0]).filename().string();
        else if (const char* sh = std::getenv("SHELL"); sh && *sh) title = fs::path(sh).filename().string();
    }

    // Started once the window knows how many rows and columns there are, so
    // the shell begins at the right size
    bool started = false;
    void start() {
        if (started) return;
        started = true;
        spawn(m_dir, m_command);
    }

    ~Session() {
        if (source) EventLoop::get().remove(source.get());
        if (master >= 0) close(master);
        if (pid > 0 && !reaped) {
            ::kill(-pid, SIGHUP);
            ::kill(pid, SIGHUP);
            reap();
        }
        vterm_free(vt);
    }

    void applyPalette() {
        const Palette p = palette();
        VTermState* state = vterm_obtain_state(vt);
        auto col = [](const Rgb& c) {
            VTermColor v;
            vterm_color_rgb(&v, static_cast<uint8_t>(std::lround(c.r * 255)), static_cast<uint8_t>(std::lround(c.g * 255)), static_cast<uint8_t>(std::lround(c.b * 255)));
            return v;
        };
        VTermColor fg = col(p.fg), bg = col(p.bg);
        vterm_state_set_default_colors(state, &fg, &bg);
        for (int i = 0; i < 16; ++i) {
            VTermColor c = col(p.ansi[i]);
            vterm_state_set_palette_color(state, i, &c);
        }
    }

    void resize(int r, int c) {
        r = std::max(2, r), c = std::max(2, c);
        if (r == rows && c == cols) return;
        rows = r, cols = c;
        vterm_set_size(vt, rows, cols);
        winsize ws{static_cast<unsigned short>(rows), static_cast<unsigned short>(cols), 0, 0};
        if (master >= 0) ioctl(master, TIOCSWINSZ, &ws);
    }

    void write(const char* data, size_t len) {
        while (len > 0 && master >= 0) {
            const ssize_t n = ::write(master, data, len);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN) {
                    pollfd p{master, POLLOUT, 0};
                    poll(&p, 1, 50);
                    continue;
                }
                return;
            }
            data += n, len -= static_cast<size_t>(n);
        }
    }

    void read() {
        char buf[65536];
        for (int round = 0; round < 8; ++round) {
            const ssize_t n = ::read(master, buf, sizeof buf);
            if (n > 0) {
                vterm_input_write(vt, buf, static_cast<size_t>(n));
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
            // EOF or EIO: the shell is gone
            finish();
            return;
        }
        vterm_screen_flush_damage(screen);
    }

    // The directory the shell is in now (a new tab starts there)
    std::string cwd() const {
        std::error_code ec;
        const auto p = fs::read_symlink(std::format("/proc/{}/cwd", pid), ec);
        return ec ? paths::home() : p.string();
    }

    // Is something other than the shell running in it (closing asks then)?
    bool busy() const {
        if (master < 0 || dead) return false;
        const pid_t fg = tcgetpgrp(master);
        return fg > 0 && fg != pid;
    }

private:
    std::string m_dir;
    std::vector<std::string> m_command;

    void spawn(const std::string& dir, const std::vector<std::string>& command) {
        winsize ws{static_cast<unsigned short>(rows), static_cast<unsigned short>(cols), 0, 0};
        const pid_t child = forkpty(&master, nullptr, nullptr, &ws);
        if (child < 0) {
            const char* msg = "\r\nCouldn't start a pseudo-terminal\r\n";
            vterm_input_write(vt, msg, std::strlen(msg));
            return;
        }
        if (child == 0) {
            // The shell's signal handling isn't the child's: everything back
            // to the defaults, unblocked, and none of the shell's descriptors
            sigset_t none;
            sigemptyset(&none);
            sigprocmask(SIG_SETMASK, &none, nullptr);
            for (int s = 1; s < NSIG; ++s) signal(s, SIG_DFL);
            close_range(3, UINT_MAX, 0);
            setenv("TERM", "xterm-256color", 1);
            setenv("COLORTERM", "truecolor", 1);
            setenv("TERM_PROGRAM", "material3ui", 1);
            unsetenv("M3_INSTANCE");
            unsetenv("M3_NO_SUPERVISOR");
            if (chdir(dir.c_str()) != 0 && chdir(paths::home().c_str()) != 0) {
            }
            if (!command.empty()) {
                std::vector<char*> argv;
                for (const auto& a : command) argv.push_back(const_cast<char*>(a.c_str()));
                argv.push_back(nullptr);
                execvp(argv[0], argv.data());
                const std::string err = std::format("material3ui: couldn't run {}: {}\r\n", command[0], std::strerror(errno));
                [[maybe_unused]] auto w = ::write(1, err.data(), err.size());
                _exit(127);
            }
            // A shell of your choosing (Settings), as a command line
            const std::string custom = Config::get().value<std::string>("/terminal/shell", "");
            if (!custom.empty()) {
                execl("/bin/sh", "sh", "-c", ("exec " + custom).c_str(), static_cast<char*>(nullptr));
                _exit(127);
            }
            const char* sh = std::getenv("SHELL");
            std::string shell = sh && *sh ? sh : "";
            if (shell.empty())
                if (const passwd* pw = getpwuid(getuid()); pw && pw->pw_shell) shell = pw->pw_shell;
            if (shell.empty()) shell = "/bin/bash";
            execl(shell.c_str(), shell.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        pid = child;
        fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
        fcntl(master, F_SETFD, FD_CLOEXEC);
        source = std::make_unique<PtySource>(this, master);
        EventLoop::get().add(source.get());
    }

    void reap() {
        // Most shells go on a hang-up at once; waited for briefly, then left
        // to the loop's next tries
        for (int i = 0; i < 20; ++i) {
            if (waitpid(pid, nullptr, WNOHANG) != 0) return;
            usleep(5000);
        }
        const pid_t p = pid;
        EventLoop::get().addTimer(500, [p] {
            if (waitpid(p, nullptr, WNOHANG) == 0) {
                ::kill(p, SIGKILL);
                waitpid(p, nullptr, 0);
            }
        });
    }

    void finish() {
        if (dead) return;
        // Its own exit status, if it's there yet
        int status = 0;
        int code = 0;
        for (int i = 0; i < 20; ++i) {
            if (waitpid(pid, &status, WNOHANG) == pid) {
                code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
                reaped = true;
                break;
            }
            usleep(5000);
        }
        dead = true;
        if (source) EventLoop::get().remove(source.get());
        // Removed from the loop while it dispatches: freed after
        EventLoop::get().defer([s = source.release()] { delete s; });
        close(master);
        master = -1;
        if (!reaped) reap();
        // A clean exit closes the tab; a failure stays to be read
        if (code == 0 || !reaped) {
            if (exited) exited();
            return;
        }
        const std::string msg = std::format("\r\n\x1b[2m[exited with code {}: press any key to close]\x1b[0m", code);
        vterm_input_write(vt, msg.data(), msg.size());
        vterm_screen_flush_damage(screen);
        lingering = true;
    }

public:
    bool reaped = false, lingering = false;
    void dismiss() {
        if (lingering && exited) exited();
    }

private:

    static void output(const char* s, size_t len, void* user) { static_cast<Session*>(user)->write(s, len); }
    static int onDamage(VTermRect r, void* user) {
        auto* s = static_cast<Session*>(user);
        if (s->damaged) s->damaged(r.start_row, r.end_row);
        return 1;
    }
    static int onMoveCursor(VTermPos pos, VTermPos old, int visible, void* user) {
        auto* s = static_cast<Session*>(user);
        s->cursor = pos;
        s->cursorVisible = visible;
        if (s->damaged) s->damaged(std::min(pos.row, old.row), std::max(pos.row, old.row) + 1);
        return 1;
    }
    static int onProp(VTermProp prop, VTermValue* v, void* user) {
        auto* s = static_cast<Session*>(user);
        switch (prop) {
        case VTERM_PROP_TITLE:
            if (v->string.initial) s->titleBuf.clear();
            s->titleBuf.append(v->string.str, v->string.len);
            if (v->string.final) {
                s->title = s->titleBuf.empty() ? "Terminal" : s->titleBuf;
                if (s->titled) s->titled();
            }
            break;
        case VTERM_PROP_CURSORVISIBLE: s->cursorVisible = v->boolean; break;
        case VTERM_PROP_CURSORSHAPE: s->cursorShape = v->number; break;
        case VTERM_PROP_MOUSE: s->mouseMode = v->number; break;
        case VTERM_PROP_ALTSCREEN: s->altScreen = v->boolean; break;
        default: break;
        }
        if (s->damaged) s->damaged(s->cursor.row, s->cursor.row + 1);
        return 1;
    }
    static int onPushLine(int cols, const VTermScreenCell* cells, void* user) {
        auto* s = static_cast<Session*>(user);
        s->scrollback.emplace_back(cells, cells + cols);
        const size_t keep = scrollbackLines();
        while (s->scrollback.size() > keep) s->scrollback.pop_front();
        if (s->scrolled) s->scrolled();
        return 1;
    }
    static int onPopLine(int cols, VTermScreenCell* cells, void* user) {
        auto* s = static_cast<Session*>(user);
        if (s->scrollback.empty()) return 0;
        const Line& l = s->scrollback.back();
        for (int i = 0; i < cols; ++i) {
            if (i < static_cast<int>(l.size())) cells[i] = l[static_cast<size_t>(i)];
            else {
                cells[i] = VTermScreenCell{};
                cells[i].width = 1;
                cells[i].fg = l.empty() ? VTermColor{} : l.front().fg;
                cells[i].bg = l.empty() ? VTermColor{} : l.front().bg;
            }
        }
        s->scrollback.pop_back();
        if (s->scrolled) s->scrolled();
        return 1;
    }
    static int onClearScrollback(void* user) {
        auto* s = static_cast<Session*>(user);
        s->scrollback.clear();
        if (s->scrolled) s->scrolled();
        return 1;
    }
};

void PtySource::dispatch(short revents) {
    if (revents) m_session->read();
}

// ── Drawing it: rows into a Cairo picture, the picture into a texture ──

struct Font {
    PangoFontDescription *regular = nullptr, *bold = nullptr, *italic = nullptr, *boldItalic = nullptr;
    double cellW = 8, cellH = 16, ascent = 12;
    float px = 0;
    ~Font() {
        for (auto* d : {regular, bold, italic, boldItalic})
            if (d) pango_font_description_free(d);
    }
};

class TermView : public Node {
public:
    std::shared_ptr<Session> session;
    std::function<void()> onActivity;
    std::function<void(float, float)> onMenu;
    std::function<bool(const KeyEvent&)> onShortcut; // the window's keys, asked first
    float fontSize = 14;

    explicit TermView(std::shared_ptr<Session> s) : session(std::move(s)) {
        fontSize = static_cast<float>(Config::get().value<double>("/terminal/fontSize", 14.0));
        session->damaged = [this](int from, int to) { damage(from, to); };
        session->scrolled = [this] {
            // New lines while scrolled back: the view stays on what it showed
            if (m_scroll > 0) m_scroll = std::min<int>(m_scroll + 1, static_cast<int>(session->scrollback.size()));
            if (m_scroll > 0) damageAll();
        };
        m_configConn = Config::get().changed.connect([this] {
            const float want = static_cast<float>(Config::get().value<double>("/terminal/fontSize", 14.0));
            if (std::abs(want - fontSize) > 0.01f) {
                fontSize = want;
                m_font.px = 0;
                requestRedraw();
            }
        });
        m_themeConn = Theme::get().changed.connect([this] {
            session->applyPalette();
            m_palette = palette();
            vterm_screen_flush_damage(session->screen);
            damageAll();
        });
        m_palette = palette();
    }
    ~TermView() override {
        *m_alive = false;
        session->damaged = nullptr;
        session->scrolled = nullptr;
        if (m_surface) cairo_surface_destroy(m_surface);
        if (m_tex) {
            Renderer::get().makeCurrent();
            glDeleteTextures(1, &m_tex);
        }
        if (m_layoutCtx) g_object_unref(m_layoutCtx);
        if (m_fontMap) g_object_unref(m_fontMap);
    }

    bool interactive() const override { return true; }
    bool keyboardNavigable() const override { return false; }

    void setFontSize(float px) {
        fontSize = std::clamp(px, 7.0f, 40.0f);
        Config::get().set("/terminal/fontSize", fontSize);
        m_font.px = 0; // measured again
        requestRedraw();
    }

    // Keys: what's typed goes to the shell (Ctrl+Shift for the window's own)
    bool onKey(const KeyEvent& e) override {
        if (onShortcut && onShortcut(e)) return true;
        if (session->lingering) {
            session->dismiss();
            return true;
        }
        if (session->dead) return false;
        const bool ctrl = e.ctrl(), shift = e.shift(), alt = e.mods & ModAlt;
        if (ctrl && shift) return false; // the window's: copy, paste, tabs
        int mod = VTERM_MOD_NONE;
        if (shift) mod |= VTERM_MOD_SHIFT;
        if (alt) mod |= VTERM_MOD_ALT;
        if (ctrl) mod |= VTERM_MOD_CTRL;
        const auto m = static_cast<VTermModifier>(mod);
        VTermKey key = VTERM_KEY_NONE;
        switch (e.sym) {
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: key = VTERM_KEY_ENTER; break;
        case XKB_KEY_Tab:
        case XKB_KEY_ISO_Left_Tab: key = VTERM_KEY_TAB; break;
        case XKB_KEY_BackSpace: key = VTERM_KEY_BACKSPACE; break;
        case XKB_KEY_Escape: key = VTERM_KEY_ESCAPE; break;
        case XKB_KEY_Up: key = VTERM_KEY_UP; break;
        case XKB_KEY_Down: key = VTERM_KEY_DOWN; break;
        case XKB_KEY_Left: key = VTERM_KEY_LEFT; break;
        case XKB_KEY_Right: key = VTERM_KEY_RIGHT; break;
        case XKB_KEY_Insert: key = VTERM_KEY_INS; break;
        case XKB_KEY_Delete: key = VTERM_KEY_DEL; break;
        case XKB_KEY_Home: key = VTERM_KEY_HOME; break;
        case XKB_KEY_End: key = VTERM_KEY_END; break;
        case XKB_KEY_Page_Up:
            if (shift) return scrollBy(rowsOnScreen() / 2), true;
            key = VTERM_KEY_PAGEUP;
            break;
        case XKB_KEY_Page_Down:
            if (shift) return scrollBy(-rowsOnScreen() / 2), true;
            key = VTERM_KEY_PAGEDOWN;
            break;
        default:
            if (e.sym >= XKB_KEY_F1 && e.sym <= XKB_KEY_F12) key = static_cast<VTermKey>(VTERM_KEY_FUNCTION(static_cast<int>(e.sym - XKB_KEY_F1) + 1));
            break;
        }
        if (key == VTERM_KEY_TAB && shift) {
            // Back-tab
            session->write("\x1b[Z", 3);
            return typed();
        }
        if (key != VTERM_KEY_NONE) {
            vterm_keyboard_key(session->vt, key, m);
            return typed();
        }
        // Ctrl with a letter or a symbol: its control code, from the key itself
        if (ctrl) {
            uint32_t c = e.baseSym;
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
            if (c >= 0x20 && c < 0x7f) {
                vterm_keyboard_unichar(session->vt, c, static_cast<VTermModifier>(mod & ~VTERM_MOD_SHIFT));
                return typed();
            }
            return false;
        }
        if (e.text.empty()) return false;
        // What it types, a character at a time (Shift's already in it)
        const auto alone = static_cast<VTermModifier>(mod & ~VTERM_MOD_SHIFT);
        for (uint32_t cp : utf8(e.text)) vterm_keyboard_unichar(session->vt, cp, alone);
        return typed();
    }

    void paste(const std::string& text) {
        if (session->dead || text.empty()) return;
        vterm_keyboard_start_paste(session->vt);
        std::string t = text;
        // Pasted line breaks as Enter would make them
        for (size_t i = 0; (i = t.find("\r\n", i)) != std::string::npos;) t.replace(i, 2, "\r");
        std::ranges::replace(t, '\n', '\r');
        session->write(t.data(), t.size());
        vterm_keyboard_end_paste(session->vt);
        typed();
    }

    // The selection's text, line by line ("" with none)
    std::string selection() const {
        if (!m_hasSel) return "";
        auto [a, b] = ordered();
        std::string out;
        for (int line = a.first; line <= b.first; ++line) {
            const int from = line == a.first ? a.second : 0, to = line == b.first ? b.second : session->cols - 1;
            std::string row;
            for (int c = from; c <= to; ++c) {
                VTermScreenCell cell;
                if (!cellAt(line, c, cell)) break;
                if (cell.chars[0] == 0 && cell.width == 1) row += ' ';
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) row += encode(cell.chars[i]);
                if (cell.width > 1) ++c;
            }
            row.erase(row.find_last_not_of(' ') + 1);
            out += row;
            if (line != b.first) out += '\n';
        }
        return out;
    }
    void clearSelection() {
        if (m_hasSel) m_hasSel = false, damageAll();
    }
    void selectAll() {
        m_hasSel = true;
        m_selA = {-static_cast<int>(session->scrollback.size()), 0};
        m_selB = {session->rows - 1, session->cols - 1};
        damageAll();
    }

    void scrollBy(int lines) {
        const int max = static_cast<int>(session->scrollback.size());
        const int s = std::clamp(m_scroll + lines, 0, max);
        if (s != m_scroll) m_scroll = s, damageAll();
    }
    void damageAll() {
        m_dirtyFrom = 0;
        m_dirtyTo = INT_MAX;
        requestRedraw();
    }

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, std::max(80.0f, available.h)}; }

    void onPress(uint32_t button, float x, float y) override {
        focus();
        const auto [row, col] = cellFor(x, y);
        if (session->mouseMode && !(wl::Display::get().heldModifiers() & ModShift) && m_scroll == 0) {
            const int b = button == BTN_LEFT ? 1 : button == BTN_MIDDLE ? 2 : 3;
            m_mouseButton = b;
            vterm_mouse_move(session->vt, row, col, VTERM_MOD_NONE);
            vterm_mouse_button(session->vt, b, true, VTERM_MOD_NONE);
            return;
        }
        if (button == BTN_RIGHT) {
            if (onMenu) onMenu(x, y);
            return;
        }
        if (button == BTN_MIDDLE) {
            // The primary selection, as X terminals paste it
            m3::run({"wl-paste", "--primary", "--no-newline"}, [this, alive = m_alive](int code, const std::string& out) {
                if (*alive && code == 0) paste(out);
            });
            return;
        }
        if (button != BTN_LEFT) return;
        const auto now = EventLoop::Clock::now();
        const bool dbl = now - m_lastPress < std::chrono::milliseconds(400);
        m_lastPress = now;
        const int line = row - m_scroll;
        if (dbl) {
            selectWord(line, col);
            return;
        }
        m_hasSel = false;
        m_selA = m_selB = {line, col};
        m_dragging = true;
        damageAll();
    }
    void onDrag(float x, float y) override {
        const auto [row, col] = cellFor(x, y);
        if (m_mouseButton) {
            if (session->mouseMode >= VTERM_PROP_MOUSE_DRAG) vterm_mouse_move(session->vt, row, col, VTERM_MOD_NONE);
            return;
        }
        if (!m_dragging) return;
        // Past the top or bottom: the view scrolls along
        if (y < rect().y) scrollBy(1);
        else if (y > rect().bottom()) scrollBy(-1);
        m_selB = {row - m_scroll, col};
        m_hasSel = m_selA != m_selB;
        damageAll();
    }
    void onRelease() override {
        if (m_mouseButton) {
            vterm_mouse_button(session->vt, m_mouseButton, false, VTERM_MOD_NONE);
            m_mouseButton = 0;
            return;
        }
        m_dragging = false;
        // Selected: it's the primary selection too
        if (m_hasSel) m3::run({"wl-copy", "--primary"}, nullptr, selection());
    }
    bool onScroll(int steps) override {
        if (session->mouseMode && m_scroll == 0 && !session->dead) {
            const int b = steps < 0 ? 4 : 5;
            for (int i = 0; i < std::abs(steps); ++i) {
                vterm_mouse_button(session->vt, b, true, VTERM_MOD_NONE);
                vterm_mouse_button(session->vt, b, false, VTERM_MOD_NONE);
            }
            return true;
        }
        // Full-screen apps (less, vim) without the mouse: arrows, as others do
        if (session->altScreen && m_scroll == 0) {
            for (int i = 0; i < std::abs(steps) * 3; ++i) vterm_keyboard_key(session->vt, steps < 0 ? VTERM_KEY_UP : VTERM_KEY_DOWN, VTERM_MOD_NONE);
            return true;
        }
        scrollBy(-steps * 3);
        return true;
    }

    void doPaint(Renderer& r) override {
        const auto& b = rect();
        const float scale = r.scale();
        ensureFont(scale);
        // The grid that fits
        const int cols = std::max(2, static_cast<int>((b.w - 2 * kPad) * scale / m_font.cellW));
        const int rows = std::max(2, static_cast<int>((b.h - 2 * kPad) * scale / m_font.cellH));
        session->resize(rows, cols);
        session->start();
        m_scroll = std::min<int>(m_scroll, static_cast<int>(session->scrollback.size()));

        const int w = std::max(1, static_cast<int>(std::lround(b.w * scale))), h = std::max(1, static_cast<int>(std::lround(b.h * scale)));
        bool full = false;
        if (!m_surface || w != m_w || h != m_h) {
            if (m_surface) cairo_surface_destroy(m_surface);
            m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            m_w = w, m_h = h;
            if (!m_tex) glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            full = true;
        }
        if (full || m_rows != rows || m_cols != cols || m_focusedDrawn != focused() || m_lastScroll != m_scroll) {
            m_rows = rows, m_cols = cols, m_lastScroll = m_scroll, m_focusedDrawn = focused();
            m_dirtyFrom = 0, m_dirtyTo = INT_MAX;
        }
        if (m_dirtyFrom < m_dirtyTo) {
            const int from = std::max(0, m_dirtyFrom), to = std::min(rows, m_dirtyTo);
            const bool everything = from == 0 && to >= rows;
            drawRows(from, to, everything);
            cairo_surface_flush(m_surface);
            // Only the band that changed goes up
            const int y0 = everything ? 0 : std::max(0, static_cast<int>(kPad * scale + from * m_font.cellH) - 1);
            const int y1 = everything ? h : std::min(h, static_cast<int>(std::ceil(kPad * scale + to * m_font.cellH)) + 1);
            const int stride = cairo_image_surface_get_stride(m_surface);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            if (stride == w * 4)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, w, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(m_surface) + static_cast<ptrdiff_t>(y0) * stride);
            m_dirtyFrom = INT_MAX, m_dirtyTo = 0;
        }
        r.image(m_tex, b, 1, Radii(14));
        if (m_scroll > 0) {
            // How far back: a thin bar on the right
            const float total = static_cast<float>(session->scrollback.size() + static_cast<size_t>(rows));
            const float vh = b.h * static_cast<float>(rows) / total;
            const float y = b.y + (b.h - vh) * (1 - static_cast<float>(m_scroll) / static_cast<float>(session->scrollback.size()));
            r.rect({b.right() - 6, y, 3, vh}, Theme::get().color("primary").withAlpha(0.7f), Radii(1.5f));
        }
    }

private:
    bool focused() const { return m_focused; }
    void onFocus(bool f) override {
        m_focused = f;
        requestRedraw();
    }

    bool typed() {
        if (m_scroll) m_scroll = 0, damageAll();
        if (m_hasSel) clearSelection();
        if (onActivity) onActivity();
        return true;
    }

    int rowsOnScreen() const { return session->rows; }

    void damage(int from, int to) {
        m_dirtyFrom = std::min(m_dirtyFrom, from + m_scroll);
        m_dirtyTo = std::max(m_dirtyTo, to + m_scroll);
        requestRedraw();
    }

    std::pair<int, int> cellFor(float x, float y) const {
        const float s = m_scale > 0 ? m_scale : 1;
        const int col = std::clamp(static_cast<int>(((x - rect().x - kPad) * s) / m_font.cellW), 0, session->cols - 1);
        const int row = std::clamp(static_cast<int>(((y - rect().y - kPad) * s) / m_font.cellH), 0, session->rows - 1);
        return {row, col};
    }

    // A line by number: negative ones in the scrollback (-1 the newest)
    bool cellAt(int line, int col, VTermScreenCell& cell) const {
        if (col < 0 || col >= session->cols) return false;
        if (line < 0) {
            const long idx = static_cast<long>(session->scrollback.size()) + line;
            if (idx < 0) return false;
            const auto& l = session->scrollback[static_cast<size_t>(idx)];
            if (col >= static_cast<int>(l.size())) {
                cell = VTermScreenCell{};
                cell.width = 1;
                vterm_color_indexed(&cell.fg, 7);
                cell.fg.type = VTERM_COLOR_DEFAULT_FG | VTERM_COLOR_RGB;
                cell.bg.type = VTERM_COLOR_DEFAULT_BG | VTERM_COLOR_RGB;
                return true;
            }
            cell = l[static_cast<size_t>(col)];
            return true;
        }
        if (line >= session->rows) return false;
        return vterm_screen_get_cell(session->screen, VTermPos{line, col}, &cell) != 0;
    }

    void selectWord(int line, int col) {
        auto wordy = [&](int c) {
            VTermScreenCell cell;
            if (!cellAt(line, c, cell) || !cell.chars[0]) return false;
            const uint32_t ch = cell.chars[0];
            return ch > 0x7f || std::isalnum(static_cast<int>(ch)) || std::string_view("-_./~:@%+#?=&").find(static_cast<char>(ch)) != std::string_view::npos;
        };
        if (!wordy(col)) return;
        int a = col, z = col;
        while (a > 0 && wordy(a - 1)) --a;
        while (z < session->cols - 1 && wordy(z + 1)) ++z;
        m_selA = {line, a}, m_selB = {line, z};
        m_hasSel = true;
        m3::run({"wl-copy", "--primary"}, nullptr, selection());
        damageAll();
    }

    std::pair<std::pair<int, int>, std::pair<int, int>> ordered() const {
        return m_selA <= m_selB ? std::pair{m_selA, m_selB} : std::pair{m_selB, m_selA};
    }
    bool selected(int line, int col) const {
        if (!m_hasSel) return false;
        auto [a, b] = ordered();
        return std::pair{line, col} >= a && std::pair{line, col} <= b;
    }

    void ensureFont(float scale) {
        const float px = fontSize * scale;
        if (m_font.px == px) return;
        m_scale = scale;
        if (!m_fontMap) {
            m_fontMap = pango_cairo_font_map_new();
            m_layoutCtx = pango_font_map_create_context(m_fontMap);
            cairo_font_options_t* fo = cairo_font_options_create();
            cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
            cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
            cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_ON);
            pango_cairo_context_set_font_options(m_layoutCtx, fo);
            cairo_font_options_destroy(fo);
        }
        auto make = [&](PangoWeight w, PangoStyle s) {
            PangoFontDescription* d = pango_font_description_from_string(Theme::get().monoFamily().c_str());
            pango_font_description_set_absolute_size(d, px * PANGO_SCALE);
            pango_font_description_set_weight(d, w);
            pango_font_description_set_style(d, s);
            return d;
        };
        for (auto** d : {&m_font.regular, &m_font.bold, &m_font.italic, &m_font.boldItalic})
            if (*d) pango_font_description_free(*d), *d = nullptr;
        m_font.regular = make(PANGO_WEIGHT_NORMAL, PANGO_STYLE_NORMAL);
        m_font.bold = make(PANGO_WEIGHT_BOLD, PANGO_STYLE_NORMAL);
        m_font.italic = make(PANGO_WEIGHT_NORMAL, PANGO_STYLE_ITALIC);
        m_font.boldItalic = make(PANGO_WEIGHT_BOLD, PANGO_STYLE_ITALIC);
        PangoFontMetrics* metrics = pango_context_get_metrics(m_layoutCtx, m_font.regular, nullptr);
        m_font.cellW = std::max(1.0, std::ceil(pango_font_metrics_get_approximate_digit_width(metrics) / static_cast<double>(PANGO_SCALE)));
        const double asc = pango_font_metrics_get_ascent(metrics) / static_cast<double>(PANGO_SCALE);
        const double desc = pango_font_metrics_get_descent(metrics) / static_cast<double>(PANGO_SCALE);
        m_font.ascent = std::ceil(asc);
        m_font.cellH = std::ceil(asc + desc + px * 0.12); // a little air between lines
        pango_font_metrics_unref(metrics);
        m_font.px = px;
        damageAll();
    }

    Rgb colourOf(const VTermColor& c, bool fg) const {
        if (fg && VTERM_COLOR_IS_DEFAULT_FG(&c)) return m_palette.fg;
        if (!fg && VTERM_COLOR_IS_DEFAULT_BG(&c)) return m_palette.bg;
        VTermColor x = c;
        vterm_screen_convert_color_to_rgb(session->screen, &x);
        return {x.rgb.red / 255.0, x.rgb.green / 255.0, x.rgb.blue / 255.0};
    }

    void drawRows(int from, int to, bool everything) {
        cairo_t* cr = cairo_create(m_surface);
        const double cw = m_font.cellW, ch = m_font.cellH, pad = kPad * m_scale;
        const Palette& p = m_palette;
        if (everything) {
            cairo_set_source_rgb(cr, p.bg.r, p.bg.g, p.bg.b);
            cairo_paint(cr);
        }
        PangoLayout* layout = pango_layout_new(m_layoutCtx);
        const bool cursorHere = m_scroll == 0 && session->cursorVisible && !session->dead;
        for (int row = from; row < to && row < session->rows; ++row) {
            const int line = row - m_scroll;
            const double y = pad + row * ch;
            // The row's ground first (to the edges on the first and last cells)
            cairo_set_source_rgb(cr, p.bg.r, p.bg.g, p.bg.b);
            cairo_rectangle(cr, 0, y, m_w, ch + (row == session->rows - 1 ? m_h - (y + ch) : 0));
            cairo_fill(cr);
            std::string run;
            double runX = 0;
            Rgb runFg;
            PangoFontDescription* runFont = nullptr;
            bool runUnder = false, runStrike = false;
            int runCells = 0;
            auto flush = [&] {
                if (run.empty()) return;
                pango_layout_set_font_description(layout, runFont);
                pango_layout_set_text(layout, run.c_str(), static_cast<int>(run.size()));
                cairo_set_source_rgb(cr, runFg.r, runFg.g, runFg.b);
                PangoLayoutIter* it = pango_layout_get_iter(layout);
                const double baseline = pango_layout_iter_get_baseline(it) / static_cast<double>(PANGO_SCALE);
                pango_layout_iter_free(it);
                const double runW = cw * static_cast<double>(runCells);
                int lw = 0, lh = 0;
                pango_layout_get_pixel_size(layout, &lw, &lh);
                if (lw > runW * 1.05 && lw > 0) {
                    // A fallback font's glyph wider than its cells (Indic
                    // scripts, some symbols): squeezed to fit the grid
                    cairo_save(cr);
                    cairo_translate(cr, runX, y + m_font.ascent - baseline);
                    cairo_scale(cr, runW / lw, 1);
                    cairo_move_to(cr, 0, 0);
                    pango_cairo_show_layout(cr, layout);
                    cairo_restore(cr);
                } else {
                    cairo_move_to(cr, runX, y + m_font.ascent - baseline);
                    pango_cairo_show_layout(cr, layout);
                }
                if (runUnder) {
                    cairo_rectangle(cr, runX, y + m_font.ascent + std::max(1.0, m_scale * 1.0), runW, std::max(1.0, static_cast<double>(m_scale)));
                    cairo_fill(cr);
                }
                if (runStrike) {
                    cairo_rectangle(cr, runX, y + ch * 0.5, runW, std::max(1.0, static_cast<double>(m_scale)));
                    cairo_fill(cr);
                }
                run.clear();
                runCells = 0;
            };
            for (int col = 0; col < session->cols; ++col) {
                VTermScreenCell cell;
                if (!cellAt(line, col, cell)) {
                    flush();
                    break;
                }
                Rgb fg = colourOf(cell.fg, true), bg = colourOf(cell.bg, false);
                if (cell.attrs.reverse) std::swap(fg, bg);
                const bool sel = selected(line, col);
                const bool isCursor = cursorHere && row == session->cursor.row && col == session->cursor.col;
                const int width = std::max(1, static_cast<int>(cell.width));
                const double x = pad + col * cw;
                if (sel) bg = mix(bg, p.selection, 0.45);
                if (isCursor && m_focused && session->cursorShape == VTERM_PROP_CURSORSHAPE_BLOCK) bg = p.cursor, fg = p.cursorText;
                if (!(bg == p.bg)) {
                    flush();
                    cairo_set_source_rgb(cr, bg.r, bg.g, bg.b);
                    cairo_rectangle(cr, x, y, cw * width, ch);
                    cairo_fill(cr);
                }
                if (isCursor && !(m_focused && session->cursorShape == VTERM_PROP_CURSORSHAPE_BLOCK)) {
                    flush();
                    cairo_set_source_rgb(cr, p.cursor.r, p.cursor.g, p.cursor.b);
                    const double t = std::max(1.0, 2.0 * m_scale);
                    if (!m_focused) {
                        cairo_set_line_width(cr, t / 2 + 0.5);
                        cairo_rectangle(cr, x + 0.75, y + 0.75, cw * width - 1.5, ch - 1.5);
                        cairo_stroke(cr);
                    } else if (session->cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) {
                        cairo_rectangle(cr, x, y + ch - t, cw * width, t);
                        cairo_fill(cr);
                    } else {
                        cairo_rectangle(cr, x, y, t, ch);
                        cairo_fill(cr);
                    }
                }
                PangoFontDescription* font = cell.attrs.bold ? (cell.attrs.italic ? m_font.boldItalic : m_font.bold) : (cell.attrs.italic ? m_font.italic : m_font.regular);
                const bool hidden = cell.attrs.conceal || cell.chars[0] == 0 || cell.chars[0] == ' ';
                if (hidden) {
                    flush();
                    if (cell.attrs.underline && cell.chars[0] == ' ') {
                        run = " ", runX = x, runFg = fg, runFont = font, runUnder = true, runStrike = false, runCells = 1;
                        flush();
                    }
                    col += width - 1;
                    continue;
                }
                std::string text;
                for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) text += encode(cell.chars[i]);
                // ASCII in a row of the same look goes as one run (a monospace
                // font keeps it on the grid); the rest a cell at a time
                const bool ascii = cell.chars[0] < 0x7f && cell.chars[1] == 0 && width == 1;
                const bool same = !run.empty() && ascii && fg == runFg && font == runFont && runUnder == (cell.attrs.underline != 0) && runStrike == (cell.attrs.strike != 0);
                if (!same) {
                    flush();
                    runX = x, runFg = fg, runFont = font, runUnder = cell.attrs.underline != 0, runStrike = cell.attrs.strike != 0;
                }
                run += text;
                runCells += width;
                if (!ascii) flush();
                col += width - 1;
            }
            flush();
        }
        g_object_unref(layout);
        cairo_destroy(cr);
        // Cairo's ARGB32 is BGRA in memory; the texture takes it as RGBA and
        // the image shader swaps it back
    }

    static std::vector<uint32_t> utf8(const std::string& s) {
        std::vector<uint32_t> out;
        const char* p = s.c_str();
        while (*p) {
            const gunichar c = g_utf8_get_char_validated(p, -1);
            if (c == static_cast<gunichar>(-1) || c == static_cast<gunichar>(-2)) break;
            out.push_back(c);
            p = g_utf8_next_char(p);
        }
        return out;
    }
    static std::string encode(uint32_t c) {
        char buf[8];
        const int n = g_unichar_to_utf8(c, buf);
        return std::string(buf, static_cast<size_t>(n));
    }

    Palette m_palette;
    Font m_font;
    float m_scale = 1;
    PangoFontMap* m_fontMap = nullptr;
    PangoContext* m_layoutCtx = nullptr;
    cairo_surface_t* m_surface = nullptr;
    GLuint m_tex = 0;
    int m_w = 0, m_h = 0, m_rows = 0, m_cols = 0, m_lastScroll = 0;
    int m_dirtyFrom = 0, m_dirtyTo = INT_MAX;
    int m_scroll = 0;
    bool m_focused = false, m_focusedDrawn = false;
    bool m_hasSel = false, m_dragging = false;
    int m_mouseButton = 0;
    std::pair<int, int> m_selA{0, 0}, m_selB{0, 0}; // line, column
    EventLoop::Clock::time_point m_lastPress{};
    Connection m_themeConn, m_configConn;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

struct TermState {
    Flex *tabs = nullptr, *stack = nullptr;
    std::vector<TermView*> views;
    int active = 0;
    Popup* popup = nullptr;
    std::function<void()> rebuild;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

class TermKeeper : public Node {
public:
    explicit TermKeeper(std::shared_ptr<TermState> st) : m_st(std::move(st)) { floating = true; }
    ~TermKeeper() override {
        *m_st->alive = false;
        m_st->rebuild = nullptr;
        m_st->views.clear();
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<TermState> m_st;
};

void activate(const std::shared_ptr<TermState>& st, int i) {
    if (st->views.empty()) return;
    st->active = std::clamp(i, 0, static_cast<int>(st->views.size()) - 1);
    for (size_t k = 0; k < st->views.size(); ++k) st->views[k]->setVisible(static_cast<int>(k) == st->active);
    TermView* v = st->views[static_cast<size_t>(st->active)];
    v->damageAll();
    EventLoop::get().defer([st, v] {
        if (*st->alive && std::ranges::find(st->views, v) != st->views.end()) v->focus();
    });
    if (st->rebuild) st->rebuild();
}

void closeTab(const std::shared_ptr<TermState>& st, TermView* v) {
    const auto it = std::ranges::find(st->views, v);
    if (it == st->views.end()) return;
    const int i = static_cast<int>(it - st->views.begin());
    st->views.erase(it);
    // Not inside its own callbacks: removed on the next turn
    EventLoop::get().defer([st, v] {
        if (!*st->alive) return;
        st->stack->remove(v);
    });
    if (st->views.empty()) {
        if (st->popup) st->popup->close();
        return;
    }
    activate(st, std::min(i, static_cast<int>(st->views.size()) - 1));
}

void copySelection(TermView* v) {
    const std::string text = v->selection();
    if (!text.empty()) m3::run({"wl-copy"}, nullptr, text);
}

void pasteInto(const std::shared_ptr<TermState>& st, TermView* v) {
    m3::run({"wl-paste", "--no-newline"}, [st, v](int code, const std::string& out) {
        if (code == 0 && *st->alive && std::ranges::find(st->views, v) != st->views.end()) v->paste(out);
    });
}

void newTab(const std::shared_ptr<TermState>& st, std::string dir, const std::vector<std::string>& command);

void menuFor(const std::shared_ptr<TermState>& st, TermView* v, float x, float y) {
    const bool hasSel = !v->selection().empty();
    std::vector<MenuItem> items;
    items.emplace_back("content_copy", "Copy", "Ctrl+Shift+C", [v] { copySelection(v); }, false, false, hasSel);
    items.emplace_back("content_paste", "Paste", "Ctrl+Shift+V", [st, v] { pasteInto(st, v); });
    items.emplace_back("select_all", "Select all", "Ctrl+Shift+A", [v] { v->selectAll(); });
    items.emplace_back("mop", "Clear", "Ctrl+Shift+K", [v] {
        v->session->scrollback.clear();
        v->session->write("\x0c", 1); // Ctrl+L: the shell draws its prompt at the top
        v->damageAll();
    });
    items.emplace_back("tab", "New tab", "Ctrl+Shift+T", [st, v] { newTab(st, v->session->cwd(), {}); }, false, false, true, true);
    items.emplace_back("folder_open", "Open this folder in Files", "", [v] {
        if (open) open("files " + v->session->cwd());
    });
    items.emplace_back("text_increase", "Bigger text", "Ctrl+=", [v] { v->setFontSize(v->fontSize + 1); }, false, false, true, true);
    items.emplace_back("text_decrease", "Smaller text", "Ctrl+−", [v] { v->setFontSize(v->fontSize - 1); });
    floatingMenuAt(v, x, y, items);
}

bool shortcut(const std::shared_ptr<TermState>& st, TermView* v, const KeyEvent& e) {
    const bool ctrl = e.ctrl(), shift = e.shift(), alt = e.mods & ModAlt;
    const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
    if (ctrl && shift) {
        switch (k) {
        case 'c': copySelection(v); return true;
        case 'v': pasteInto(st, v); return true;
        case 't': newTab(st, v->session->cwd(), {}); return true;
        case 'w': closeTab(st, v); return true;
        case 'a': v->selectAll(); return true;
        case 'k':
            v->session->scrollback.clear();
            v->session->write("\x0c", 1);
            v->damageAll();
            return true;
        case 'n':
            m3::spawn("material3ui msg terminal");
            return true;
        default: break;
        }
        if (e.sym == XKB_KEY_ISO_Left_Tab || e.sym == XKB_KEY_Tab) return activate(st, st->active - 1), true;
        if (e.sym == XKB_KEY_Page_Up || e.sym == XKB_KEY_Page_Down) return false;
    }
    if (ctrl && !shift) {
        if (e.sym == XKB_KEY_Tab || e.sym == XKB_KEY_Page_Down) return activate(st, (st->active + 1) % static_cast<int>(st->views.size())), true;
        if (e.sym == XKB_KEY_Page_Up) return activate(st, (st->active + static_cast<int>(st->views.size()) - 1) % static_cast<int>(st->views.size())), true;
        if (e.sym == XKB_KEY_equal || e.sym == XKB_KEY_plus || e.sym == XKB_KEY_KP_Add) return v->setFontSize(v->fontSize + 1), true;
        if (e.sym == XKB_KEY_minus || e.sym == XKB_KEY_KP_Subtract) return v->setFontSize(v->fontSize - 1), true;
        if (e.sym == XKB_KEY_0) return v->setFontSize(14), true;
    }
    if (shift && !ctrl && e.sym == XKB_KEY_Insert) return pasteInto(st, v), true;
    if (alt && !ctrl && e.sym >= XKB_KEY_1 && e.sym <= XKB_KEY_9) {
        const int i = static_cast<int>(e.sym - XKB_KEY_1);
        if (i < static_cast<int>(st->views.size())) return activate(st, i), true;
    }
    return false;
}

void newTab(const std::shared_ptr<TermState>& st, std::string dir, const std::vector<std::string>& command) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) dir = paths::home();
    auto session = std::make_shared<Session>(dir, command);
    auto* v = st->stack->add<TermView>(session);
    v->grow = true;
    st->views.push_back(v);
    std::weak_ptr<TermState> weak = st;
    session->titled = [weak] {
        if (auto s = weak.lock(); s && *s->alive && s->rebuild) s->rebuild();
    };
    session->exited = [weak, v] {
        auto s = weak.lock();
        if (!s || !*s->alive) return;
        EventLoop::get().defer([s, v] {
            if (*s->alive) closeTab(s, v);
        });
    };
    v->onShortcut = [weak, v](const KeyEvent& e) {
        auto s = weak.lock();
        return s && *s->alive && shortcut(s, v, e);
    };
    v->onMenu = [weak, v](float x, float y) {
        if (auto s = weak.lock(); s && *s->alive) menuFor(s, v, x, y);
    };
    activate(st, static_cast<int>(st->views.size()) - 1);
}

} // namespace

void terminal(Flex* card, Popup& popup, const std::string& dir, const std::vector<std::string>& command) {
    auto st = std::make_shared<TermState>();
    st->popup = &popup;
    card->setPadding(Insets(8))->setSpacing(6);
    st->tabs = card->add<Flex>(Flex::Direction::Row);
    st->tabs->setSpacing(4)->setAlignItems(Align::Center)->setPadding({4, 0});
    st->stack = card->add<Flex>(Flex::Direction::Column);
    st->stack->setAlignItems(Align::Stretch);
    st->stack->grow = true;
    card->add<TermKeeper>(st);

    st->rebuild = [st] {
        auto* t = st->tabs;
        t->clearChildren();
        for (size_t i = 0; i < st->views.size(); ++i) {
            TermView* v = st->views[i];
            const bool here = static_cast<int>(i) == st->active;
            auto* b = t->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({14, 6, 6, 6})->setSpacing(6);
            b->setBackground(here ? "secondary_container" : "surface_container_high", false);
            b->add<Icon>("terminal", 16)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            b->add<Label>(v->session->title)->setSize(13)->setWeight(here ? 600 : 500)->setMaxWidth(200)->setColor(here ? "on_secondary_container" : "on_surface", false);
            b->givesWay = true;
            b->onClicked = [st, i](uint32_t button) {
                if (button == BTN_LEFT) activate(st, static_cast<int>(i));
            };
            auto* x = iconButton(b, "close", [st, v] { closeTab(st, v); }, 14);
            x->setPadding(Insets(4));
            x->tooltip = "Close tab (Ctrl+Shift+W)";
        }
        iconButton(t, "add", [st] {
            const std::string dir = st->views.empty() ? paths::home() : st->views[static_cast<size_t>(st->active)]->session->cwd();
            newTab(st, dir, {});
        }, 20)->tooltip = "New tab (Ctrl+Shift+T)";
        t->add<Node>()->grow = true;
    };

    popup.onKey = [st](const KeyEvent& e) {
        // Keys the terminal didn't get (it wasn't focused): given to it
        if (st->views.empty()) return false;
        TermView* v = st->views[static_cast<size_t>(st->active)];
        v->focus();
        return v->onKey(e);
    };
    terminalGo = [st](const std::string& d, const std::vector<std::string>& cmd) {
        if (*st->alive) newTab(st, d, cmd);
    };
    newTab(st, dir, command);
}

void terminalClosed() {
    terminalGo = nullptr;
}

// Settings → Terminal
void terminalSettings(Flex* page) {
    auto* g = group(page, "Text");
    sliderRow(g, "format_size", "Text size", "/terminal/fontSize", 14, 8, 28, 1, [](double v) { return std::format("{:.0f} px", v); });
    choiceRow<std::string>(g, "text_select_end", "Cursor", "Until a program asks for another (new tabs)", "/terminal/cursor", "block",
                           {{"block", "Block"}, {"beam", "Beam"}, {"underline", "Underline"}});
    auto* s = group(page, "Sessions");
    choiceRow<int>(s, "history", "Scrollback", "Lines kept above the screen", "/terminal/scrollback", 10000, {{2000, "2,000"}, {10000, "10,000"}, {50000, "50,000"}});
    textRow(s, "terminal", "Shell", "/terminal/shell", "");
    caption(s, "  A command for new tabs (fish, zsh -l, tmux…); empty, your login shell. Enter saves");
    caption(page, "Your terminal's colours come from the wallpaper, as the shell's do");
}

} // namespace m3::shell::panels
