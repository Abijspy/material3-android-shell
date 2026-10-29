#pragma once

#include "core/Signal.h"
#include "wayland/LayerSurface.h"

#include <memory>
#include <string>

namespace m3::shell {

namespace desktop {
class Board;
}

// The background of one output: the wallpaper (cover-fit, with an animated
// transition when it changes), then the desktop's widgets and shortcuts
// (desktop::Item), which take clicks where they are and nowhere else.
class Desktop {
public:
    explicit Desktop(wl::Output* output);
    // Settings changed: the widgets rebuilt on the same surface (recreating
    // it would flash the wallpaper)
    void refresh();

private:
    void fill(); // the items, from config.json
    // niri's overview: the wallpaper blurred, as the backdrop behind the
    // workspaces (material3ui-backdrop, put there by the shell's niri rules)
    void syncBackdrop();
    wl::Output* m_output = nullptr;
    std::unique_ptr<wl::LayerSurface> m_surface, m_backdrop;
    desktop::Board* m_board = nullptr;
    std::string m_items; // as last filled, to tell a change
    Connection m_config, m_editing;
};

} // namespace m3::shell
