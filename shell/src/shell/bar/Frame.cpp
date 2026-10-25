#include "shell/bar/Frame.h"

#include "core/Config.h"
#include "shell/bar/Bar.h"
#include "shell/bar/Widgets.h"
#include "ui/Theme.h"
#include "wayland/LayerSurface.h"

#include <algorithm>
#include <format>

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

namespace {

// A strip the compositor blurs behind (where it takes regions: Hyprland
// blurs by the frame's alpha, corners and all)
class BlurStrip : public Node {
protected:
    SizeF doMeasure(SizeF) override { return {}; }
};

// The bar's colour everywhere but a rounded hole: the bar's thickness on
// its edge, the frame's on the others
class FrameShape : public Node {
public:
    Insets inset;
    float radius = 25;

    FrameShape() {
        for (int i = 0; i < 4; ++i) add<BlurStrip>()->blurBehind = barBlur();
    }

protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doArrange() override {
        const auto& b = rect();
        const RectF strips[4] = {{b.x, b.y, b.w, inset.top},
                                 {b.x, b.bottom() - inset.bottom, b.w, inset.bottom},
                                 {b.x, b.y + inset.top, inset.left, b.h - inset.top - inset.bottom},
                                 {b.right() - inset.right, b.y + inset.top, inset.right, b.h - inset.top - inset.bottom}};
        for (size_t i = 0; i < children().size() && i < 4; ++i) children()[i]->arrange(strips[i]);
    }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, ColorRef("surface_container", Theme::get().barOpacity()).resolve());
        r.erase({b.x + inset.left, b.y + inset.top, b.w - inset.left - inset.right, b.h - inset.top - inset.bottom}, Radii(radius));
    }
};

} // namespace

int Frame::thickness() {
    return std::clamp(static_cast<int>(Config::get().value<double>("/statusBar/frame/thickness", 10)), 2, 32);
}

Frame::Frame(wl::Output* output) : m_output(output) {
    rebuild();
}

Frame::~Frame() = default;

void Frame::rebuild() {
    const int t = thickness(), bar = Bar::thickness();
    const BarEdge edge = barEdge();
    if (!m_surface) {
        LayerSurface::Options o;
        o.ns = "material3ui-frame";
        // Under the windows, over the wallpaper: windows keep inside it, and
        // a floating one dragged onto it stays on top
        o.layer = LayerSurface::Layer::Bottom;
        o.anchor = LayerSurface::Top | LayerSurface::Bottom | LayerSurface::Left | LayerSurface::Right;
        o.exclusiveZone = -1; // the whole output, whatever keeps its edges
        m_surface = std::make_unique<LayerSurface>(m_output, o);
        m_surface->setInputRegion(std::vector<RectF>{}); // clicks reach the desktop
    }
    auto shape = std::make_unique<FrameShape>();
    auto side = [&](BarEdge e) { return static_cast<float>(e == edge ? bar : t); };
    shape->inset = {side(BarEdge::Left), side(BarEdge::Top), side(BarEdge::Right), side(BarEdge::Bottom)};
    shape->radius = static_cast<float>(std::clamp(Config::get().value<double>("/statusBar/frame/rounding", 25), 0.0, 60.0));
    m_surface->setRoot(std::move(shape));

    // The thin edges' reserved space: made again only when it changes, as
    // windows rearrange whenever it does
    const std::string key = std::format("{} {}", static_cast<int>(edge), t);
    if (key == m_zonesFor) return;
    m_zonesFor = key;
    m_zones.clear();
    for (const BarEdge e : {BarEdge::Top, BarEdge::Bottom, BarEdge::Left, BarEdge::Right}) {
        if (e == edge) continue; // the bar keeps its own
        LayerSurface::Options o;
        o.ns = "material3ui-frame-edge";
        o.layer = LayerSurface::Layer::Bottom;
        const bool across = e == BarEdge::Top || e == BarEdge::Bottom;
        o.anchor = (e == BarEdge::Top      ? LayerSurface::Top
                    : e == BarEdge::Bottom ? LayerSurface::Bottom
                    : e == BarEdge::Left   ? LayerSurface::Left
                                           : LayerSurface::Right) |
                   (across ? LayerSurface::Left | LayerSurface::Right : LayerSurface::Top | LayerSurface::Bottom);
        (across ? o.height : o.width) = t;
        o.exclusiveZone = t;
        auto zone = std::make_unique<LayerSurface>(m_output, o);
        zone->setInputRegion(std::vector<RectF>{});
        zone->setRoot(std::make_unique<Flex>()); // nothing to draw
        zone->setVisible(m_visible);
        m_zones.push_back(std::move(zone));
    }
}

void Frame::setVisible(bool visible) {
    m_visible = visible;
    m_surface->setVisible(visible);
    for (auto& z : m_zones) z->setVisible(visible);
}

} // namespace m3::shell
