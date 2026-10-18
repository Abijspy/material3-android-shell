#include "shell/Osd.h"

#include "services/Audio.h"
#include "services/Network.h"
#include "ui/Theme.h"

#include <cmath>
#include <format>

namespace m3::shell {

using namespace m3::ui;

// A thick progress track that eases to its value
class Osd::Bar : public Node {
public:
    Tween value{0, motion::shortMs, Easing::standard()};

protected:
    SizeF doMeasure(SizeF) override { return {180, 12}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        const float y = b.y + (b.h - 12) / 2;
        r.rect({b.x, y, b.w, 12}, Theme::get().color("secondary_container"), Radii(6));
        const float w = std::clamp(value.value(), 0.0f, 1.0f) * b.w;
        if (w > 0.5f) r.rect({b.x, y, std::max(12.0f, w), 12}, Theme::get().color("primary"), Radii(6));
        if (value.running()) requestRedraw();
    }
    bool doAnimating() const override { return value.running(); }
};

Osd::Osd(std::function<wl::Output*()> output) : m_output(std::move(output)) {
    m_audio = Audio::get().changed.connect([this] {
        const auto* s = Audio::get().sink();
        if (!s) return;
        const float v = s->volume();
        // The first snapshot (and switching devices) is not a user change
        const bool change = m_audioSeen && s->name == m_sink && (std::abs(v - m_volume) > 0.004f || s->muted != m_muted);
        m_audioSeen = true;
        m_sink = s->name;
        m_volume = v;
        m_muted = s->muted;
        if (!change) return;
        const std::string icon = s->muted ? "volume_off" : v < 0.34f ? "volume_mute" : v < 0.67f ? "volume_down" : "volume_up";
        show(icon, s->muted ? 0 : v, s->muted ? "Muted" : std::format("{}%", std::lround(v * 100)));
    });
    m_light = Brightness::get().changed.connect([this] {
        const float v = Brightness::get().value();
        if (m_brightness >= 0 && std::abs(v - m_brightness) > 0.001f)
            show(v < 0.4f ? "brightness_low" : v < 0.75f ? "brightness_medium" : "brightness_high", v,
                 std::format("{}%", std::lround(v * 100)));
        m_brightness = v;
    });
    m_brightness = Brightness::get().available() ? Brightness::get().value() : -1;
}

void Osd::show(const std::string& icon, float value, const std::string& label) {
    if (!m_surface) {
        wl::Output* out = m_output();
        if (!out) return;
        wl::LayerSurface::Options o;
        o.ns = "material3ui-osd";
        o.layer = wl::LayerSurface::Layer::Overlay;
        o.anchor = wl::LayerSurface::Bottom;
        o.width = 340;
        o.height = 72;
        o.marginBottom = 72;
        m_surface = std::make_unique<wl::LayerSurface>(out, o);
        m_surface->setInputRegion(std::vector<RectF>{});
        auto root = std::make_unique<Flex>(Flex::Direction::Row);
        root->setJustify(Align::Center);
        m_pill = root->add<Flex>(Flex::Direction::Row);
        pill(m_pill->setBackground("surface_container_high", false))->setPadding({20, 14})->setSpacing(14);
        m_icon = m_pill->add<Icon>(icon, 24);
        m_icon->setColor("primary");
        m_bar = m_pill->add<Bar>();
        m_bar->value.set(value, false);
        m_label = m_pill->add<Label>();
        m_label->setSize(14)->setWeight(500);
        m_label->fixedWidth = 48;
        m_pill->setOpacity(0, false);
        m_pill->setTranslateY(12, false);
        m_surface->setRoot(std::move(root));
    }
    if (m_destroy) EventLoop::get().cancel(m_destroy);
    m_destroy = 0;
    m_icon->setText(icon);
    m_bar->value.set(value);
    m_bar->requestRedraw();
    m_label->setText(label);
    m_pill->setOpacity(1);
    m_pill->setTranslateY(0);

    if (m_hide) EventLoop::get().cancel(m_hide);
    m_hide = EventLoop::get().addTimer(1500, [this] {
        m_hide = 0;
        m_pill->setOpacity(0);
        m_pill->setTranslateY(12);
        m_destroy = EventLoop::get().addTimer(ui::motion::mediumMs + 50, [this] {
            m_destroy = 0;
            m_surface.reset();
        });
    });
}

} // namespace m3::shell
