#include "shell/Tooltip.h"

#include "ui/Node.h"
#include "wayland/LayerSurface.h"

#include <algorithm>
#include <cmath>

namespace m3::shell {

using namespace m3::ui;

Tooltip& Tooltip::get() {
    static Tooltip instance;
    return instance;
}

void Tooltip::hover(wl::Surface* surface, Node* node) {
    if (m_timer) EventLoop::get().cancel(m_timer);
    m_timer = 0;
    if (!node) return hide();
    // Copied now: the node may be rebuilt before the pause is over
    const std::string text = node->tooltip;
    const auto r = node->rect();
    // Shown at once when one is already up (moving along the dock), else
    // after a pause
    const int delay = m_surface ? 0 : 600;
    m_timer = EventLoop::get().addTimer(delay, [this, surface, text, r] {
        m_timer = 0;
        // Still a surface of ours?
        for (auto* s : wl::Surface::all())
            if (s == surface) return show(surface, text, r.x, r.y, r.w, r.h);
    });
}

void Tooltip::hide() {
    if (m_timer) EventLoop::get().cancel(m_timer);
    m_timer = 0;
    m_surface.reset();
}

void Tooltip::show(wl::Surface* source, const std::string& text, float x, float y, float w, float h) {
    auto* layer = dynamic_cast<wl::LayerSurface*>(source);
    if (!layer) return;
    const auto& o = layer->options();
    using A = wl::LayerSurface;
    // Only surfaces across the whole output (the bar, the dock) place it
    if (!(o.anchor & A::Left) || !(o.anchor & A::Right)) return;

    auto root = std::make_unique<Flex>(Flex::Direction::Row);
    root->setPadding({10, 5})->setRadius(Radii(6))->setBackground("inverse_surface", false);
    root->add<Label>(text)->setSize(12)->setWeight(500)->setColor("inverse_on_surface", false)->setMaxWidth(360);
    const SizeF size = root->measure({400, 100});
    const int tw = static_cast<int>(std::ceil(size.w)), th = static_cast<int>(std::ceil(size.h));
    // Centred on the item, kept on the screen
    const float width = source->size().w;
    const int left = o.marginLeft + static_cast<int>(std::clamp(x + w / 2 - static_cast<float>(tw) / 2, 4.0f,
                                                                 std::max(4.0f, width - static_cast<float>(tw) - 4)));
    const bool bottom = (o.anchor & A::Bottom) && !(o.anchor & A::Top);
    constexpr int gap = 6;

    wl::LayerSurface::Options opts;
    opts.ns = "material3ui-tooltip";
    opts.layer = wl::LayerSurface::Layer::Overlay;
    // From the screen's edge, not after the bar's reserved space
    opts.exclusiveZone = -1;
    opts.width = tw;
    opts.height = th;
    if (bottom) {
        // Above the dock: from the bottom edge, past the surface to the item's top
        opts.anchor = A::Bottom | A::Left;
        opts.marginBottom = o.marginBottom + static_cast<int>(std::lround(source->size().h - y)) + gap;
    } else {
        // Below the bar
        opts.anchor = A::Top | A::Left;
        opts.marginTop = o.marginTop + static_cast<int>(std::lround(y + h)) + gap;
    }
    opts.marginLeft = left;
    m_surface = std::make_unique<wl::LayerSurface>(source->output(), opts);
    m_surface->setInputRegion(std::vector<RectF>{}); // clicks pass through
    root->setOpacity(0, false);
    root->setOpacity(1);
    m_surface->setRoot(std::move(root));
}

} // namespace m3::shell
