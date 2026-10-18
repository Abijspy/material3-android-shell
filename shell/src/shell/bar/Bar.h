#pragma once

#include "core/Signal.h"
#include "shell/bar/Status.h"
#include "wayland/LayerSurface.h"

#include <memory>

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

private:
    int height() const;

    wl::Output* m_output;
    OpenPopup m_openPopup;
    std::unique_ptr<wl::LayerSurface> m_surface;
};

} // namespace m3::shell
