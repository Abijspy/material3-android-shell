#pragma once

#include "core/Signal.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace m3 {

// The compositor the shell runs on, as one model whichever it is: its
// outputs, workspaces and windows, kept current from its events, and what
// the shell asks of it (switch workspace, focus or close a window, screens
// off, log out). Hyprland's backend (services/Hyprland, which also keeps
// what only Hyprland has: its options, binds and layer rules) or niri's
// (services/Niri), as the session says. What not every compositor can do is
// asked of can() first.
class Compositor {
public:
    enum class Feature {
        Minimise,       // a window put out of sight, and brought back
        KeyGrab,        // every key to the shell for a moment, binds included (the switcher, recording a shortcut)
        WindowGeometry, // each window's place on the desktop (the overview, picking a window to capture)
        CursorPosition, // where the pointer is on the desktop
        Reload,         // its config read again on request
        NativeOverview, // its own overview of the workspaces, in place of the shell's
        DynamicWorkspaces, // as many as are used, and one empty: none to show beyond them
        WindowCapture,  // it screenshots a window itself (without WindowGeometry for grim)
    };
    struct Output {
        std::string name;
        bool focused = false;
        int activeWorkspace = 0;                      // its number
        float x = 0, y = 0, w = 0, h = 0, scale = 1; // on the desktop, logical
    };
    struct Workspace {
        int number = 0; // as shown and switched to: Hyprland's id, niri's place on its output
        std::string name, output;
        int windows = 0;
    };
    struct Window {
        std::string id; // Hyprland's address, niri's id
        std::string appId, title, output;
        int workspace = 0; // its number (0 while minimised)
        std::string workspaceName;
        bool minimised = false, floating = false;
        float x = 0, y = 0, w = 0, h = 0; // with WindowGeometry
        int focusOrder = 0;               // 0 the focused one, then less and less recent
    };

    // The one the shell runs on (a stand-in with nothing in it, outside any)
    static Compositor& get();
    virtual ~Compositor() = default;

    virtual std::string name() const = 0; // "Hyprland"
    virtual bool available() const = 0;
    virtual std::string version() const = 0;
    virtual bool can(Feature feature) const = 0;

    // Kept current
    virtual const std::vector<Output>& outputs() const = 0;
    virtual const std::vector<Workspace>& workspaces() const = 0;
    // The ones a bar on `output` shows: every one where the numbers are the
    // desktop's (Hyprland), that output's own where each has its own (niri)
    virtual std::vector<Workspace> workspacesOn(const std::string& output) const = 0;
    virtual const std::string& activeTitle() const = 0;
    virtual const std::string& activeAppId() const = 0;
    // The active workspace's number on `output` ("": the focused one)
    int activeWorkspace(const std::string& output = {}) const;
    std::string focusedOutput() const;

    // Asked for when needed
    virtual std::vector<Window> windows(bool minimised = false) const = 0;
    // With CursorPosition: on the desktop
    virtual std::optional<std::pair<float, float>> cursorPosition() const = 0;

    // Asked of it
    // On `output` where each has its own numbers (niri); else the focused one
    virtual void focusWorkspace(int number, const std::string& output = {}) = 0;
    virtual void focusWorkspaceRelative(int delta) = 0; // on the focused output, wrapping round
    virtual void focusWindow(const std::string& id) = 0; // a minimised one comes back
    virtual void closeWindow(const std::string& id) = 0;
    virtual void minimise(const std::string& appId) = 0; // with Minimise: its windows
    virtual void grabKeys(bool grab) = 0;                // with KeyGrab
    virtual void setScreens(bool on) = 0;
    // Ends the session; false when it can't (logind is asked instead)
    virtual bool quit() = 0;
    virtual void reload() = 0; // with Reload
    virtual void toggleOverview() {} // with NativeOverview
    // With WindowCapture: the focused window to `path`; `done` with success
    virtual void captureWindow(const std::string& /*path*/, std::function<void(bool)> done) { done(false); }

    Signal<> workspacesChanged;   // which exist, which is active, the outputs
    Signal<> activeWindowChanged; // its title or app
    Signal<> windowsChanged;      // one opened, closed, moved or focused
};

} // namespace m3
