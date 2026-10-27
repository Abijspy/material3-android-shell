#pragma once

#include "services/Compositor.h"

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// niri's state as its event stream tells it (niri-ipc's Event enum): the
// windows and workspaces, their focus, the keyboard layouts and the
// overview. Apart from the backend (services/Niri), for the unit tests.
namespace m3::niri {

struct Model {
    struct Win {
        uint64_t id = 0;
        std::string title, appId;
        std::optional<uint64_t> workspace;
        bool focused = false, floating = false, urgent = false;
        double focusTime = 0; // seconds, niri's monotonic focus timestamp
    };
    struct Ws {
        uint64_t id = 0;
        int idx = 0; // its place on its output, from 1
        std::string name, output;
        bool active = false, focused = false, urgent = false;
    };
    struct Out {
        std::string name;
        float x = 0, y = 0, w = 0, h = 0, scale = 1; // logical
    };

    std::map<uint64_t, Win> windows;
    std::map<uint64_t, Ws> workspaces;
    std::vector<Out> outputs; // from the Outputs request (no event says)
    std::vector<std::string> layouts;
    int layout = 0; // the keyboard layout in use
    bool overview = false;

    // What an event changed
    enum Change : unsigned {
        None = 0,
        Workspaces = 1,
        Windows = 2,
        Focus = 4, // the focused window, or its title
        Layouts = 8,
        Overview = 16,
    };
    // One line of the event stream, parsed: what it changed
    unsigned apply(const nlohmann::json& event);
    // The Outputs request's reply ({"winit": {"logical": {…}}, …})
    void setOutputs(const nlohmann::json& reply);
    // The Workspaces and Windows requests' replies (the state at the start)
    void setWorkspaces(const nlohmann::json& list);
    void setWindows(const nlohmann::json& list);

    // As the shell sees it
    std::vector<Compositor::Workspace> shellWorkspaces() const;
    std::vector<Compositor::Output> shellOutputs() const;
    std::vector<Compositor::Window> shellWindows() const;
    const Win* focusedWindow() const;
    std::string focusedOutput() const;
    // The workspace at `idx` on `output` ("": the focused one)
    const Ws* workspaceAt(int idx, const std::string& output) const;
};

} // namespace m3::niri
