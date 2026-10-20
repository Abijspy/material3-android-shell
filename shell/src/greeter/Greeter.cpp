#include "greeter/Greeter.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "render/Renderer.h"
#include "services/Auth.h"
#include "services/Session.h"
#include "services/Wallpaper.h"
#include "shell/LockView.h"
#include "wayland/Display.h"
#include "wayland/LayerSurface.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3::greeter {

namespace {

std::vector<shell::LockView::Choice> loginUsers() {
    std::vector<shell::LockView::Choice> out;
    setpwent();
    while (passwd* pw = getpwent()) {
        const std::string shell = pw->pw_shell ? pw->pw_shell : "";
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || shell.ends_with("nologin") || shell.ends_with("false")) continue;
        out.push_back({pw->pw_name, auth::realName(pw->pw_name)});
    }
    endpwent();
    return out;
}

std::vector<shell::LockView::Choice> sessions() {
    std::vector<shell::LockView::Choice> out;
    for (const char* dir : {"/usr/share/wayland-sessions", "/usr/local/share/wayland-sessions"}) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".desktop") continue;
            std::ifstream in(e.path());
            std::string line, name, exec;
            bool hidden = false;
            while (std::getline(in, line)) {
                if (line.starts_with("Name=") && name.empty()) name = line.substr(5);
                else if (line.starts_with("Exec=")) exec = line.substr(5);
                else if ((line == "Hidden=true") || (line == "NoDisplay=true")) hidden = true;
            }
            if (!hidden && !exec.empty()) out.push_back({exec, name.empty() ? e.path().stem().string() : name});
        }
    }
    // Hyprland first: it is what this shell is made for
    std::ranges::stable_sort(out, [](const auto& a, const auto& b) {
        return (a.label.find("Hyprland") != std::string::npos) > (b.label.find("Hyprland") != std::string::npos);
    });
    return out;
}

std::string memoryFile() {
    return paths::stateDir() + "/greeter.json";
}

std::vector<std::string> splitCommand(const std::string& cmd) {
    std::vector<std::string> out;
    std::istringstream in(cmd);
    std::string word;
    while (in >> std::quoted(word)) out.push_back(word);
    return out;
}

} // namespace

Greeter::Greeter() = default;

Greeter::~Greeter() {
    if (m_fd >= 0) close(m_fd);
}

bool Greeter::start() {
    auto& d = wl::Display::get();
    if (!d.connect() || !Renderer::get().init(d.display())) return false;

    // Last user and session, if remembered
    try {
        std::ifstream in(memoryFile());
        if (in) {
            const auto j = nlohmann::json::parse(in);
            m_user = j.value("user", "");
            m_session = j.value("session", "");
        }
    } catch (...) {
    }

    if (const char* sock = std::getenv("GREETD_SOCK")) {
        m_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, sock, sizeof addr.sun_path - 1);
        if (::connect(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
            log::error("cannot reach greetd at {}: {}", sock, std::strerror(errno));
            close(m_fd);
            m_fd = -1;
        } else {
            EventLoop::get().add(this);
        }
    } else {
        log::warn("GREETD_SOCK is not set: preview mode, logins are disabled");
    }

    d.onOutputReady = [this](wl::Output* o) { addOutput(o); };
    d.onOutputRemoved = [this](wl::Output* o) {
        m_views.erase(o);
        m_surfaces.erase(o);
    };
    for (auto& o : d.outputs())
        if (o->ready) addOutput(o.get());
    beginSession();
    return true;
}

void Greeter::addOutput(wl::Output* output) {
    wl::LayerSurface::Options o;
    o.ns = "material3ui-greeter";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1; // cover everything
    o.keyboard = wl::LayerSurface::Keyboard::OnDemand;
    auto surface = std::make_unique<wl::LayerSurface>(output, o);
    surface->inlineBlur = true; // the widgets blur its own wallpaper

    shell::LockView::Options opts;
    opts.greeter = true;
    opts.users = loginUsers();
    opts.sessions = sessions();
    opts.wallpaper = Wallpaper::get().lockPath();
    if (m_user.empty() && !opts.users.empty()) m_user = opts.users.front().id;
    opts.user = m_user;
    // Remembered session first
    if (!m_session.empty())
        std::ranges::stable_sort(opts.sessions, [this](auto& a, auto&) { return a.id == m_session; });
    if (m_session.empty() && !opts.sessions.empty()) m_session = opts.sessions.front().id;
    auto view = std::make_unique<shell::LockView>(opts);
    shell::LockView* raw = view.get();
    raw->onSubmit = [this, raw](const std::string& answer) {
        m_session = raw->session(); // the screen the user typed on
        submit(answer);
    };
    raw->onUserChanged = [this](const std::string& user) {
        m_user = user;
        beginSession();
    };
    raw->onPower = [](const std::string& action) {
        if (action == "suspend") Session::get().suspend();
        else if (action == "reboot") Session::get().reboot();
        else if (action == "poweroff") Session::get().powerOff();
    };
    if (m_fd < 0) raw->setInfo("Preview — not connected to greetd");
    surface->setRoot(std::move(view));
    surface->setFocus(raw->field());
    m_views[output] = raw;
    m_surfaces[output] = std::move(surface);
}

