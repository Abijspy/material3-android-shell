#include "shell/bar/Bar.h"

#include "core/Config.h"
#include "shell/bar/Island.h"
#include "shell/bar/Widgets.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

Bar::Bar(wl::Output* output, OpenPopup openPopup) : m_output(output), m_openPopup(std::move(openPopup)) {
    LayerSurface::Options opts;
    opts.ns = "material3ui-bar";
    opts.layer = LayerSurface::Layer::Top;
    opts.anchor = anchor();
    opts.height = height();
    opts.exclusiveZone = height();
    m_surface = std::make_unique<LayerSurface>(output, opts);
    rebuild();
}

uint32_t Bar::anchor() {
    const bool bottom = Config::get().value<std::string>("/statusBar/position", "top") == "bottom";
    return (bottom ? LayerSurface::Bottom : LayerSurface::Top) | LayerSurface::Left | LayerSurface::Right;
}

int Bar::height() const {
    return static_cast<int>(Config::get().value<double>("/statusBar/height", 40));
}

void Bar::rebuild() {
    // backgroundStyle 0 floats each module in its own capsule; anything else
    // draws one bar-wide surface
    const bool floating = Config::get().value<int>("/statusBar/backgroundStyle", 0) == 0;
    auto module = [](const char* name) { return Config::get().value<bool>(std::string("/statusBar/modules/") + name, true); };
    const float alpha = Theme::get().barOpacity();

    auto root = std::make_unique<BarLayout>();
    auto* bg = root.get();

    auto* start = bg->add<Flex>(Flex::Direction::Row);
    start->setSpacing(8);
    if (module("workspaces")) {
        auto* ws = start->add<Workspaces>(m_output->name);
        if (floating) {
            pill(ws->setBackground(ColorRef("surface_container_high", alpha), false));
            ws->blurBehind = true;
        }
    }
    if (module("title")) capsule(start, floating)->add<WindowTitle>();

    // The dynamic island is its own pill
    bg->add<Island>(m_openPopup);

    auto* end = bg->add<Flex>(Flex::Direction::Row);
    end->setSpacing(8);
    if (module("tray")) {
        auto* trayCap = capsule(end, floating);
        trayCap->setPadding({4, 0});
        trayCap->add<TrayIcons>(m_openPopup);
    }
    // Status icons open Quick Settings: always there
    auto* statusCap = capsule(end, floating);
    statusCap->setPadding({4, 0});
    statusCap->add<StatusIcons>(m_openPopup);
    if (module("battery")) capsule(end, floating)->add<BatteryStatus>();

    auto* surface = m_surface.get();
    if (!floating) {
        // Full background: paint it under everything via a backdrop node
        class Backdrop : public Node {
        protected:
            void doPaint(Renderer& r) override {
                r.rect(rect(), ColorRef("surface_container", Theme::get().barOpacity()).resolve());
            }
        };
        auto wrapper = std::make_unique<Node>();
        wrapper->add<Backdrop>()->blurBehind = true;
        wrapper->adopt(std::move(root));
        surface->setRoot(std::move(wrapper));
    } else {
        surface->setRoot(std::move(root));
    }
    m_surface->setAnchor(anchor());
    m_surface->setSize(0, height());
    m_surface->setExclusiveZone(height());
}

void Bar::setVisible(bool visible) {
    m_surface->setVisible(visible);
}

} // namespace m3::shell
