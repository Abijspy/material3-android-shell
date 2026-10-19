#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "wayland/LayerSurface.h"

#include <memory>
#include <string>
#include <vector>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

// The dock on one output: pinned apps, then running ones, centred on the
// bottom edge. "always" reserves its space; "autohide" slides away and comes
// back when the pointer reaches the edge (or the workspace is empty).
// Click focuses an app's window (cycling through several) or launches it,
// middle click opens a new one, right click pins or unpins.
class Dock {
public:
    // Kept out while its right-click menu is open (the pointer leaves the
    // dock for the menu, which would otherwise hide it from under you)
    void setHeld(bool held);
    explicit Dock(wl::Output* output);
    ~Dock();

    void rebuild();

    // dock.pinned: desktop ids, in order
    static std::vector<std::string> pinned();
    static void setPinned(const std::vector<std::string>& ids);

private:
    struct Item;
    void refresh();         // windows changed: rebuild the icons
    void reveal(bool show); // autohide
    void updateInput();
    bool autoHide() const;
    int panelHeight() const;

    wl::Output* m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    class DockRoot* m_root = nullptr;
    ui::Flex* m_card = nullptr;
    bool m_shown = true, m_hovered = false, m_held = false;
    EventLoop::TimerId m_hideTimer = 0, m_refreshTimer = 0;
    Connection m_events, m_workspaces, m_apps;
};

} // namespace m3::shell
