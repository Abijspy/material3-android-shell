#pragma once

#include <memory>
#include <string>
#include <vector>

namespace m3::wl {
struct Output;
class LayerSurface;
} // namespace m3::wl

namespace m3::shell {

// Caelestia's look (Status bar → Style → Framed): the bar's surface goes on
// round the screen's other three edges as a thin frame whose inner corners
// are rounded, so the desktop sits in a rounded window of its own. One
// surface under the windows draws it all, the bar's own strip too (the bar
// draws only its widgets); three empty ones keep windows off the thin edges.
class Frame {
public:
    explicit Frame(wl::Output* output);
    ~Frame();
    // The settings changed: its thickness, corners or colour, the bar's edge
    void rebuild();
    void setVisible(bool visible);

    // Its thickness on the thin edges (Settings → Status bar → Frame)
    static int thickness();

private:
    wl::Output* m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    std::vector<std::unique_ptr<wl::LayerSurface>> m_zones;
    std::string m_zonesFor; // the edge and thickness the zones reserve
    bool m_visible = true;
};

} // namespace m3::shell
