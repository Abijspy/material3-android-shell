#pragma once

#include "core/Signal.h"
#include "shell/bar/Status.h"
#include "wayland/LayerSurface.h"

namespace m3::ui {
class Flex;
}

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

// The status bar on one output: workspaces and the window title on the left,
// the clock in the centre, status on the right.
class Bar {
public:
    Bar(wl::Output* output, OpenPopup openPopup);

    void rebuild();
    void setVisible(bool visible);
    bool visible() const { return m_surface->visible(); }
    void redraw() { m_surface->requestRedraw(); }

    // Top or bottom edge, from statusBar.position
    static uint32_t anchor();

    // Modules the bar can show, and where they are: statusBar.layout holds
    // {"left": [ids], "center": [ids], "right": [ids]}
    struct Module {
        const char* id;
        const char* icon;
        const char* name;
        const char* about = "";
    };
    static const std::vector<Module>& modules();
    // Edit mode, on every bar at once: widgets get move/remove controls,
    // each zone a "+" that opens the widget picker, and a Done button
    static inline bool editing = false;
    using Layout = std::array<std::vector<std::string>, 3>; // left, centre, right
    static Layout layout();
    static void setLayout(const Layout& layout);

private:
    int height() const;
    void addModule(ui::Flex* zone, const std::string& id, bool floating);

    wl::Output* m_output;
    OpenPopup m_openPopup;
    std::unique_ptr<wl::LayerSurface> m_surface;
};

} // namespace m3::shell
