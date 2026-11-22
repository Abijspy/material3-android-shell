#include "apps/AppHost.h"

#include "build_config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "wayland/Display.h"
#include "render/Images.h"
#include "shell/PanelKit.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3::apps {

namespace {

std::string socketPath(const std::string& id) {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    const std::string dir = rt && *rt ? rt : "/tmp";
    const std::string inst = paths::instance();
    return std::format("{}/material3ui-{}{}.sock", dir, id, inst.empty() ? "" : "-" + inst);
}

sockaddr_un address(const std::string& path) {
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
    return a;
}

// Paths as given, made absolute from here (the copy that's running is
// elsewhere); a command after -e left alone
std::vector<std::string> absolute(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    bool command = false;
    for (const auto& a : args) {
        if (a == "-e" || a == "--") command = true;
        std::error_code ec;
        if (!command && !a.empty() && a[0] != '-' && a.find("://") == std::string::npos && a[0] != '/' && fs::exists(a, ec))
            out.push_back(fs::absolute(a, ec).lexically_normal().string());
        else if (!command && a.starts_with("file://"))
            out.push_back(a.substr(7));
        else
            out.push_back(a);
    }
    return out;
}

// Hands the arguments to a copy already running: true when there was one
bool handOver(const std::string& id, const std::vector<std::string>& args) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    auto a = address(socketPath(id));
    if (connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        close(fd);
        return false;
    }
    std::string msg;
    for (const auto& s : args) msg += s + '\0';
    const char* p = msg.data();
    size_t left = msg.size();
    while (left > 0) {
        const ssize_t n = write(fd, p, left);
        if (n <= 0) break;
        p += n, left -= static_cast<size_t>(n);
    }
    close(fd);
    return true;
}

// The program's socket: later launches' arguments arrive here
class Listener : public EventSource {
public:
    Listener(std::string path, std::function<void(std::vector<std::string>)> got) : m_path(std::move(path)), m_got(std::move(got)) {
        unlink(m_path.c_str());
        m_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        auto a = address(m_path);
        if (bind(m_fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(m_fd, 4) != 0) {
            log::warn("{}: {}", m_path, std::strerror(errno));
            close(m_fd);
            m_fd = -1;
        }
    }
    ~Listener() override {
        if (m_fd >= 0) close(m_fd), unlink(m_path.c_str());
    }
    int fd() const override { return m_fd; }
    void dispatch(short) override {
        while (true) {
            const int c = accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) return;
            // Small and local: read to the end at once
            std::string data;
            char buf[4096];
            timeval tv{1, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            for (ssize_t n; (n = read(c, buf, sizeof buf)) > 0;) data.append(buf, static_cast<size_t>(n));
            close(c);
            std::vector<std::string> args;
            size_t at = 0;
            for (size_t z; (z = data.find('\0', at)) != std::string::npos; at = z + 1) args.push_back(data.substr(at, z - at));
            m_got(std::move(args));
        }
    }

private:
    std::string m_path;
    std::function<void(std::vector<std::string>)> m_got;
    int m_fd = -1;
};

// SIGINT and SIGTERM end it through the loop; children are reaped there
class Signals : public EventSource {
public:
    Signals() {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGCHLD);
        sigprocmask(SIG_BLOCK, &set, nullptr);
        m_fd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
        signal(SIGPIPE, SIG_IGN);
    }
    ~Signals() override { close(m_fd); }
    int fd() const override { return m_fd; }
    void dispatch(short) override {
        signalfd_siginfo info;
        while (read(m_fd, &info, sizeof info) == sizeof info) {
            if (info.ssi_signo == SIGCHLD) reapChildren();
            else EventLoop::get().quit(0);
        }
    }

private:
    int m_fd = -1;
};

// Its children one over another, each the whole of it: the app, and the
// Settings and About sheet over it
class Stack : public ui::Node {
protected:
    SizeF doMeasure(SizeF available) override {
        for (auto& c : children())
            if (c->shown()) c->measure(available);
        return available;
    }
    void doArrange() override {
        for (auto& c : children())
            if (c->shown()) {
                c->measure({rect().w, rect().h});
                c->arrange(rect());
            }
    }
};

// The dimmed backdrop behind the sheet: a click on it closes the sheet
class Scrim : public ui::Flex {
public:
    Scrim() : Flex(Direction::Column) {}
    std::function<void()> onDismiss;
    bool interactive() const override { return true; }
    void onClick(uint32_t button) override {
        if (button == BTN_LEFT && onDismiss) onDismiss();
    }
};

