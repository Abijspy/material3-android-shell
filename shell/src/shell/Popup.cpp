#include "shell/Popup.h"

#include <algorithm>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

namespace {

// Places the card under the anchor, clamped to the surface, and passes
// clicks outside it through as "empty" so the popup can close.
class PopupLayout : public Node {
public:
    float anchorX = -1;
    float margin = 8;
    bool centreV = false;
    bool fill = false;

protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doArrange() override {
        if (children().empty()) return;
        auto& card = children().front();
        const auto& r = rect();
        if (fill) {
            card->arrange({r.x + 32, r.y + 32, r.w - 64, r.h - 64});
            return;
        }
        const SizeF s = card->measure({r.w - margin * 2, r.h - margin * 2});
        float x = anchorX < 0 ? (r.w - s.w) / 2 : anchorX - s.w / 2;
        x = std::clamp(x, r.x + margin, std::max(r.x + margin, r.right() - margin - s.w));
        const float y = centreV ? r.y + (r.h - s.h) / 3 : r.y + margin;
        card->arrange({x, y, s.w, std::min(s.h, r.h - margin * 2)});
    }
};

} // namespace

Popup::Popup(wl::Output* output, Options options, const std::function<void(Flex*, Popup&)>& build)
    : m_output(output), m_options(std::move(options)) {
    LayerSurface::Options opts;
    opts.ns = "material3ui-popup";
    opts.layer = LayerSurface::Layer::Overlay;
    opts.anchor = LayerSurface::Top | LayerSurface::Bottom | LayerSurface::Left | LayerSurface::Right;
    opts.exclusiveZone = 0; // stay clear of the bar's zone
    opts.keyboard = LayerSurface::Keyboard::Exclusive;
    m_surface = std::make_unique<LayerSurface>(output, opts);

    auto root = std::make_unique<PopupLayout>();
    root->anchorX = m_options.anchorX;
    root->centreV = m_options.centredVertically;
    root->fill = m_options.fill;
    m_card = root->add<Flex>(Flex::Direction::Column);
    m_card->setPadding(Insets(20))->setSpacing(12)->setAlignItems(Align::Stretch);
    m_card->setBackground("surface_container", false)->setRadius(Radii(28));
    if (!m_options.fill) m_card->fixedWidth = m_options.width;
    // Enter: fade in while dropping a few pixels into place
    m_card->setOpacity(0, false);
    m_card->setTranslateY(-12, false);
    m_card->setOpacity(1);
    m_card->setTranslateY(0);

    build(m_card, *this);
    m_surface->setRoot(std::move(root));
    if (initialFocus) m_surface->setFocus(initialFocus);

    m_surface->onEmptyClick = [this](float, float) { close(); };
    m_surface->onKey = [this](const KeyEvent& e) {
        if (e.sym != XKB_KEY_Escape) return false;
        close();
        return true;
    };
}

Popup::~Popup() {
    if (m_closeTimer) EventLoop::get().cancel(m_closeTimer);
}

void Popup::close() {
    if (m_closing) return;
    m_closing = true;
    m_card->setOpacity(0);
    m_card->setTranslateY(-8);
    m_closeTimer = EventLoop::get().addTimer(ui::motion::mediumMs, [this] {
        m_closeTimer = 0;
        if (onClosed) onClosed(); // the owner destroys us here
    });
}

} // namespace m3::shell
