#include "shell/MonitorLayout.h"

#include "render/Renderer.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

MonitorLayout::MonitorLayout(std::vector<Screen> screens) : m_screens(std::move(screens)) {
    for (const auto& s : m_screens) {
        auto* card = add<Flex>(Flex::Direction::Column);
        card->setJustify(Align::Center)->setAlignItems(Align::Center)->setSpacing(2)->setRadius(Radii(14));
        card->setBackground(s.focused ? "primary_container" : "surface_container_highest", false);
        card->setBorder(2, ColorRef(s.focused ? "primary" : "outline_variant"));
        card->add<Label>(s.title)->setSize(14)->setWeight(600)->setColor(s.focused ? "on_primary_container" : "on_surface", false);
        card->add<Label>(s.detail)->setSize(12)->setColor(s.focused ? "on_primary_container" : "on_surface_variant", false);
        m_cards.push_back(card);
    }
}

MonitorLayout::View MonitorLayout::fit() const {
    if (m_drag >= 0) return m_frozen;
    const auto& b = rect();
    float minX = std::numeric_limits<float>::max(), minY = minX, maxX = -minX, maxY = -minX;
    for (const auto& s : m_screens) {
        minX = std::min(minX, s.x);
        minY = std::min(minY, s.y);
        maxX = std::max(maxX, s.x + s.w);
        maxY = std::max(maxY, s.y + s.h);
    }
    if (m_screens.empty()) return {1, b.x, b.y};
    const float pad = 28, w = maxX - minX, h = maxY - minY;
    // Room around the screens for dragging one past the others
    const float scale = std::min((b.w - pad * 2) / (w * 1.4f), (b.h - pad * 2) / (h * 1.2f));
    return {scale, b.x + (b.w - w * scale) / 2 - minX * scale, b.y + (b.h - h * scale) / 2 - minY * scale};
}

void MonitorLayout::doArrange() {
    const View v = fit();
    for (size_t i = 0; i < m_screens.size(); ++i) {
        const auto& s = m_screens[i];
        m_cards[i]->arrange({v.ox + s.x * v.scale + 2, v.oy + s.y * v.scale + 2, s.w * v.scale - 4, s.h * v.scale - 4});
    }
}

void MonitorLayout::doPaint(Renderer& r) {
    r.rect(rect(), Theme::get().color("surface_container_high"), Radii(24));
}

void MonitorLayout::onPress(uint32_t button, float x, float y) {
    if (button != BTN_LEFT) return;
    m_frozen = fit();
    for (size_t i = 0; i < m_cards.size(); ++i)
        if (m_cards[i]->rect().contains(x, y)) {
            m_drag = static_cast<int>(i);
            m_grabX = (x - m_frozen.ox) / m_frozen.scale - m_screens[i].x;
            m_grabY = (y - m_frozen.oy) / m_frozen.scale - m_screens[i].y;
        }
}

void MonitorLayout::onDrag(float x, float y) {
    if (m_drag < 0) return;
    auto& s = m_screens[static_cast<size_t>(m_drag)];
    float nx = (x - m_frozen.ox) / m_frozen.scale - m_grabX, ny = (y - m_frozen.oy) / m_frozen.scale - m_grabY;
    // Snap each edge to the others' edges within a few screen pixels
    const float reach = 24 / m_frozen.scale;
    for (size_t i = 0; i < m_screens.size(); ++i) {
        if (static_cast<int>(i) == m_drag) continue;
        const auto& o = m_screens[i];
        for (float edge : {o.x, o.x + o.w}) {
            if (std::abs(nx - edge) < reach) nx = edge;
            if (std::abs(nx + s.w - edge) < reach) nx = edge - s.w;
        }
        for (float edge : {o.y, o.y + o.h}) {
            if (std::abs(ny - edge) < reach) ny = edge;
            if (std::abs(ny + s.h - edge) < reach) ny = edge - s.h;
        }
    }
    s.x = std::round(nx);
    s.y = std::round(ny);
    requestLayout();
}

void MonitorLayout::onRelease() {
    if (m_drag < 0) return;
    const auto& s = m_screens[static_cast<size_t>(m_drag)];
    m_drag = -1;
    requestLayout();
    if (onMoved) onMoved(s.key, static_cast<int>(s.x), static_cast<int>(s.y));
}

} // namespace m3::shell
