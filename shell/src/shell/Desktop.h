#pragma once

#include "core/Signal.h"
#include "wayland/LayerSurface.h"

#include <memory>

namespace m3::shell {

// The background of one output: the wallpaper (cover-fit, with an animated
// transition when it changes) and the desktop clock widget.
class Desktop {
public:
    explicit Desktop(wl::Output* output);

private:
    std::unique_ptr<wl::LayerSurface> m_surface;
};

} // namespace m3::shell
