#include "services/Niri.h"

#include "core/Log.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace m3 {

namespace {

int connectUnix(const std::string& path, bool nonblocking) {
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | (nonblocking ? SOCK_NONBLOCK : 0), 0);
    if (fd < 0) return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof addr.sun_path) {
        close(fd);
        return -1;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    return fd;
}

bool writeAll(int fd, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

// A window id from the shell's string
std::optional<uint64_t> windowId(const std::string& id) {
    try {
        size_t used = 0;
        const unsigned long long v = std::stoull(id, &used);
        if (used == id.size()) return v;
    } catch (...) {
    }
    return std::nullopt;
}

} // namespace

Niri& Niri::get() {
    static Niri instance;
    return instance;
}

Niri::Niri() {
    const char* path = std::getenv("NIRI_SOCKET");
    if (!path || !*path) return;
    m_path = path;
    // The state now, for whoever asks before the stream's first lines
    if (auto ws = request("Workspaces")) m_model.setWorkspaces(ws->value("Workspaces", nlohmann::json::array()));
    if (auto wins = request("Windows")) m_model.setWindows(wins->value("Windows", nlohmann::json::array()));
    if (auto outs = request("Outputs")) m_model.setOutputs(outs->value("Outputs", nlohmann::json::object()));
    publish(niri::Model::Workspaces | niri::Model::Windows | niri::Model::Focus);
    connectEvents();
}

