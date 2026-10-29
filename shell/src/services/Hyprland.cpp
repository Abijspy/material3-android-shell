#include "services/Hyprland.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <map>
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
    "changeworkspaceid", // 0.57: a workspace renumbered
};

const std::set<std::string> kWindowEvents = {
    "activewindow", "activewindowv2", "windowtitle", "windowtitlev2", "closewindow", "workspace", "workspacev2",
};

// Events after which the windows (which, where, which has focus) may differ
const std::set<std::string> kWindowsEvents = {
    "openwindow", "closewindow", "activewindow", "movewindow", "workspace", "focusedmon", "changeworkspaceid",
};

} // namespace

Hyprland& Hyprland::get() {
    static Hyprland instance;
    return instance;
}

Hyprland::Hyprland() {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig || !*sig) return;
    // niri started from a Hyprland session passes its variable on: niri's
    // session, then, and nothing of it to touch
    if (const char* niri = std::getenv("NIRI_SOCKET"); niri && *niri) return;
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

std::string Hyprland::version() const {
    if (auto v = requestJson("version")) return v->value("tag", v->value("version", ""));
    return {};
}

bool Hyprland::can(Feature feature) const {
    // Minimise (a special workspace), key grabs (a submap with no binds),
    // window geometry, the cursor's place and reloading; the overview and
    // window screenshots are the shell's (with grim)
    return available() && feature != Feature::NativeOverview && feature != Feature::WindowCapture && feature != Feature::DynamicWorkspaces;
}

std::vector<Compositor::Window> Hyprland::windows(bool minimised) const {
    std::vector<Window> out;
    auto j = requestJson("clients");
    if (!j || !j->is_array()) return out;
    for (const auto& c : *j) {
        Window w;
        w.id = c.value("address", "");
        w.appId = c.value("class", "");
        w.title = c.value("title", "");
        if (const auto it = m_monitorNames.find(c.value("monitor", -1)); it != m_monitorNames.end()) w.output = it->second;
        w.floating = c.value("floating", false);
        w.focusOrder = c.value("focusHistoryID", 1 << 20);
        int ws = 0;
        bool special = false;
        if (c.contains("workspace")) {
            const auto& wj = c["workspace"];
            ws = wj.value("id", 0);
            w.workspaceName = wj.value("name", "");
            // Special workspaces (the scratchpad, minimised windows): ids < 0
            // before 0.57, no id at all since, and their type and name say so
            special = ws < 0 || wj.value("type", "") == "special" || w.workspaceName.starts_with("special:");
        }
        w.minimised = special;
        w.workspace = ws > 0 && !special ? ws : 0;
        if (c.contains("at") && c["at"].size() == 2) {
            w.x = c["at"][0].get<float>();
            w.y = c["at"][1].get<float>();
        }
        if (c.contains("size") && c["size"].size() == 2) {
            w.w = c["size"][0].get<float>();
            w.h = c["size"][1].get<float>();
        }
        if ((w.workspace > 0 || (minimised && special)) && c.value("mapped", true) && !c.value("hidden", false)) out.push_back(std::move(w));
    }
    return out;
}

std::optional<std::pair<float, float>> Hyprland::cursorPosition() const {
    auto c = requestJson("cursorpos");
    if (!c || !c->is_object()) return std::nullopt;
    return std::pair{c->value("x", 0.0f), c->value("y", 0.0f)};
}

void Hyprland::focusWindow(const std::string& address) {
    // A minimised window (parked on special:minimized) comes back to the
    // active workspace first, instead of the special workspace opening
    for (const auto& w : windows(true))
        if (w.id == address && w.minimised)
            // "e+0": the workspace in front of the user now
            dispatch(std::format("hl.dsp.window.move({{ workspace = \"e+0\", window = \"address:{}\" }})", address),
                     std::format("movetoworkspace e+0,address:{}", address));
    dispatch("hl.dsp.focus({ window = \"address:" + address + "\" })", "focuswindow address:" + address);
}

void Hyprland::closeWindow(const std::string& address) {
    dispatch("hl.dsp.window.close({ window = \"address:" + address + "\" })", "closewindow address:" + address);
}

void Hyprland::minimise(const std::string& appId) {
    // Hyprland has no minimised state: the windows go to a hidden special
    // workspace; opening the app or its dock icon brings them back
    const std::string cls = "class:^(" + appId + ")$";
    dispatch("hl.dsp.window.move({ workspace = \"special:minimized\", window = \"" + cls + "\", silent = true })",
             "movetoworkspacesilent special:minimized," + cls);
}

void Hyprland::grabKeys(bool grab) {
    // An empty submap (shell.lua's): Hyprland's binds pause until reset
    request(std::string("dispatch hl.dsp.submap(\"") + (grab ? "material3ui_capture" : "reset") + "\")");
}

void Hyprland::setScreens(bool on) {
    dispatch(on ? "hl.dsp.dpms(\"on\")" : "hl.dsp.dpms(\"off\")", on ? "dpms on" : "dpms off");
}

bool Hyprland::quit() {
    if (!available()) return false;
    request(m_lua ? "dispatch hl.dsp.exit()" : "dispatch exit");
    return true;
}

void Hyprland::reload() {
    request("reload");
}

void Hyprland::focusWorkspace(int id, const std::string&) {
    request(m_lua ? std::format("dispatch hl.dsp.focus({{ workspace = \"{}\" }})", id)
                  : std::format("dispatch workspace {}", id));
}

void Hyprland::focusWorkspaceRelative(int delta) {
    request(m_lua ? std::format("dispatch hl.dsp.focus({{ workspace = \"m{:+}\" }})", delta)
                  : std::format("dispatch workspace m{:+}", delta));
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
    if (kWindowsEvents.contains(name)) windowsChanged.emit();
    // Events arrive in bursts (a workspace switch is several); refresh once
    if (kWorkspaceEvents.contains(name)) EventLoop::get().debounce(m_workspaceDebounce, 16, [this] { refreshWorkspaces(); });
    if (kWindowEvents.contains(name)) EventLoop::get().debounce(m_windowDebounce, 16, [this] { refreshActiveWindow(); });
}

void Hyprland::refreshWorkspaces() {
    std::vector<Workspace> workspaces;
    if (auto j = requestJson("workspaces"); j && j->is_array()) {
        for (const auto& w : *j) {
            Workspace ws;
            ws.number = w.value("id", 0);
            ws.name = w.value("name", "");
            ws.output = w.value("monitor", "");
            ws.windows = w.value("windows", 0);
            if (ws.number > 0) workspaces.push_back(ws); // skip special workspaces
        }
    }
    std::ranges::sort(workspaces, {}, &Workspace::number);

    std::vector<Output> outputs;
    m_monitorNames.clear();
    if (auto j = requestJson("monitors"); j && j->is_array()) {
        for (const auto& m : *j) {
            Output o;
            o.name = m.value("name", "");
            m_monitorNames[m.value("id", -1)] = o.name; // the windows name theirs by id
            o.focused = m.value("focused", false);
            if (m.contains("activeWorkspace")) o.activeWorkspace = m["activeWorkspace"].value("id", 0);
            // Logical: the mode divided by the scale
            o.scale = m.value("scale", 1.0f);
            o.x = m.value("x", 0.0f);
            o.y = m.value("y", 0.0f);
            o.w = m.value("width", 0.0f) / o.scale;
            o.h = m.value("height", 0.0f) / o.scale;
            outputs.push_back(o);
        }
    }
    m_workspaces = std::move(workspaces);
    m_outputs = std::move(outputs);
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
