#include "shell/bar/Bar.h"

#include "core/Config.h"
#include "shell/bar/Widgets.h"
#include "wayland/Display.h"

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

Bar::Bar(wl::Output* output) : m_output(output) {
    LayerSurface::Options opts;
    opts.ns = "material3ui-bar";
    opts.layer = LayerSurface::Layer::Top;
    opts.anchor = LayerSurface::Top | LayerSurface::Left | LayerSurface::Right;
    opts.height = height();
    opts.exclusiveZone = height();
    m_surface = std::make_unique<LayerSurface>(output, opts);
    rebuild();
}

int Bar::height() const {
    return static_cast<int>(Config::get().value<double>("/statusBar/height", 40));
}

void Bar::rebuild() {
    // backgroundStyle 0 floats each module in its own capsule; anything else
    // draws one bar-wide surface
    const bool floating = Config::get().value<int>("/statusBar/backgroundStyle", 0) == 0;

    auto root = std::make_unique<BarLayout>();
    auto* bg = root.get();

    auto* start = bg->add<Flex>(Flex::Direction::Row);
    start->setSpacing(8);
    auto* ws = start->add<Workspaces>(m_output->name);
    if (floating) pill(ws->setBackground("surface_container_high", false));
    auto* titleCap = capsule(start, floating);
    titleCap->add<WindowTitle>();

    auto* centre = capsule(bg, floating);
    centre->add<Clock>();

    auto* end = capsule(bg, floating);
    end->add<Battery>();

    auto* surface = m_surface.get();
    if (!floating) {
        // Full background: paint it under everything via a backdrop node
        class Backdrop : public Node {
        protected:
            void doPaint(Renderer& r) override { r.rect(rect(), ColorRef("surface_container").resolve()); }
        };
        auto wrapper = std::make_unique<Node>();
        wrapper->add<Backdrop>();
        wrapper->adopt(std::move(root));
        surface->setRoot(std::move(wrapper));
    } else {
        surface->setRoot(std::move(root));
    }
    m_surface->setSize(0, height());
    m_surface->setExclusiveZone(height());
}

void Bar::setVisible(bool visible) {
    m_surface->setVisible(visible);
}

} // namespace m3::shell
