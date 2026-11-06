#include "services/Umbriel.h"

#include "core/Log.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
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

std::string str(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

float num(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : 0.0f;
}

bool flag(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

} // namespace

Umbriel& Umbriel::get() {
    static Umbriel instance;
    return instance;
}

std::string Umbriel::socketPath() {
    if (const char* s = std::getenv("UMBRIEL_SOCKET"); s && *s) return s;
    return {};
}

Umbriel::Umbriel() {
    m_path = socketPath();
    if (m_path.empty()) return;
    // The state now, for whoever asks before the subscription's first lines
    if (auto ws = request({{"cmd", "workspaces"}}); ws && ws->is_array()) m_wsJson = *ws;
    if (auto wins = request({{"cmd", "windows"}}); wins && wins->is_array()) m_winJson = *wins;
    if (auto outs = request({{"cmd", "outputs"}}); outs && outs->is_array()) m_outJson = *outs;
    rebuild();
    connectEvents();
}

std::optional<nlohmann::json> Umbriel::request(const nlohmann::json& req) const {
    if (m_path.empty()) return std::nullopt;
    const int fd = connectUnix(m_path, false);
    if (fd < 0) return std::nullopt;
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string reply;
    if (writeAll(fd, req.dump() + "\n")) {
        char buf[65536];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof buf)) > 0) {
            reply.append(buf, static_cast<size_t>(n));
            if (reply.find('\n') != std::string::npos) break;
        }
    }
    close(fd);
    try {
        const auto j = nlohmann::json::parse(reply.substr(0, reply.find('\n')));
        if (j.contains("ok")) return j["ok"];
        if (j.contains("err")) log::warn("umbriel: {} → {}", req.dump(), j["err"].dump());
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

bool Umbriel::action(const std::string& name, nlohmann::json fields) const {
    fields["cmd"] = "action";
    fields["action"] = name;
    return request(fields).has_value();
}

std::string Umbriel::version() const {
    // "umbriel 0.1.0 (90f65cb)": the IPC has no version of its own
    std::string out;
    if (FILE* p = popen("umbriel --version 2>/dev/null", "r")) {
        std::array<char, 256> buf{};
        while (fgets(buf.data(), buf.size(), p)) out += buf.data();
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    if (out.starts_with("umbriel ")) out.erase(0, 8);
    return out;
}

bool Umbriel::can(Feature feature) const {
    if (!available()) return false;
    switch (feature) {
    case Feature::Reload:
    case Feature::NativeOverview:
    case Feature::DynamicWorkspaces:
    // Floating windows; window-focus brings one back
    case Feature::Minimise:
    // The focused window's box is where it shows (a window screenshot)
    case Feature::WindowGeometry: return true;
    // No key grab for the shell; the pointer's place isn't said
    default: return false;
    }
}

// ── State ──

void Umbriel::connectEvents() {
    if (m_eventFd >= 0) {
        EventLoop::get().remove(this);
        close(m_eventFd);
    }
    m_eventFd = connectUnix(m_path, true);
    const nlohmann::json subscribe = {{"cmd", "subscribe"}, {"events", m_families}};
    if (m_eventFd < 0 || !writeAll(m_eventFd, subscribe.dump() + "\n")) {
        if (m_eventFd >= 0) close(m_eventFd);
        m_eventFd = -1;
        log::warn("cannot subscribe to Umbriel's events; retrying");
        m_reconnect = EventLoop::get().addTimer(2000, [this] {
            m_reconnect = 0;
            connectEvents();
        });
        return;
    }
    m_buffer.clear();
    EventLoop::get().add(this);
}

void Umbriel::dispatch(short) {
    char buf[65536];
    for (;;) {
        const ssize_t n = ::read(m_eventFd, buf, sizeof buf);
        if (n > 0) {
            m_buffer.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            // Umbriel restarted (or quit): again when it's back
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
            handle(nlohmann::json::parse(line));
        } catch (const std::exception& e) {
            log::warn("umbriel: an event it couldn't read: {}", e.what());
        }
    }
}

void Umbriel::handle(const nlohmann::json& event) {
    if (event.contains("err")) {
        log::warn("umbriel: the subscription was refused: {}", event["err"].dump());
        EventLoop::get().defer([this] { refused(); });
        return;
    }
    const std::string family = str(event, "event");
    const auto data = event.find("data");
    if (family.empty() || data == event.end()) return;
    if (family == "keyboard_layout") {
        std::vector<std::string> names;
        if (const auto n = data->find("names"); n != data->end() && n->is_array())
            for (const auto& v : *n)
                if (v.is_string()) names.push_back(v.get<std::string>());
        const int current = data->value("current_index", -1);
        if (names == m_layouts && current == m_layout) return;
        m_layouts = std::move(names);
        m_layout = current;
        keyboardLayoutsChanged.emit();
        return;
    }
    if (!data->is_array()) return;
    // The same again (polling asks whether or not it changed): nothing to do
    if ((family == "workspaces" && *data == m_wsJson) || (family == "windows" && *data == m_winJson) || (family == "outputs" && *data == m_outJson))
        return;
    // Each is the whole of its family: in place of what was
    const auto oldWs = m_workspaces;
    if (family == "workspaces") m_wsJson = *data;
    else if (family == "windows") m_winJson = *data;
    else if (family == "outputs") m_outJson = *data;
    else return;
    rebuild();
    if (family == "windows") windowsChanged.emit();
    // Windows change the workspaces' counts too
    if (family != "windows" || m_workspaces.size() != oldWs.size() ||
        !std::ranges::equal(m_workspaces, oldWs, [](const Workspace& a, const Workspace& b) { return a.number == b.number && a.output == b.output && a.windows == b.windows; }))
        workspacesChanged.emit();
}

void Umbriel::refused() {
    // It closes a refused subscription: no reconnecting as if it restarted
    if (m_reconnect) {
        EventLoop::get().cancel(m_reconnect);
        m_reconnect = 0;
    }
    if (m_eventFd >= 0) {
        EventLoop::get().remove(this);
        close(m_eventFd);
        m_eventFd = -1;
    }
    // The keyboard layout's family is the one to do without
    if (m_families.size() > 3) {
        m_families.pop_back();
        log::info("umbriel: subscribing without the keyboard layout");
        connectEvents();
        return;
    }
    if (m_poll) return;
    log::warn("umbriel: no event stream from this Umbriel; asking every 1.5 s instead");
    m_poll = EventLoop::get().addTimer(1500, [this] { poll(); }, true);
}

void Umbriel::poll() {
    for (const char* family : {"workspaces", "windows", "outputs"})
        if (auto data = request({{"cmd", family}}); data && data->is_array()) handle({{"event", family}, {"data", *data}});
}

int Umbriel::workspaceNumber(const std::string& id) const {
    for (const auto& w : m_wsJson)
        if (str(w, "id") == id) return w.value("index", 0);
    return 0;
}

void Umbriel::rebuild() {
    m_workspaces.clear();
    for (const auto& w : m_wsJson) {
        if (!w.is_object()) continue;
        Workspace ws;
        ws.number = w.value("index", 0);
        ws.name = flag(w, "named") ? str(w, "name") : std::string();
        ws.output = str(w, "output");
        ws.windows = w.value("window_count", 0);
        m_workspaces.push_back(ws);
    }
    m_outputs.clear();
    for (const auto& o : m_outJson) {
        if (!o.is_object() || !flag(o, "enabled")) continue;
        Output out;
        out.name = str(o, "name");
        out.focused = flag(o, "focused");
        out.activeWorkspace = workspaceNumber(str(o, "active_workspace"));
        if (const auto p = o.find("position"); p != o.end() && p->is_object()) {
            out.x = num(*p, "x");
            out.y = num(*p, "y");
        }
        if (const auto s = o.find("logical_size"); s != o.end() && s->is_object()) {
            out.w = num(*s, "width");
            out.h = num(*s, "height");
        }
        out.scale = o.value("scale", 1.0f);
        m_outputs.push_back(out);
    }
    // The active window (the seat's), first among the most recent
    std::string active, title, app;
    for (const auto& w : m_winJson)
        if (w.is_object() && flag(w, "active")) {
            active = str(w, "id");
            title = str(w, "title");
            app = str(w, "app_id");
        }
    if (!active.empty()) {
        std::erase(m_recent, active);
        m_recent.insert(m_recent.begin(), active);
    }
    std::erase_if(m_recent, [this](const std::string& id) {
        return std::ranges::none_of(m_winJson, [&](const nlohmann::json& w) { return str(w, "id") == id; });
    });
    if (title != m_activeTitle || app != m_activeAppId) {
        m_activeTitle = title;
        m_activeAppId = app;
        activeWindowChanged.emit();
    }
}

std::vector<Compositor::Workspace> Umbriel::workspacesOn(const std::string& output) const {
    const std::string on = output.empty() ? focusedOutput() : output;
    std::vector<Workspace> out;
    for (const auto& w : m_workspaces)
        if (w.output == on) out.push_back(w);
    return out;
}

std::vector<Compositor::Window> Umbriel::windows(bool minimised) const {
    std::vector<Window> out;
    for (const auto& w : m_winJson) {
        if (!w.is_object()) continue;
        Window win;
        win.id = str(w, "id");
        if (win.id.empty()) continue;
        win.minimised = flag(w, "minimized");
        if (win.minimised && !minimised) continue;
        win.appId = str(w, "app_id");
        win.title = str(w, "title");
        win.output = str(w, "output");
        const std::string ws = str(w, "workspace");
        win.workspace = win.minimised ? 0 : workspaceNumber(ws);
        for (const auto& j : m_wsJson)
            if (str(j, "id") == ws && flag(j, "named")) win.workspaceName = str(j, "name");
        win.floating = flag(w, "floating");
        win.x = num(w, "x");
        win.y = num(w, "y");
        win.w = num(w, "w");
        win.h = num(w, "h");
        const auto it = std::ranges::find(m_recent, win.id);
        win.focusOrder = it != m_recent.end() ? static_cast<int>(it - m_recent.begin()) : static_cast<int>(m_recent.size()) + 1;
        out.push_back(win);
    }
    return out;
}

// ── Actions ──

void Umbriel::focusWorkspace(int number, const std::string& output) {
    nlohmann::json fields = {{"workspace", number}};
    if (!output.empty()) fields["output"] = output;
    action("workspace-switch", fields);
}

void Umbriel::focusWorkspaceRelative(int delta) {
    for (int i = 0; i < std::abs(delta); ++i) action(delta > 0 ? "workspace-next" : "workspace-previous");
}

void Umbriel::focusWindow(const std::string& id) {
    if (!id.empty()) action("window-focus", {{"window", id}});
}

void Umbriel::closeWindow(const std::string& id) {
    if (!id.empty()) action("window-close", {{"window", id}});
}

void Umbriel::minimise(const std::string& appId) {
    // What Umbriel says can go (floating, on a workspace, not pinned)
    for (const auto& w : m_winJson)
        if (w.is_object() && str(w, "app_id") == appId && flag(w, "minimizable")) action("window-minimize", {{"window", str(w, "id")}});
}

void Umbriel::setScreens(bool on) {
    action(on ? "dpms-on" : "dpms-off");
}

bool Umbriel::quit() {
    if (!available()) return false;
    return action("session-quit", {{"skip_confirmation", true}});
}

void Umbriel::reload() {
    action("config-reload");
}

void Umbriel::toggleOverview() {
    action("overview-toggle");
}

void Umbriel::nextKeyboardLayout() {
    action("keyboard-layout-next");
}

} // namespace m3
