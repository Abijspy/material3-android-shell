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
    m_node = node;
    const auto r = node->rect();
    // Shown at once when one is already up (moving along the dock), else
    // after a pause. The text is taken now: the node may be gone by then
    // (a rebuilt dock), and only its words are needed.
    const int delay = m_surface ? 0 : 600;
    m_timer = EventLoop::get().addTimer(delay, [this, surface, r, text = node->tooltip] {
        m_timer = 0;
        for (auto* s : wl::Surface::all())
            if (s == surface && m_node) return show(surface, text, r.x, r.y, r.w, r.h);
    });
}

void Tooltip::hide() {
    if (m_timer) EventLoop::get().cancel(m_timer);
    m_timer = 0;
    m_surface.reset();
    m_node = nullptr;
    m_root = nullptr;
    m_label = nullptr;
    m_source = nullptr;
}

void Tooltip::changed(Node* node) {
    if (node != m_node || !m_surface || !m_label) return;
    if (node->tooltip.empty()) return hide();
    m_label->setText(node->tooltip);
    place();
}

void Tooltip::place() {
    auto* layer = dynamic_cast<wl::LayerSurface*>(m_source);
    if (!layer || !m_root) return;
    const auto& o = layer->options();
    using A = wl::LayerSurface;
    const SizeF size = m_root->measure({400, 100});
    const int tw = static_cast<int>(std::ceil(size.w)), th = static_cast<int>(std::ceil(size.h));
    // Centred on the item, kept on the screen
    const float width = m_source->size().w;
    const int left = o.marginLeft + static_cast<int>(std::clamp(m_x + m_w / 2 - static_cast<float>(tw) / 2, 4.0f,
                                                                 std::max(4.0f, width - static_cast<float>(tw) - 4)));
    const bool bottom = (o.anchor & A::Bottom) && !(o.anchor & A::Top);
    constexpr int gap = 6;
    if (bottom) m_surface->setMargins(0, 0, o.marginBottom + static_cast<int>(std::lround(m_source->size().h - m_y)) + gap, left);
    else m_surface->setMargins(o.marginTop + static_cast<int>(std::lround(m_y + m_h)) + gap, 0, 0, left);
    m_surface->setSize(tw, th);
}

void Tooltip::show(wl::Surface* source, const std::string& text, float x, float y, float w, float h) {
    auto* layer = dynamic_cast<wl::LayerSurface*>(source);
    if (!layer || text.empty()) return;
    const auto& o = layer->options();
    using A = wl::LayerSurface;
    // Only surfaces across the whole output (the bar, the dock) place it
    if (!(o.anchor & A::Left) || !(o.anchor & A::Right)) return;
    m_source = source;
    m_x = x;
    m_y = y;
    m_w = w;
    m_h = h;

    auto root = std::make_unique<Flex>(Flex::Direction::Row);
    root->setPadding({10, 5})->setRadius(Radii(6))->setBackground("inverse_surface", false);
    m_label = root->add<Label>(text);
    m_label->setSize(12)->setWeight(500)->setColor("inverse_on_surface", false)->setMaxWidth(360);
    m_root = root.get();

    wl::LayerSurface::Options opts;
    opts.ns = "material3ui-tooltip";
    opts.layer = wl::LayerSurface::Layer::Overlay;
    // From the screen's edge, not after the bar's reserved space
    opts.exclusiveZone = -1;
    opts.width = 1;
    opts.height = 1;
    opts.anchor = ((o.anchor & A::Bottom) && !(o.anchor & A::Top) ? A::Bottom : A::Top) | A::Left;
    m_surface = std::make_unique<wl::LayerSurface>(source->output(), opts);
    m_surface->setInputRegion(std::vector<RectF>{}); // clicks pass through
    root->setOpacity(0, false);
    root->setOpacity(1);
    m_surface->setRoot(std::move(root));
    place();
}

} // namespace m3::shell
