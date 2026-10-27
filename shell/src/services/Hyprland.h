#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "services/Compositor.h"

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// Hyprland over its two Unix sockets: requests on .socket.sock (JSON
// replies), and the event stream on .socket2.sock, which keeps workspaces,
// monitors and the active window current. The shell's compositor backend
// (Compositor), plus what only Hyprland has: raw requests and dispatches in
// its config's syntax, for Hyprland Settings, the binds and layer rules.
class Hyprland : public Compositor, public EventSource {
public:
    static Hyprland& get();

    // Compositor
    std::string name() const override { return "Hyprland"; }
    bool available() const override { return !m_dir.empty(); }
    std::string version() const override;
    bool can(Feature feature) const override;
    const std::vector<Output>& outputs() const override { return m_outputs; }
    const std::vector<Workspace>& workspaces() const override { return m_workspaces; }
    std::vector<Workspace> workspacesOn(const std::string&) const override { return m_workspaces; } // the desktop's numbers
    const std::string& activeTitle() const override { return m_activeTitle; }
    const std::string& activeAppId() const override { return m_activeClass; }
    // Mapped windows on regular workspaces; `minimised` adds those parked on
    // special:minimized (the Settings app's minimise)
    std::vector<Window> windows(bool minimised = false) const override;
    std::optional<std::pair<float, float>> cursorPosition() const override;
    void focusWorkspace(int number) override;
    void focusWorkspaceRelative(int delta) override; // on the focused monitor, wrapping over existing ones
    void focusWindow(const std::string& address) override;
    void closeWindow(const std::string& address) override;
    void minimise(const std::string& appId) override;
    void grabKeys(bool grab) override;
    void setScreens(bool on) override;
    bool quit() override;
    void reload() override;

    // Hyprland's own
    bool usingLua() const { return m_lua; }
    std::string request(const std::string& command) const;
    std::optional<nlohmann::json> requestJson(const std::string& command) const;
    // Dispatch in the config's syntax: a Lua expression or a legacy command
    void dispatch(const std::string& lua, const std::string& legacy);
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
    std::vector<Output> m_outputs;
    std::map<int, std::string> m_monitorNames; // id → name
    std::string m_activeTitle, m_activeClass;
    EventLoop::TimerId m_workspaceDebounce = 0;
    EventLoop::TimerId m_windowDebounce = 0;
    EventLoop::TimerId m_reconnect = 0;
};

} // namespace m3
