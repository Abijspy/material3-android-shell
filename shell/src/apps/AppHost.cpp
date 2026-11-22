#include "apps/AppHost.h"

#include "build_config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/Compositor.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "wayland/Display.h"

#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
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

void raise(const std::string& appId) {
    auto& c = Compositor::get();
    for (const auto& w : c.windows(true))
        if (w.appId == appId) c.focusWindow(w.id);
}

} // namespace

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

    shell::Popup::Options opts;
    opts.name = app.id;
    opts.window = true;
    opts.appId = app.appId;
    opts.title = app.title;
    opts.icon = app.icon;
    opts.windowWidth = app.width;
    opts.windowHeight = app.height;
    auto window = std::make_unique<shell::Popup>(nullptr, opts, [&app, args](ui::Flex* card, shell::Popup& p) { app.build(card, p, args); });
    window->onClosed = [&app] {
        if (app.closed) app.closed();
        EventLoop::get().defer([] { EventLoop::get().quit(0); });
    };
    Listener listener(socketPath(app.id), [&app](std::vector<std::string> more) {
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
