#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "ui/Node.h"
#include "wayland/LayerSurface.h"
#include "wayland/Window.h"

#include <functional>
#include <memory>
#include <string>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

// A panel that drops from the bar. The surface covers the output's usable
// area on the overlay layer with exclusive keyboard focus; the card is placed
// under `anchorX` (or centred). Esc or a click outside closes it.
//
// With `window`, the same contents live in an app window instead (the
// Settings app): its own title bar with minimise, maximise and close, a
// dock icon through its app id, and no closing on Esc or outside clicks.
class Popup {
public:
    struct Options {
        std::string name;
        float anchorX = -1; // < 0: centred horizontally
        float width = 360; // 0: as wide as the content
        bool centredVertically = false;
        bool fill = false; // the card takes the whole output (overview)
        // An app window instead of a layer popup
        bool window = false;
        std::string appId, title, icon; // icon: a theme icon for the title bar
        int windowWidth = 1100, windowHeight = 760;
    };

    Popup(wl::Output* output, Options options, const std::function<void(ui::Flex* card, Popup& popup)>& build);
    ~Popup();

    void close();
    // Out of the way for a moment (a tool needs the screen): a popup
    // closes, a window minimises
    void dismiss();
    void minimize();
    void toggleMaximized();
    bool isWindow() const { return m_window != nullptr; }
    wl::Window* window() const { return m_window.get(); }
    bool closing() const { return m_closing; }
    const std::string& name() const { return m_options.name; }
    wl::Output* output() const { return m_output; }
    wl::LayerSurface* surface() const { return m_surface.get(); }

    std::function<void()> onClosed; // after the exit animation
    ui::Node* initialFocus = nullptr; // set by the builder: gets the keyboard

private:
    wl::Output* m_output;
    Options m_options;
    std::unique_ptr<wl::LayerSurface> m_surface;
    std::unique_ptr<wl::Window> m_window;
    ui::Icon* m_maxIcon = nullptr;
    void buildTitleBar(ui::Flex* card);
    ui::Flex* m_card = nullptr;
    bool m_closing = false;
    Connection m_theme;
    float m_drop = -1; // which way the card slides in and out
    EventLoop::TimerId m_closeTimer = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace m3::shell
