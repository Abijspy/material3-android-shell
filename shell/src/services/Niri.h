#pragma once

#include "core/EventLoop.h"
#include "services/Compositor.h"
#include "services/NiriModel.h"

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// niri over its IPC socket ($NIRI_SOCKET): a JSON request a line, its reply
// a line, and on a connection of its own the event stream, which starts
// with the whole state and keeps it current (services/NiriModel). The
// shell's compositor backend in a niri session. niri's workspaces are each
// output's own, numbered from 1, with an empty one at the end; it has its
// own overview and window screenshots, and no minimised windows.
class Niri : public Compositor, public EventSource {
public:
    static Niri& get();

    // Compositor
    std::string name() const override { return "niri"; }
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
    void grabKeys(bool) override {} // a layer surface with the keyboard has it, but for niri's binds
    void setScreens(bool on) override;
    bool quit() override;
    void reload() override;
    void toggleOverview() override;
    void captureWindow(const std::string& path, std::function<void(bool)> done) override;

    // niri's own
    // A request ("Outputs", {"Action": …}): the reply's Ok value, or nothing
    std::optional<nlohmann::json> request(const nlohmann::json& request) const;
    void action(const nlohmann::json& action);
    const niri::Model& model() const { return m_model; }
    Signal<> layoutsChanged; // the keyboard layouts, or the one in use

    // EventSource: the event stream
    int fd() const override { return m_eventFd; }
    void dispatch(short revents) override;

private:
    Niri();
    void connectEvents();
    void handle(const nlohmann::json& event);
    void refreshOutputs();
    void publish(unsigned changes); // the model, as the shell's types, and the signals

    std::string m_path;
    int m_eventFd = -1;
    bool m_handled = false; // the stream's first line ({"Ok":"Handled"}) seen
    std::string m_buffer;
    niri::Model m_model;
    std::vector<Output> m_outputs;
    std::vector<Workspace> m_workspaces;
    std::string m_activeTitle, m_activeAppId;
    unsigned m_pending = 0; // changes waiting to be published
    EventLoop::TimerId m_publish = 0, m_reconnect = 0;
    // Window screenshots waiting for niri to say they're written
    std::map<std::string, std::pair<std::function<void(bool)>, EventLoop::TimerId>> m_captures;
};

} // namespace m3
