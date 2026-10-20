#include "shell/Desktop.h"

#include "core/Config.h"
#include "shell/bar/Widgets.h"
#include "core/EventLoop.h"
#include "render/Images.h"
#include "services/Wallpaper.h"
#include "ui/Theme.h"

#include <cmath>
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

// A big clock and date on the desktop, Material You style
class DesktopClock : public Flex {
public:
    DesktopClock() : Flex(Direction::Column) {
        setAlignItems(Align::Start)->setSpacing(-6)->setPadding({72, 140});
        m_time = add<Label>();
        m_time->setSize(96)->setWeight(500)->setColor("primary", false);
        m_date = add<Label>();
        m_date->setSize(22)->setWeight(500)->setColor("on_surface", false);
        // A new format shows at once (Settings → Wallpaper & style)
        m_config = Config::get().changed.connect([this] {
            if (m_timer) EventLoop::get().cancel(m_timer);
            m_timer = 0;
            tick();
        });
        tick();
    }
    ~DesktopClock() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

protected:
    SizeF doMeasure(SizeF available) override {
        Flex::doMeasure(available);
        return available;
    }

private:
    void tick() {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        auto& c = Config::get();
        const auto time = c.value<std::string>("/desktop/timeFormat", "hh:mm");
        m_time->setText(Clock::sample(time));
        m_date->setText(Clock::sample(c.value<std::string>("/desktop/dateFormat", "dddd, d MMMM")));
        m_date->setVisible(c.value<bool>("/desktop/showDate", true));
        // Seconds tick every second, the rest on the minute
        const bool seconds = time.find('s') != std::string::npos;
        m_timer = EventLoop::get().addTimer(seconds ? 1000 : (60 - tm.tm_sec) * 1000 + 20, [this] {
            m_timer = 0;
            tick();
        });
    }
    Label *m_time, *m_date;
    EventLoop::TimerId m_timer = 0;
    Connection m_config;
};

} // namespace

Desktop::Desktop(wl::Output* output) {
    wl::LayerSurface::Options o;
    o.ns = "material3ui-wallpaper";
    o.layer = wl::LayerSurface::Layer::Background;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1; // under the bar too
    m_surface = std::make_unique<wl::LayerSurface>(output, o);
    auto root = std::make_unique<Node>();
    root->add<WallpaperNode>();
    if (Config::get().value<bool>("/desktop/clock", true)) root->add<DesktopClock>();
    m_surface->setInputRegion(std::vector<RectF>{}); // windows below get the clicks
    m_surface->setRoot(std::move(root));
}

} // namespace m3::shell
