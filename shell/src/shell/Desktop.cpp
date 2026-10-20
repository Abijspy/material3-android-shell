#include "shell/Desktop.h"

#include "shell/DesktopEditor.h"
#include "shell/DesktopItems.h"

#include "core/Config.h"
#include "shell/bar/Widgets.h"
#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Wallpaper.h"
#include "ui/Theme.h"

#include <cmath>
#include <functional>
#include <linux/input-event-codes.h>
#include <ctime>
#include <random>

namespace m3::shell {

using namespace m3::ui;

namespace {

// Draws the wallpaper; on change the new picture is decoded off-thread and
// revealed over the old one
class WallpaperNode : public Node {
public:
    WallpaperNode() {
        m_current = Wallpaper::get().path();
        m_conn = Wallpaper::get().changed.connect([this] { change(Wallpaper::get().path()); });
    }

protected:
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        const int w = static_cast<int>(std::lround(b.w * r.scale())), h = static_cast<int>(std::lround(b.h * r.scale()));
        r.rect(b, Theme::get().color("surface_container_lowest"));
        const auto* next = m_next.empty() ? nullptr : Images::get().picture(m_next, w, h, true, [this] { started(); });
        const auto* cur = m_current.empty() ? nullptr : Images::get().picture(m_current, w, h, true, [this] { requestRedraw(); });
        if (cur) draw(r, cur);
        if (next && m_started) {
            const float t = m_t.value();
            if (m_style == 0) {
                r.pushOpacity(t);
                draw(r, next);
                r.popOpacity();
            } else {
                // Wipe from the left, or grow from the centre, as a clip
                const RectF clip = m_style == 1 ? RectF{b.x, b.y, b.w * t, b.h}
                                                : RectF{b.x + b.w * (1 - t) / 2, b.y + b.h * (1 - t) / 2, b.w * t, b.h * t};
                r.pushClip(clip);
                draw(r, next);
                r.popClip();
            }
            if (m_t.running()) {
                requestRedraw();
            } else {
                m_current = m_next;
                m_next.clear();
                m_started = false;
            }
        }
    }
    bool doAnimating() const override { return m_t.running(); }

private:
    void draw(Renderer& r, const Images::Texture* t) {
        const auto& b = rect();
        const float s = std::max(b.w / static_cast<float>(t->width), b.h / static_cast<float>(t->height));
        const float dw = static_cast<float>(t->width) * s, dh = static_cast<float>(t->height) * s;
        r.image(t->id, {b.x + (b.w - dw) / 2, b.y + (b.h - dh) / 2, dw, dh});
    }

    void change(const std::string& path) {
        if (path == m_current || path == m_next) return;
        if (!m_next.empty()) m_current = m_next; // skip an unfinished transition
        m_next = path;
        m_started = false;
        const auto kind = Config::get().value<std::string>("/background/wallpaperTransition", "random");
        static std::mt19937 rng{std::random_device{}()};
        m_style = kind == "fade" ? 0 : kind == "wipe" ? 1 : kind == "grow" ? 2 : static_cast<int>(rng() % 3);
        requestRedraw(); // starts the decode
    }

    void started() {
        m_started = true;
        m_t = Tween(0, motion::longMs + 200, Easing::emphasizedDecelerate());
        m_t.set(1);
        requestRedraw();
    }

    std::string m_current, m_next;
    bool m_started = false;
    int m_style = 0;
    Tween m_t{0, motion::longMs, Easing::emphasizedDecelerate()};
    Connection m_conn;
};

// The desktop's empty space: a right click opens its menu
class Backdrop : public Node {
public:
    std::function<void(float x, float y)> onMenu;
    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float y) override {
        if (button == BTN_RIGHT && onMenu) onMenu(x, y);
    }
};

} // namespace

Desktop::Desktop(wl::Output* output) {
    wl::LayerSurface::Options o;
    o.ns = "material3ui-wallpaper";
    o.layer = wl::LayerSurface::Layer::Background;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1; // under the bar too
    m_surface = std::make_unique<wl::LayerSurface>(output, o);
    m_surface->inlineBlur = true; // under the widgets is its own wallpaper
    auto root = std::make_unique<Node>();
    root->add<WallpaperNode>();
    // Its empty space takes the right click (the menu); the items above it
    // take their own clicks. Windows cover the rest as ever.
    auto* backdrop = root->add<Backdrop>();
    backdrop->onMenu = [output](float x, float y) {
        EventLoop::get().defer([output, x, y] { desktopeditor::menu(output, x, y); });
    };
    m_board = root->add<desktop::Board>();
    m_surface->setRoot(std::move(root));
    // While the editor shows them as cards, these step aside
    m_editing = desktopeditor::changed().connect([this] { m_board->setVisible(!desktopeditor::isOpen()); });
    fill();
    // Edited in the desktop editor (or Settings): rebuilt when they change
    m_config = Config::get().changed.connect([this] {
        const auto now = Config::get().value<nlohmann::json>("/desktop/items", nlohmann::json()).dump();
        if (now != m_items) fill();
    });
}

void Desktop::fill() {
    m_items = Config::get().value<nlohmann::json>("/desktop/items", nlohmann::json()).dump();
    m_board->clearChildren();
    m_board->spots.clear();
    for (const auto& item : desktop::load()) {
        desktop::build(m_board, item, false);
        m_board->spots.push_back({item.x, item.y});
    }
    m_board->requestLayout();
}

} // namespace m3::shell
