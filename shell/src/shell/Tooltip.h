#pragma once

#include "core/EventLoop.h"

#include <memory>
#include <string>

namespace m3::ui {
class Node;
}
namespace m3::wl {
class LayerSurface;
class Surface;
} // namespace m3::wl

namespace m3::shell {

// M3 plain tooltips for the bar and the dock: after the pointer rests on a
// node with a `tooltip` for a moment, a small pill of text on its own
// overlay surface, below the bar or above the dock, passing clicks through.
class Tooltip {
public:
    static Tooltip& get();
    // Surface::tooltipHook: a node to explain (after a pause), or null
    void hover(wl::Surface* surface, ui::Node* node);
    void hide();

private:
    Tooltip() = default;
    void show(wl::Surface* source, const std::string& text, float x, float y, float w, float h);
    std::unique_ptr<wl::LayerSurface> m_surface;
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3::shell
