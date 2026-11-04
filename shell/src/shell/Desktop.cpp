#include "shell/Desktop.h"

#include "shell/DesktopEditor.h"
#include "shell/DesktopItems.h"

#include "core/Config.h"
#include "shell/bar/Widgets.h"
#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Niri.h"
#include "services/NiriConfig.h"
#include "services/Wallpaper.h"
#include "shell/LiveWallpaper.h"
#include "ui/Theme.h"

#include <GLES2/gl2.h>
#include <cmath>
#include <memory>
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
        // A live wallpaper switched, changed or paused
        m_live = LiveWallpaper::get().changed.connect([this] {
            if (!LiveWallpaper::get().on() || (m_video && m_videoPath != LiveWallpaper::get().video())) m_video.reset();
            if (m_video) m_video->pause(LiveWallpaper::get().paused());
            requestRedraw();
        });
    }
    ~WallpaperNode() override {
        if (m_tex) glDeleteTextures(1, &m_tex);
    }

protected:
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        const int w = static_cast<int>(std::lround(b.w * r.scale())), h = static_cast<int>(std::lround(b.h * r.scale()));
        if (paintLive(r, w, h)) return;
        r.rect(b, Theme::get().color("surface_container_lowest"));
        const auto* next = m_next.empty() ? nullptr : Images::get().picture(m_next, w, h, true, [this] { started(); });
        const auto* cur = m_current.empty() ? nullptr : Images::get().picture(m_current, w, h, true, [this] { requestRedraw(); });
        if (cur) draw(r, cur);
        // Already decoded (shown before): the cache has no decode to report,
        // so the transition starts here
        if (next && !m_started) started();
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
    // The live wallpaper, at this screen's size in device pixels: its newest
    // frame, uploaded as it comes. False when off (the still one then)
    bool paintLive(Renderer& r, int w, int h) {
        auto& live = LiveWallpaper::get();
        if (!live.on() || w <= 0 || h <= 0) return false;
        if (!m_video || m_video->width() != w || m_video->height() != h) {
            m_video = std::make_unique<VideoStream>(live.video(), w, h);
            m_videoPath = live.video();
            m_video->pause(live.paused());
            m_video->onFrame = [this] { requestRedraw(); };
        }
        if (m_video->fresh()) {
            const auto& f = m_video->frame();
            if (!m_tex) {
                glGenTextures(1, &m_tex);
                glBindTexture(GL_TEXTURE_2D, m_tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glBindTexture(GL_TEXTURE_2D, m_tex);
            if (m_texW != w || m_texH != h) {
                // BGRA, as the image program takes Cairo's
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, f.data());
                m_texW = w;
                m_texH = h;
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, f.data());
            }
            m_video->drawn();
        }
        // Until the first frame: the still one under it (a frame of it)
        if (!m_tex || m_texW != w) return false;
        r.image(m_tex, rect());
        return true;
    }

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
    Connection m_conn, m_live;
    std::unique_ptr<VideoStream> m_video;
    std::string m_videoPath;
    GLuint m_tex = 0;
    int m_texW = 0, m_texH = 0;
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

Desktop::Desktop(wl::Output* output) : m_output(output) {
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
    syncBackdrop();
    // Edited in the desktop editor (or Settings): rebuilt when they change
    m_config = Config::get().changed.connect([this] {
        const auto now = Config::get().value<nlohmann::json>("/desktop/items", nlohmann::json()).dump();
        if (now != m_items) fill();
        syncBackdrop();
    });
}

void Desktop::syncBackdrop() {
    // Only while niri reads the shell's rules: else it would be a second
    // wallpaper over the first, hiding the widgets
    const bool want = Niri::get().available() && NiriConfig::blurredOverview() && NiriConfig::get().included();
    if (want == (m_backdrop != nullptr)) return;
    if (!want) {
        m_backdrop.reset();
        return;
    }
    wl::LayerSurface::Options o;
    o.ns = "material3ui-backdrop";
    o.layer = wl::LayerSurface::Layer::Background;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1;
    m_backdrop = std::make_unique<wl::LayerSurface>(m_output, o);
    m_backdrop->inlineBlur = true;
    m_backdrop->inlineBlurRadius = 56; // soft, like a frosted pane
    auto root = std::make_unique<Node>();
    root->add<WallpaperNode>();
    root->add<Node>()->blurBehind = true; // all of it
    m_backdrop->setRoot(std::move(root));
}

void Desktop::refresh() {
    fill();
    m_surface->requestLayout();
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