std::optional<nlohmann::json> Niri::request(const nlohmann::json& req) const {
    if (m_path.empty()) return std::nullopt;
    const int fd = connectUnix(m_path, false);
    if (fd < 0) return std::nullopt;
    // A blocking request answers at once: niri replies before going on
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string reply;
    if (writeAll(fd, req.dump() + "\n")) {
        shutdown(fd, SHUT_WR);
        char buf[16384];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0) {
            reply.append(buf, static_cast<size_t>(n));
            if (reply.find('\n') != std::string::npos) break;
        }
    }
    close(fd);
    try {
        const auto j = nlohmann::json::parse(reply.substr(0, reply.find('\n')));
        if (j.contains("Ok")) return j["Ok"];
        if (j.contains("Err")) log::warn("niri: {} → {}", req.dump(), j["Err"].dump());
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

void Niri::action(const nlohmann::json& a) {
    request({{"Action", a}});
}

std::string Niri::version() const {
    // {"Version": "26.04 (8ed0da4)"}: a string over IPC (`niri msg -j
    // version` adds the CLI's beside it, as an object)
    if (auto v = request("Version"); v && v->contains("Version")) {
        const auto& ver = (*v)["Version"];
        if (ver.is_string()) return ver.get<std::string>();
        if (ver.is_object()) return ver.value("compositor", "");
    }
    return {};
}

bool Niri::can(Feature feature) const {
    if (!available()) return false;
    switch (feature) {
    case Feature::Reload:
    case Feature::NativeOverview:
    case Feature::DynamicWorkspaces:
    case Feature::WindowCapture: return true;
    // No minimised windows, no submaps, and windows' places aren't the
    // desktop's (they scroll); nor is the pointer's
    default: return false;
    }
}

// ── State ──

void Niri::connectEvents() {
    m_eventFd = connectUnix(m_path, true);
    if (m_eventFd < 0 || !writeAll(m_eventFd, "\"EventStream\"\n")) {
        if (m_eventFd >= 0) close(m_eventFd);
        m_eventFd = -1;
        log::warn("cannot open niri's event stream; retrying");
        m_reconnect = EventLoop::get().addTimer(2000, [this] {
            m_reconnect = 0;
            connectEvents();
        });
        return;
    }
    m_handled = false;
    m_buffer.clear();
    EventLoop::get().add(this);
}

void Niri::dispatch(short) {
    char buf[65536];
    for (;;) {
        const ssize_t n = ::read(m_eventFd, buf, sizeof buf);
        if (n > 0) {
            m_buffer.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            // niri restarted (or quit): reconnect when it's back
            EventLoop::get().remove(this);
            close(m_eventFd);
            m_eventFd = -1;
            m_reconnect = EventLoop::get().addTimer(1000, [this] {
                m_reconnect = 0;
                connectEvents();
            });
        }
        break;
    }
    size_t pos;
    while ((pos = m_buffer.find('\n')) != std::string::npos) {
        const std::string line = m_buffer.substr(0, pos);
        m_buffer.erase(0, pos + 1);
        try {
            const auto j = nlohmann::json::parse(line);
            if (!m_handled) {
                m_handled = true; // {"Ok":"Handled"}
                continue;
            }
            handle(j);
        } catch (const std::exception& e) {
            log::warn("niri: an event it couldn't read: {}", e.what());
        }
    }
}

void Niri::handle(const nlohmann::json& event) {
    // Window screenshots: written now
    if (const auto it = event.find("ScreenshotCaptured"); it != event.end()) {
        std::string path;
        if (const auto p = it->find("path"); p != it->end() && p->is_string()) path = p->get<std::string>();
        if (const auto c = m_captures.find(path); c != m_captures.end()) {
            auto [done, timer] = std::move(c->second);
            m_captures.erase(c);
            EventLoop::get().cancel(timer);
            done(true);
        }
        return;
    }
    if (const auto it = event.find("ConfigLoaded"); it != event.end()) {
        configLoaded.emit(it->value("failed", false));
        refreshOutputs(); // monitors may be set differently now
        publish(niri::Model::Workspaces);
        return;
    }
    const unsigned changes = m_model.apply(event);
    // Outputs come and go with their workspaces; niri has no event of their own
    if (event.contains("WorkspacesChanged")) refreshOutputs();
    if (changes & niri::Model::Layouts) layoutsChanged.emit();
    if (!(changes & (niri::Model::Workspaces | niri::Model::Windows | niri::Model::Focus))) return;
    // A burst (a workspace switch is several events): published once
    m_pending |= changes;
    EventLoop::get().debounce(m_publish, 16, [this] {
        const unsigned pending = m_pending;
        m_pending = 0;
        publish(pending);
    });
}

void Niri::refreshOutputs() {
    if (auto outs = request("Outputs")) m_model.setOutputs(outs->value("Outputs", nlohmann::json::object()));
    outputsChanged.emit();
}

void Niri::outputAction(const std::string& output, const nlohmann::json& a) {
    request({{"Output", {{"output", output}, {"action", a}}}});
    refreshOutputs();
    publish(niri::Model::Workspaces);
}

void Niri::publish(unsigned changes) {
    m_workspaces = m_model.shellWorkspaces();
    m_outputs = m_model.shellOutputs();
    if (changes & (niri::Model::Workspaces | niri::Model::Windows)) workspacesChanged.emit();
    if (changes & niri::Model::Windows) windowsChanged.emit();
    const auto* focused = m_model.focusedWindow();
    const std::string title = focused ? focused->title : "", app = focused ? focused->appId : "";
    if (title != m_activeTitle || app != m_activeAppId) {
        m_activeTitle = title;
        m_activeAppId = app;
        activeWindowChanged.emit();
    }
}

std::vector<Compositor::Workspace> Niri::workspacesOn(const std::string& output) const {
    const std::string on = output.empty() ? m_model.focusedOutput() : output;
    std::vector<Workspace> out;
    for (const auto& w : m_workspaces)
        if (w.output == on) out.push_back(w);
    return out;
}

std::vector<Compositor::Window> Niri::windows(bool) const {
    return m_model.shellWindows();
}

// ── Actions ──

void Niri::focusWorkspace(int number, const std::string& output) {
    // That output's workspace by id (an index is the focused output's)
    if (const auto* w = m_model.workspaceAt(number, output))
        return action({{"FocusWorkspace", {{"reference", {{"Id", w->id}}}}}});
    action({{"FocusWorkspace", {{"reference", {{"Index", number}}}}}});
}

void Niri::focusWorkspaceRelative(int delta) {
    for (int i = 0; i < std::abs(delta); ++i) action({{delta > 0 ? "FocusWorkspaceDown" : "FocusWorkspaceUp", nlohmann::json::object()}});
}

void Niri::focusWindow(const std::string& id) {
    if (const auto w = windowId(id)) action({{"FocusWindow", {{"id", *w}}}});
}

void Niri::closeWindow(const std::string& id) {
    if (const auto w = windowId(id)) action({{"CloseWindow", {{"id", *w}}}});
}

void Niri::setScreens(bool on) {
    action({{on ? "PowerOnMonitors" : "PowerOffMonitors", nlohmann::json::object()}});
}

bool Niri::quit() {
    if (!available()) return false;
    action({{"Quit", {{"skip_confirmation", true}}}});
    return true;
}

void Niri::reload() {
    action({{"LoadConfigFile", {{"path", nullptr}}}});
}

void Niri::toggleOverview() {
    action({{"ToggleOverview", nlohmann::json::object()}});
}

void Niri::captureWindow(const std::string& path, std::function<void(bool)> done) {
    // niri writes it (and copies it), then says so in the stream
    const auto timer = EventLoop::get().addTimer(5000, [this, path] {
        if (const auto c = m_captures.find(path); c != m_captures.end()) {
            auto done = std::move(c->second.first);
            m_captures.erase(c);
            done(false);
        }
    });
    m_captures[path] = {std::move(done), timer};
    action({{"ScreenshotWindow", {{"id", nullptr}, {"write_to_disk", true}, {"show_pointer", false}, {"path", path}}}});
}

} // namespace m3