struct Sheet {
    shell::Popup* popup = nullptr;
    Scrim* scrim = nullptr;
    ui::Flex *body = nullptr, *tabs = nullptr;
    int tab = 0;
    std::function<void(int)> show;
};

// The About page: the app, its version, what it does, and where it's from
void aboutPage(ui::Flex* page, const App& app) {
    using namespace ui;
    auto* head = page->add<Flex>(Flex::Direction::Row);
    head->setSpacing(18)->setAlignItems(Align::Center)->setPadding({4, 8});
    const std::string icon = app.appId;
    if (!Images::get().findIcon(icon, 72).empty()) {
        head->add<ImageView>(72, [icon](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().icon(icon, static_cast<int>(std::lround(72 * scale)));
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
    } else {
        // Not installed (a build directory): its symbol in a tonal circle
        auto* dot = head->add<Flex>(Flex::Direction::Row);
        dot->fixedWidth = dot->fixedHeight = 72;
        dot->setRadius(Radii(36))->setBackground("primary_container", false)->setAlignItems(Align::Center)->setJustify(Align::Center);
        dot->add<Icon>(app.icon, 38)->setFill(true)->setColor("on_primary_container", false);
    }
    auto* names = head->add<Flex>(Flex::Direction::Column);
    names->setAlignItems(Align::Start)->setSpacing(3);
    names->grow = true;
    names->add<Label>(app.title)->setSize(24)->setWeight(500);
    names->add<Label>(std::format("Version {}  ·  part of Material3UI", M3_VERSION))->setSize(13)->setColor("on_surface_variant", false);
    names->add<Label>(app.appId)->setSize(12)->setColor("outline", false)->setFamily(Theme::get().monoFamily());
    if (!app.about.summary.empty()) page->add<Label>(app.about.summary)->setSize(15)->setMaxWidth(520)->setMaxLines(4);
    if (!app.about.features.empty()) {
        auto* g = shell::panels::group(page, "What it does");
        for (const auto& f : app.about.features) {
            auto* r = g->add<Flex>(Flex::Direction::Row);
            r->setSpacing(12)->setAlignItems(Align::Start)->setPadding({16, 8});
            r->add<Icon>("check_circle", 18)->setColor("primary", false);
            r->add<Label>(f)->setSize(14)->setMaxWidth(470)->setMaxLines(3);
        }
    }
    auto* g = shell::panels::group(page, "About");
    auto line = [&](const char* ic, const std::string& k, const std::string& v) {
        auto r = shell::panels::row(g, ic, k, v);
        (void)r;
    };
    line("deployed_code", "Package", "material3ui-" + app.id + " (pacman)");
    if (!app.about.builtWith.empty()) line("construction", "Built on", app.about.builtWith);
    line("gavel", "License", "MIT");
    line("person", "By", "Abishek (Abijspy) and Material3UI's contributors");
    auto links = page->add<Flex>(Flex::Direction::Row);
    links->setSpacing(8)->setPadding({4, 6});
    links->wrap = true;
    shell::panels::pillButton(links, "Website", false, [] {
        m3::run({"xdg-open", "https://github.com/Abijspy/material3-android-shell"}, nullptr);
    }, "open_in_new");
    shell::panels::pillButton(links, "Report a problem", false, [] {
        m3::run({"xdg-open", "https://github.com/Abijspy/material3-android-shell/issues"}, nullptr);
    }, "bug_report");
    shell::panels::pillButton(links, "What's new", false, [] { askShell({"popup", "whatsnew"}); }, "new_releases");
    shell::panels::pillButton(links, "Check for updates", false, [] { askShell({"settings", "packages"}); }, "system_update");
}

void raise(const std::string& appId) {
    auto& c = Compositor::get();
    for (const auto& w : c.windows(true))
        if (w.appId == appId) c.focusWindow(w.id);
}

} // namespace

Builder withSheet(const App& app, shell::Popup::Options& opts, Builder inner) {
    auto sheet = std::make_shared<Sheet>();
    opts.titleButtons.push_back({"settings", "Settings and about", [sheet] {
                                     if (sheet->show) sheet->show(sheet->scrim && sheet->scrim->visible() ? -1 : 0);
                                 }});
    return [&app, sheet, inner](ui::Flex* card, shell::Popup& p) {
        using namespace ui;
        auto* stack = card->add<Stack>();
        stack->grow = true;
        auto* content = stack->add<Flex>(Flex::Direction::Column);
        content->setAlignItems(Align::Stretch);
        inner(content, p);
        sheet->popup = &p;

        // The sheet: Settings and About, over the app
        sheet->scrim = stack->add<Scrim>();
        sheet->scrim->setBackground(ColorRef("scrim", 0.32f), false)->setAlignItems(Align::Center)->setJustify(Align::Center)->setPadding(Insets(24));
        sheet->scrim->setVisible(false);
        auto* sc = sheet->scrim->add<Flex>(Flex::Direction::Column);
        sc->setBackground("surface_container", false)->setRadius(Radii(28))->setPadding({20, 18, 20, 18})->setSpacing(12)->setAlignItems(Align::Stretch);
        sc->fixedWidth = 620;
        sc->fixedHeight = std::min(640, app.height - 110);
        auto* bar = sc->add<Flex>(Flex::Direction::Row);
        bar->setSpacing(8)->setAlignItems(Align::Center);
        sheet->tabs = bar->add<Flex>(Flex::Direction::Row);
        sheet->tabs->setSpacing(4);
        sheet->tabs->grow = true;
        shell::kit::iconButton(bar, "close", [sheet] { sheet->show(-1); }, 20)->tooltip = "Close (Escape)";
        auto* scroll = sc->add<ScrollView>();
        scroll->maxHeight = 1e6f;
        scroll->grow = true;
        scroll->blurEdges = true;
        sheet->body = scroll->add<Flex>(Flex::Direction::Column);
        sheet->body->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({0, 4, 8, 4});
        sheet->scrim->onDismiss = [sheet] { sheet->show(-1); };
        // Clicks on the card stay on it
        sc->setBorder(0, ColorRef::none());
        sheet->show = [sheet, &app](int tab) {
            if (tab < 0) {
                sheet->scrim->setVisible(false);
                sheet->body->clearChildren();
                return;
            }
            const bool has = app.settings || app.settingsIn;
            if (!has) tab = 1;
            sheet->tab = tab;
            sheet->scrim->setVisible(true);
            sheet->tabs->clearChildren();
            const char* names[] = {"Settings", "About"};
            const char* icons[] = {"tune", "info"};
            for (int i = 0; i < 2; ++i) {
                if (i == 0 && !has) continue;
                auto* b = sheet->tabs->add<Button>(Flex::Direction::Row);
                ui::pill(b)->setPadding({14, 8})->setSpacing(8);
                const bool here = i == tab;
                b->setBackground(here ? "secondary_container" : "surface_container_high", false);
                b->add<Icon>(icons[i], 18)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
                b->add<Label>(names[i])->setSize(14)->setWeight(here ? 600 : 500)->setColor(here ? "on_secondary_container" : "on_surface", false);
                b->onClicked = [sheet, i](uint32_t btn) {
                    if (btn == BTN_LEFT) EventLoop::get().defer([sheet, i] {
                        if (sheet->show) sheet->show(i);
                    });
                };
            }
            sheet->body->clearChildren();
            if (tab == 0) {
                if (app.settingsIn && sheet->popup) app.settingsIn(sheet->body, *sheet->popup);
                else if (app.settings) app.settings(sheet->body);
                shell::kit::caption(sheet->body, "Saved as you change them (in ~/.config/material3ui/config.json)");
            } else {
                aboutPage(sheet->body, app);
            }
        };
        // Escape closes the sheet before anything else; Ctrl+, opens it
        auto appKeys = p.onKey;
        p.onKey = [sheet, appKeys](const KeyEvent& e) {
            if (sheet->scrim && sheet->scrim->visible()) {
                if (e.sym == XKB_KEY_Escape) return sheet->show(-1), true;
                return false;
            }
            if (e.ctrl() && e.sym == XKB_KEY_comma) return sheet->show(0), true;
            return appKeys && appKeys(e);
        };
    };
}

void askShell(const std::vector<std::string>& args) {
    const std::string shell = ownCommand("material3ui");
    if (shell.empty()) return;
    std::vector<std::string> argv = {shell, "msg"};
    argv.insert(argv.end(), args.begin(), args.end());
    m3::run(argv, nullptr);
}

int run(const App& app, int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && (args[0] == "--version" || args[0] == "-v")) {
        std::cout << "material3ui-" << app.id << " " << M3_VERSION << "\n";
        return 0;
    }
    if (!args.empty() && (args[0] == "--help" || args[0] == "-h")) {
        std::cout << "material3ui-" << app.id << " " << M3_VERSION << ": " << app.title << ", Material3UI's own\n\n"
                  << (app.usage.empty() ? "usage: material3ui-" + app.id + " [file…]\n" : app.usage);
        return 0;
    }
    args = absolute(args);
    if (handOver(app.id, args)) return 0;

    Signals signals;
    EventLoop::get().add(&signals);
    auto& display = wl::Display::get();
    if (!display.connect()) {
        std::cerr << "material3ui-" << app.id << ": no Wayland display\n";
        return 1;
    }
    if (!Renderer::get().init(display.display())) return 1;

    // Other apps and the shell's own parts, through the shell
    shell::panels::open = [](const std::string& what) {
        const auto sp = what.find(' ');
        const std::string name = what.substr(0, sp), arg = sp == std::string::npos ? "" : what.substr(sp + 1);
        // Another app: its own program, straight (the shell says when one
        // isn't installed)
        static const std::map<std::string, std::string> programs = {{"files", "files"}, {"viewer", "viewer"}, {"player", "player"}, {"tasks", "tasks"},
                                                                     {"terminal", "terminal"}, {"notes", "notes"}, {"pdf", "pdf"}, {"text", "text"}, {"music", "music"}, {"disks", "disks"},
                                                                     {"camera", "camera"}, {"backups", "backups"}, {"recorder", "recorder"}, {"calculator", "calculator"},
                                                                     {"fonts", "fonts"}, {"imagewriter", "imagewriter"}};
        if (auto it = programs.find(name); it != programs.end()) {
            const std::string program = ownCommand("material3ui-" + it->second);
            if (!program.empty()) {
                std::vector<std::string> argv = {program};
                if (!arg.empty()) argv.push_back(arg);
                spawnArgs(argv);
                return;
            }
        }
        std::vector<std::string> cmd;
        if (name == "files") cmd = {"files"};
        else if (name == "viewer") cmd = {"view"};
        else if (name == "player") cmd = {"play"};
        else if (name == "imageeditor") cmd = {"edit"};
        else if (name == "wallpaper") cmd = {"wallpaper", "set"};
        else if (name == "tasks" || name == "terminal" || name == "settings") cmd = {name};
        else cmd = {"popup", name};
        if (!arg.empty()) cmd.push_back(arg);
        askShell(cmd);
    };

    // Files it's asked to open go in the recent list (the dock's menu,
    // Spotlight, other apps' "Recent")
    auto remember = [&app](const std::vector<std::string>& files) {
        if (app.id == "terminal") return; // its arguments are a command
        std::error_code ec;
        for (const auto& f : files)
            if (!f.starts_with("-") && std::filesystem::is_regular_file(f, ec)) search::recordRecent(f, app.title, app.appId);
    };
    remember(args);

    shell::Popup::Options opts;
    opts.name = app.id;
    opts.window = true;
    opts.appId = app.appId;
    opts.title = app.title;
    opts.icon = app.icon;
    opts.windowWidth = app.width;
    opts.windowHeight = app.height;
    auto window = std::make_unique<shell::Popup>(nullptr, opts, withSheet(app, opts, [&app, args, remember](ui::Flex* card, shell::Popup& p) {
        if (app.opensDrops && app.go) {
            card->onDropFiles = [&app, remember](const std::vector<std::string>& paths) {
                remember(paths);
                for (const auto& f : paths) app.go({f});
                raise(app.appId);
            };
            card->onDropHover = [card](bool over) { card->setOpacity(over ? 0.82f : 1.0f); };
        }
        app.build(card, p, args);
    }));
    window->onClosed = [&app] {
        if (app.closed) app.closed();
        EventLoop::get().defer([] { EventLoop::get().quit(0); });
    };
    Listener listener(socketPath(app.id), [&app, remember](std::vector<std::string> more) {
        remember(more);
        if (app.go) app.go(more);
        raise(app.appId);
    });
    if (listener.fd() >= 0) EventLoop::get().add(&listener);

    const int code = EventLoop::get().run();
    if (listener.fd() >= 0) EventLoop::get().remove(&listener);
    window.reset();
    EventLoop::get().remove(&signals);
    return code;
}

} // namespace m3::apps
