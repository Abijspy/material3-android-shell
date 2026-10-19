#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// Hyprland over its two Unix sockets: requests on .socket.sock (JSON
// replies), and the event stream on .socket2.sock, which keeps workspaces,
// monitors and the active window current.
class Hyprland : public EventSource {
public:
    struct Workspace {
        int id = 0;
        std::string name;
        std::string monitor;
        int windows = 0;
    };
    struct Monitor {
        std::string name;
        int activeWorkspace = 0;
        bool focused = false;
    };

    struct Client {
        std::string address, cls, title, monitor;
        int workspace = 0;
        float x = 0, y = 0, w = 0, h = 0;
        bool floating = false;
    };
    struct MonitorGeometry {
        std::string name;
        float x = 0, y = 0, w = 0, h = 0, scale = 1;
    };

    static Hyprland& get();

    // Fetched on demand (the overview), not tracked
    // Mapped windows on regular workspaces; `minimised` adds those parked on
    // special workspaces (the Settings app's minimise), which have ids < 0
    std::vector<Client> clients(bool minimised = false) const;
    std::vector<MonitorGeometry> monitorGeometry() const;
    void focusWindow(const std::string& address);

    bool available() const { return !m_dir.empty(); }
    bool usingLua() const { return m_lua; }

    std::string request(const std::string& command) const;
    std::optional<nlohmann::json> requestJson(const std::string& command) const;

    // Dispatch in the config's syntax: a Lua expression or a legacy command
    void dispatch(const std::string& lua, const std::string& legacy);
    void focusWorkspace(int id);
    void focusWorkspaceRelative(int delta); // on the focused monitor, wrapping over existing ones

    const std::vector<Workspace>& workspaces() const { return m_workspaces; }
    const std::vector<Monitor>& monitors() const { return m_monitors; }
    int activeWorkspace(const std::string& monitor) const;
    const std::string& activeWindowTitle() const { return m_activeTitle; }
    const std::string& activeWindowClass() const { return m_activeClass; }

    // Emitted after the state has been refreshed
    Signal<> workspacesChanged;
    Signal<> activeWindowChanged;
    Signal<const std::string&, const std::string&> event; // raw socket2 events

    // EventSource: the event socket
    int fd() const override { return m_eventFd; }
    void dispatch(short revents) override;

private:
    Hyprland();
    void connectEvents();
    void handleEvent(const std::string& name, const std::string& data);
    void refreshWorkspaces();
    void refreshActiveWindow();

    std::string m_dir;
    bool m_lua = false;
    int m_eventFd = -1;
    std::string m_buffer;

    std::vector<Workspace> m_workspaces;
    std::vector<Monitor> m_monitors;
    std::string m_activeTitle, m_activeClass;

    EventLoop::TimerId m_workspaceDebounce = 0;
    EventLoop::TimerId m_windowDebounce = 0;
    EventLoop::TimerId m_reconnect = 0;
};

} // namespace m3
