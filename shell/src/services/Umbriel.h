#pragma once

#include "core/EventLoop.h"
#include "services/Compositor.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// Umbriel (Noctalia's compositor) over its IPC socket ($UMBRIEL_SOCKET,
// else $XDG_RUNTIME_DIR/umbriel-$WAYLAND_DISPLAY.sock): a JSON request a
// line, {"ok": …} or {"err": …} back, and on a connection of its own a
// subscription that starts with each family's state and sends the whole
// of it again on each change (workspaces, windows, outputs, the keyboard
// layout). The shell's compositor backend in an Umbriel session. Like
// niri's, its workspaces are each output's own, numbered from 1, with an
// empty one at the end; it has its own overview.
//
// Umbriel is young and changes as it's developed: fields it no longer
// sends read as empty, ones it adds are ignored, a refused action is only
// logged. When it refuses the subscription (a family renamed), the
// keyboard layout's is dropped first; refused again, the state is asked
// for every 1.5 s instead, so the bar and dock keep up.
class Umbriel : public Compositor, public EventSource {
public:
    static Umbriel& get();
    // Where its socket is in this session; "" outside Umbriel
    static std::string socketPath();

    // Compositor
    std::string name() const override { return "Umbriel"; }
    bool available() const override { return !m_path.empty(); }
    std::string version() const override;
    bool can(Feature feature) const override;
    const std::vector<Output>& outputs() const override { return m_outputs; }
    const std::vector<Workspace>& workspaces() const override { return m_workspaces; }
    std::vector<Workspace> workspacesOn(const std::string& output) const override;
    const std::string& activeTitle() const override { return m_activeTitle; }
    const std::string& activeAppId() const override { return m_activeAppId; }
    std::vector<Window> windows(bool minimised = false) const override;
    std::optional<std::pair<float, float>> cursorPosition() const override { return std::nullopt; }
    void focusWorkspace(int number, const std::string& output = {}) override;
    void focusWorkspaceRelative(int delta) override;
    void focusWindow(const std::string& id) override;
    void closeWindow(const std::string& id) override;
    void minimise(const std::string&) override {}
    void grabKeys(bool) override {}
    void setScreens(bool on) override;
    bool quit() override;
    void reload() override;
    void toggleOverview() override;
    std::vector<std::string> keyboardLayouts() const override { return m_layouts; }
    int keyboardLayout() const override { return m_layout; }
    void nextKeyboardLayout() override;

    // Umbriel's own
    // A request ({"cmd": "workspaces"}): the reply's ok value, or nothing
    std::optional<nlohmann::json> request(const nlohmann::json& request) const;
    // An action by name, with its typed fields ({"workspace": 2}); false when refused
    bool action(const std::string& name, nlohmann::json fields = nlohmann::json::object()) const;

    // EventSource: the subscription
    int fd() const override { return m_eventFd; }
    void dispatch(short revents) override;

private:
    Umbriel();
    void connectEvents();
    void handle(const nlohmann::json& event);
    void refused(); // the subscription: with less, or polling
    void poll();
    void rebuild(); // the snapshots, as the shell's types
    int workspaceNumber(const std::string& id) const; // its place on its output, 0 if none

    std::string m_path;
    int m_eventFd = -1;
    std::string m_buffer;
    nlohmann::json m_wsJson = nlohmann::json::array(), m_winJson = nlohmann::json::array(), m_outJson = nlohmann::json::array();
    std::vector<Output> m_outputs;
    std::vector<Workspace> m_workspaces;
    std::vector<std::string> m_recent; // window ids, the most recently active first
    std::vector<std::string> m_layouts;
    int m_layout = -1;
    std::string m_activeTitle, m_activeAppId;
    EventLoop::TimerId m_reconnect = 0, m_poll = 0;
    std::vector<std::string> m_families = {"workspaces", "windows", "outputs", "keyboard_layout"};
};

} // namespace m3