void Greeter::forEachView(const std::function<void(shell::LockView*)>& fn) {
    for (auto& [_, v] : m_views) fn(v);
}

void Greeter::send(const nlohmann::json& request) {
    if (m_fd < 0) return;
    const std::string body = request.dump();
    const uint32_t len = static_cast<uint32_t>(body.size()); // native byte order, as greetd expects
    std::string frame(reinterpret_cast<const char*>(&len), sizeof len);
    frame += body;
    if (::send(m_fd, frame.data(), frame.size(), MSG_NOSIGNAL) < 0) {
        log::error("greetd: {}", std::strerror(errno));
        return;
    }
    m_expect.push_back(request.value("type", ""));
}

void Greeter::beginSession() {
    if (m_fd < 0 || m_user.empty()) return;
    if (m_inSession) send({{"type", "cancel_session"}});
    m_inSession = true;
    m_prompting = false;
    send({{"type", "create_session"}, {"username", m_user}});
}

void Greeter::submit(const std::string& answer) {
    if (m_fd < 0) {
        forEachView([](shell::LockView* v) { v->fail("Preview mode: not connected to greetd"); });
        return;
    }
    if (!m_prompting) return;
    m_prompting = false;
    forEachView([](shell::LockView* v) { v->setBusy(true); });
    send({{"type", "post_auth_message_response"}, {"response", answer}});
}

void Greeter::dispatch(short) {
    char buf[4096];
    for (;;) {
        const ssize_t n = ::recv(m_fd, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) {
            m_buffer.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            log::error("greetd closed the connection");
            EventLoop::get().quit(1);
        }
        break;
    }
    while (m_buffer.size() >= 4) {
        uint32_t len;
        std::memcpy(&len, m_buffer.data(), 4);
        if (m_buffer.size() < 4 + len) break;
        const std::string body = m_buffer.substr(4, len);
        m_buffer.erase(0, 4 + len);
        try {
            handle(nlohmann::json::parse(body));
        } catch (const std::exception& e) {
            log::error("greetd: bad reply: {}", e.what());
        }
    }
}

void Greeter::handle(const nlohmann::json& r) {
    const std::string forRequest = m_expect.empty() ? "" : m_expect.front();
    if (!m_expect.empty()) m_expect.pop_front();
    const std::string type = r.value("type", "");
    if (forRequest == "cancel_session") return; // its outcome changes nothing

    if (forRequest == "start_session") {
        if (type == "success") {
            // greetd starts the session once we exit
            remember();
            EventLoop::get().quit(0);
        } else {
            forEachView([&](shell::LockView* v) { v->fail(r.value("description", "Could not start the session")); });
            beginSession();
        }
        return;
    }

    // Replies to create_session and post_auth_message_response
    if (type == "auth_message") {
        const std::string kind = r.value("auth_message_type", "");
        const std::string text = r.value("auth_message", "");
        if (kind == "info" || kind == "error") {
            // Show it and carry on (fingerprint hints, warnings)
            forEachView([&](shell::LockView* v) { v->setInfo(text); });
            send({{"type", "post_auth_message_response"}, {"response", nullptr}});
            return;
        }
        m_prompting = true;
        std::string prompt = text;
        while (!prompt.empty() && (prompt.back() == ' ' || prompt.back() == ':')) prompt.pop_back();
        forEachView([&](shell::LockView* v) { v->setPrompt(prompt.empty() ? "Password" : prompt, kind == "secret"); });
    } else if (type == "success") {
        forEachView([](shell::LockView* v) { v->setInfo("Starting…"); });
        send({{"type", "start_session"}, {"cmd", splitCommand(m_session)}, {"env", nlohmann::json::array()}});
    } else if (type == "error") {
        const bool authError = r.value("error_type", "") == "auth_error";
        const std::string msg = authError ? "Wrong password" : r.value("description", "Login failed");
        forEachView([&](shell::LockView* v) { v->fail(msg); });
        // A failed session is over; start a fresh one for the next try
        send({{"type", "cancel_session"}});
        m_inSession = false;
        beginSession();
    }
}

void Greeter::remember() const {
    std::error_code ec;
    fs::create_directories(paths::stateDir(), ec);
    std::ofstream(memoryFile()) << nlohmann::json{{"user", m_user}, {"session", m_session}}.dump();
}

} // namespace m3::greeter
