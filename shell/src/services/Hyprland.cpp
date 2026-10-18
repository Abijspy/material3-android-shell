#include "services/Hyprland.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <set>
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

// Events after which the workspace list or window counts may differ
const std::set<std::string> kWorkspaceEvents = {
    "workspace", "workspacev2", "focusedmon", "focusedmonv2", "createworkspace", "createworkspacev2",
    "destroyworkspace", "destroyworkspacev2", "moveworkspace", "moveworkspacev2", "renameworkspace",
    "openwindow", "closewindow", "movewindow", "movewindowv2", "monitoradded", "monitoraddedv2",
    "monitorremoved", "monitorremovedv2", "configreloaded",
};

const std::set<std::string> kWindowEvents = {
    "activewindow", "activewindowv2", "windowtitle", "windowtitlev2", "closewindow", "workspace", "workspacev2",
};

} // namespace

Hyprland& Hyprland::get() {
    static Hyprland instance;
    return instance;
}

Hyprland::Hyprland() {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig || !*sig) return;
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    m_dir = std::format("{}/hypr/{}", runtime ? runtime : "/tmp", sig);

    // Lua configs (0.55+) take Lua expressions for dispatch
    if (auto status = requestJson("status"); status && status->contains("configProvider"))
        m_lua = status->at("configProvider").get<std::string>() == "lua";
    else
        m_lua = std::filesystem::exists(paths::home() + "/.config/hypr/hyprland.lua");

    refreshWorkspaces();
    refreshActiveWindow();
    connectEvents();
}

std::string Hyprland::request(const std::string& command) const {
    if (m_dir.empty()) return {};
    const int fd = connectUnix(m_dir + "/.socket.sock", false);
    if (fd < 0) return {};
    std::string out;
    if (::write(fd, command.data(), command.size()) == static_cast<ssize_t>(command.size())) {
        char buf[8192];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return out;
}

std::optional<nlohmann::json> Hyprland::requestJson(const std::string& command) const {
    const auto reply = request("j/" + command);
    if (reply.empty()) return std::nullopt;
    try {
        return nlohmann::json::parse(reply);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void Hyprland::dispatch(const std::string& lua, const std::string& legacy) {
    request("dispatch " + (m_lua ? lua : legacy));
}

void Hyprland::focusWorkspace(int id) {
    request(m_lua ? std::format("dispatch hl.dsp.focus({{ workspace = \"{}\" }})", id)
                  : std::format("dispatch workspace {}", id));
}

void Hyprland::focusWorkspaceRelative(int delta) {
    request(m_lua ? std::format("dispatch hl.dsp.focus({{ workspace = \"m{:+}\" }})", delta)
                  : std::format("dispatch workspace m{:+}", delta));
}

int Hyprland::activeWorkspace(const std::string& monitor) const {
    for (const auto& m : m_monitors)
        if (m.name == monitor || (monitor.empty() && m.focused)) return m.activeWorkspace;
    return 0;
}

void Hyprland::connectEvents() {
    m_eventFd = connectUnix(m_dir + "/.socket2.sock", true);
    if (m_eventFd < 0) {
        log::warn("cannot open the Hyprland event socket; retrying");
        m_reconnect = EventLoop::get().addTimer(2000, [this] {
            m_reconnect = 0;
            connectEvents();
        });
        return;
    }
    EventLoop::get().add(this);
}

void Hyprland::dispatch(short) {
    char buf[8192];
    for (;;) {
        const ssize_t n = ::read(m_eventFd, buf, sizeof buf);
        if (n > 0) {
            m_buffer.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Hyprland restarted or crashed: reconnect when it is back
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
        const auto sep = line.find(">>");
        if (sep == std::string::npos) continue;
        handleEvent(line.substr(0, sep), line.substr(sep + 2));
    }
}

void Hyprland::handleEvent(const std::string& name, const std::string& data) {
    event.emit(name, data);
    // Events arrive in bursts (a workspace switch is several); refresh once
    if (kWorkspaceEvents.contains(name)) EventLoop::get().debounce(m_workspaceDebounce, 16, [this] { refreshWorkspaces(); });
    if (kWindowEvents.contains(name)) EventLoop::get().debounce(m_windowDebounce, 16, [this] { refreshActiveWindow(); });
}

void Hyprland::refreshWorkspaces() {
    std::vector<Workspace> workspaces;
    if (auto j = requestJson("workspaces"); j && j->is_array()) {
        for (const auto& w : *j) {
            Workspace ws;
            ws.id = w.value("id", 0);
            ws.name = w.value("name", "");
            ws.monitor = w.value("monitor", "");
            ws.windows = w.value("windows", 0);
            if (ws.id > 0) workspaces.push_back(ws); // skip special workspaces
        }
    }
    std::ranges::sort(workspaces, {}, &Workspace::id);

    std::vector<Monitor> monitors;
    if (auto j = requestJson("monitors"); j && j->is_array()) {
        for (const auto& m : *j) {
            Monitor mon;
            mon.name = m.value("name", "");
            mon.focused = m.value("focused", false);
            if (m.contains("activeWorkspace")) mon.activeWorkspace = m["activeWorkspace"].value("id", 0);
            monitors.push_back(mon);
        }
    }
    m_workspaces = std::move(workspaces);
    m_monitors = std::move(monitors);
    workspacesChanged.emit();
}

void Hyprland::refreshActiveWindow() {
    std::string title, cls;
    if (auto j = requestJson("activewindow"); j && j->is_object()) {
        title = j->value("title", "");
        cls = j->value("class", "");
    }
    if (title == m_activeTitle && cls == m_activeClass) return;
    m_activeTitle = std::move(title);
    m_activeClass = std::move(cls);
    activeWindowChanged.emit();
}

} // namespace m3
