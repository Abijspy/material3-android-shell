#pragma once

#include "core/EventLoop.h"

#include <memory>
#include <string>

namespace m3::ui {
class Node;
class Flex;
class Label;
} // namespace m3::ui
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
    // A node's tooltip text changed: the one showing follows it
    void changed(ui::Node* node);

private:
    Tooltip() = default;
    void show(wl::Surface* source, const std::string& text, float x, float y, float w, float h);
    void place(); // sizes and centres the surface for the label's text
    std::unique_ptr<wl::LayerSurface> m_surface;
    ui::Node* m_node = nullptr; // what it explains (alive: its surface reports its end)
    ui::Flex* m_root = nullptr;
    ui::Label* m_label = nullptr;
    wl::Surface* m_source = nullptr;
    float m_x = 0, m_y = 0, m_w = 0, m_h = 0; // the item, in the source's coordinates
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3::shell
