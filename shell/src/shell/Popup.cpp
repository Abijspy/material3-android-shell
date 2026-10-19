#include "shell/Popup.h"

#include "core/Config.h"
#include "services/Hyprland.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <linux/input-event-codes.h>
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
    bool fromBottom = false; // the bar is on the bottom edge: drop up from it
    float bottomGap = 8;     // room kept under the card (the dock's, for its menus)

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
        const float h = std::min(s.h, r.h - margin * 2);
        // Centred with the clamped height: a card taller than the screen
        // must not start above its top edge
        const float y = centreV ? std::max(r.y + margin, r.y + (r.h - h) / 3) : fromBottom ? r.bottom() - bottomGap - h : r.y + margin;
        card->arrange({x, y, s.w, h});
    }
};

} // namespace

namespace {

// The title bar's empty stretch: pressing it moves the window, a double
// press maximises or restores it
class DragArea : public Node {
public:
    std::function<void()> onDrag, onDouble;
    bool interactive() const override { return true; }
    void onPress(uint32_t button, float, float) override {
        if (button != BTN_LEFT) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - m_last < std::chrono::milliseconds(400)) {
            m_last = {};
            if (onDouble) onDouble();
            return;
        }
        m_last = now;
        if (onDrag) onDrag();
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::chrono::steady_clock::time_point m_last;
};

} // namespace

Popup::Popup(wl::Output* output, Options options, const std::function<void(Flex*, Popup&)>& build)
    : m_output(output), m_options(std::move(options)) {
    if (m_options.window) {
        wl::Window::Options wo;
        wo.appId = m_options.appId;
        wo.title = m_options.title;
        wo.width = m_options.windowWidth;
        wo.height = m_options.windowHeight;
        wo.minWidth = 860;
        wo.minHeight = 560;
        m_window = std::make_unique<wl::Window>(wo);
        // The compositor rounds, borders and blurs the window: a plain,
        // translucent card that fills it
        auto root = std::make_unique<Flex>(Flex::Direction::Column);
        m_card = root.get();
        m_card->setAlignItems(Align::Stretch);
        m_card->setBackground(ColorRef("surface_container", Theme::get().panelOpacity()), false);
        m_theme = Theme::get().changed.connect([this] {
            m_card->setBackground(ColorRef("surface_container", Theme::get().panelOpacity()));
            m_card->requestLayout();
        });
        buildTitleBar(m_card);
        auto* body = m_card->add<Flex>(Flex::Direction::Column);
        body->setAlignItems(Align::Stretch);
        body->grow = true;
        build(body, *this);
        m_window->setRoot(std::move(root));
        if (initialFocus) m_window->setFocus(initialFocus);
        m_window->onCloseRequested = [this] { close(); };
        m_window->onStateChanged = [this] {
            if (m_maxIcon) m_maxIcon->setText(m_window->maximized() ? "filter_none" : "crop_square");
        };
        return;
    }
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
    root->fromBottom = m_options.aboveDock || Config::get().value<std::string>("/statusBar/position", "top") == "bottom";
    // The dock's height: its icons and the padding round them
    if (m_options.aboveDock) root->bottomGap = static_cast<float>(Config::get().value<double>("/dock/iconSize", 44)) + 52;
    m_drop = root->fromBottom && !m_options.centredVertically ? 1.0f : -1.0f;
    m_card = root->add<Flex>(Flex::Direction::Column);
    m_card->setPadding(Insets(20))->setSpacing(12)->setAlignItems(Align::Stretch);
    m_card->setBackground(ColorRef("surface_container", Theme::get().panelOpacity()), false)->setRadius(Radii(28));
    m_card->blurBehind = true;
    if (!m_options.fill && m_options.width > 0) m_card->fixedWidth = m_options.width;
    // Enter: fade in while dropping a few pixels into place
    m_card->setOpacity(0, false);
    m_card->setTranslateY(12 * m_drop, false);
    m_card->setOpacity(1);
    m_card->setTranslateY(0);

    // Opacity and roundness are live: Settings previews them on itself
    m_theme = Theme::get().changed.connect([this] {
        m_card->setBackground(ColorRef("surface_container", Theme::get().panelOpacity()));
        m_card->requestLayout();
    });

    build(m_card, *this);
    m_surface->setRoot(std::move(root));
    if (initialFocus) m_surface->setFocus(initialFocus);

    m_surface->onEmptyClick = [this](float, float) { close(); };
    m_surface->onKey = [this](const KeyEvent& e) {
        if (onKey && onKey(e)) return true;
        if (e.sym != XKB_KEY_Escape) return false;
        close();
        return true;
    };
}

Popup::~Popup() {
    *m_alive = false;
    if (m_closeTimer) EventLoop::get().cancel(m_closeTimer);
}

void Popup::buildTitleBar(Flex* card) {
    auto* bar = card->add<Flex>(Flex::Direction::Row);
    bar->setPadding({18, 8, 10, 4})->setSpacing(6);
    if (!m_options.icon.empty()) bar->add<Icon>(m_options.icon, 20)->setColor("primary", false);
    bar->add<Label>(m_options.title)->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    auto* drag = bar->add<DragArea>();
    drag->grow = true;
    drag->minHeight = 32;
    drag->onDrag = [this] { m_window->startMove(); };
    drag->onDouble = [this] { toggleMaximized(); };
    // Round tonal buttons, close last
    auto button = [&](const char* icon, std::function<void()> fn, const char* hover) -> Icon* {
        auto* b = bar->add<Button>();
        pill(b)->setPadding(Insets(7));
        b->pressedRadius = 8;
        b->setBackground("surface_container_highest", false);
        auto* i = b->add<Icon>(icon, 18);
        i->setColor(hover, false);
        b->onClicked = [fn = std::move(fn)](uint32_t button) {
            if (button == BTN_LEFT) fn();
        };
        return i;
    };
    button("remove", [this] { minimize(); }, "on_surface");
    m_maxIcon = button("crop_square", [this] { toggleMaximized(); }, "on_surface");
    button("close", [this] { close(); }, "error");
}

void Popup::minimize() {
    if (!m_window) return;
    // Hyprland has no minimised state: park the window on a hidden special
    // workspace; opening the app or its dock icon brings it back
    const std::string cls = "class:^(" + m_options.appId + ")$";
    Hyprland::get().dispatch("hl.dsp.window.move({ workspace = \"special:minimized\", window = \"" + cls + "\", silent = true })",
                             "movetoworkspacesilent special:minimized," + cls);
}

void Popup::toggleMaximized() {
    if (m_window) m_window->setMaximized(!m_window->maximized());
}

void Popup::dismiss() {
    if (m_window)
        minimize();
    else
        close();
}

void Popup::close() {
    if (m_closing) return;
    m_closing = true;
    if (m_window) {
        // Windows close at once; the compositor animates them out
        EventLoop::get().defer([this, alive = m_alive] {
            if (*alive && onClosed) onClosed(); // the owner destroys us here
        });
        return;
    }
    m_card->setOpacity(0);
    m_card->setTranslateY(8 * m_drop);
    m_closeTimer = EventLoop::get().addTimer(ui::motion::mediumMs, [this] {
        m_closeTimer = 0;
        if (onClosed) onClosed(); // the owner destroys us here
    });
}

} // namespace m3::shell
