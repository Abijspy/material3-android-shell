#pragma once

#include "wayland/Surface.h"

#include <functional>
#include <string>

struct xdg_surface;
struct xdg_toplevel;

namespace m3::wl {

// An xdg-shell toplevel: an ordinary app window the compositor tiles,
// floats, focuses and lists (the Settings app). It draws its own title bar;
// the compositor adds rounding, borders, shadows and blur.
class Window : public Surface {
public:
    struct Options {
        std::string appId = "material3ui";
        std::string title = "Material3UI";
        int width = 1000, height = 720; // used until the compositor picks a size
        int minWidth = 0, minHeight = 0;
    };

    explicit Window(Options options);
    ~Window() override;

    void setTitle(const std::string& title);
    void setMaximized(bool maximized);
    bool maximized() const { return m_maximized; }
    // Starts an interactive move (call from a pointer press on the title bar)
    void startMove();

    std::function<void()> onCloseRequested; // the compositor's close (Super+Q …)
    std::function<void()> onStateChanged;   // maximised or restored

    std::string describe() const override;

    // Protocol callbacks
    void surfaceConfigure(uint32_t serial);
    void toplevelConfigure(int width, int height);
    void close();

protected:
    void destroyRole() override;

private:
    Options m_options;
    xdg_surface* m_xdgSurface = nullptr;
    xdg_toplevel* m_toplevel = nullptr;
    int m_pendingW = 0, m_pendingH = 0;
    bool m_maximized = false; // as last asked for (Hyprland's states can't tell)
};

} // namespace m3::wl
