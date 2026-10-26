#include "shell/Ringer.h"

#include "core/EventLoop.h"
#include "render/Renderer.h"
#include "services/Alarms.h"
#include "shell/ClockFaces.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

constexpr float kWidth = 440, kMargin = 12;

// The icon in its circle, a ring rippling out of it (ambient motion, at
// the ambient rate)
class Pulse : public Flex {
public:
    explicit Pulse(const std::string& icon) : Flex(Direction::Row) {
        pill(setBackground("primary", false))->setJustify(Align::Center);
        fixedWidth = fixedHeight = 56;
        add<Icon>(icon, 28)->setFill(true)->setColor("on_primary", false);
    }

protected:
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        const double t = std::fmod(static_cast<double>(Alarms::nowMs()) / 1400.0, 1.0);
        const float grow = static_cast<float>(t) * 14, cx = b.x + b.w / 2, cy = b.y + b.h / 2, radius = b.w / 2 + grow;
        r.rect({cx - radius, cy - radius, radius * 2, radius * 2}, Theme::get().color("primary").withAlpha(0.35f * (1 - static_cast<float>(t))),
               Radii(radius));
        Flex::doPaint(r);
    }
    bool doAnimating() const override { return true; }
    bool doAmbient() const override { return !Flex::doAnimating(); }
};

Button* bigButton(Node* parent, const std::string& icon, const std::string& label, bool primary, std::function<void()> fn) {
    auto* b = parent->add<Button>(Flex::Direction::Row);
    pill(b->setBackground(primary ? "primary" : "secondary_container", false))->setPadding({18, 14})->setSpacing(8)->setJustify(Align::Center);
    b->pressedRadius = 14;
    b->grow = true;
    b->add<Icon>(icon, 22)->setColor(primary ? "on_primary" : "on_secondary_container", false);
    b->add<Label>(label)->setSize(15)->setWeight(600)->setColor(primary ? "on_primary" : "on_secondary_container", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT) fn();
    };
    return b;
}

class RingingCards : public Live {
public:
    explicit RingingCards(float width) : m_width(width) {
        setSpacing(10);
        fixedWidth = width;
        watch(Alarms::get().changed);
        sync = [this] { build(); };
        build();
    }

private:
    void build() {
        clearChildren();
        m_since.clear();
        const auto& ringing = Alarms::get().ringing();
        setVisible(!ringing.empty());
        for (const auto& r : ringing) card(r);
        if (ringing.empty()) m_ticker.reset();
        else if (!m_ticker) m_ticker = std::make_unique<SecondTicker>([this] { tick(); });
        tick();
    }
    void card(const Alarms::Ringing& r) {
        auto& al = Alarms::get();
        auto* c = add<Flex>(Flex::Direction::Column);
        c->setBackground(ColorRef("surface_container_high", 0.96f), false)->setRadius(Radii(28))->setPadding({20, 18})->setSpacing(16);
        c->setAlignItems(Align::Stretch);
        c->blurBehind = true;
        auto* top = c->add<Flex>(Flex::Direction::Row);
        top->setSpacing(16);
        top->add<Pulse>(r.timer ? "timer" : "alarm");
        auto* texts = top->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(0);
        texts->grow = true;
        std::string title;
        if (r.timer) {
            title = "Time's up";
        } else if (const auto* a = al.alarm(r.id)) {
            title = Alarms::timeText(a->hour, a->minute);
        }
        texts->add<Label>(r.timer ? "Timer" : "Alarm")->setSize(13)->setWeight(500)->setColor("primary", false);
        texts->add<Label>(title)->setSize(r.timer ? 26 : 34)->setWeight(r.timer ? 600 : 400)->setColor("on_surface", false);
        auto* sub = texts->add<Label>(r.label);
        sub->setSize(14)->setMaxWidth(m_width - 140)->setColor("on_surface_variant", false);
        sub->setVisible(!r.label.empty() || r.timer);
        if (r.timer) m_since.emplace_back(r.since, sub, r.label); // "−0:12", counting on

        auto* buttons = c->add<Flex>(Flex::Direction::Row);
        buttons->setSpacing(8);
        const std::string id = r.id;
        if (r.timer) bigButton(buttons, "more_time", "+1:00", false, [id] { Alarms::get().addTime(id, 60000); });
        else bigButton(buttons, "snooze", "Snooze", false, [id] { Alarms::get().snooze(id); });
        bigButton(buttons, "alarm_off", "Stop", true, [id] { Alarms::get().stop(id); });
    }
    void tick() {
        const std::time_t now = std::time(nullptr);
        for (const auto& [since, label, name] : m_since) {
            const std::string over = "−" + Alarms::countdown(std::max<int64_t>(0, now - since) * 1000);
            label->setText(name.empty() ? over : name + " · " + over);
        }
    }
    float m_width;
    std::vector<std::tuple<std::time_t, Label*, std::string>> m_since;
    std::unique_ptr<SecondTicker> m_ticker;
};

// Its children's rects, after each arrangement: the surface's input region
class Column : public Flex {
public:
    std::function<void()> onArranged;
    Column() : Flex(Direction::Column) {}

protected:
    void doArrange() override {
        Flex::doArrange();
        if (onArranged) onArranged();
    }
};

} // namespace

Node* addRingingCards(Node* parent, float width) {
    return parent->add<RingingCards>(width);
}

Ringer::Ringer(std::function<wl::Output*()> output) : m_output(std::move(output)) {
    m_conn = Alarms::get().changed.connect([this] { EventLoop::get().defer([this] { sync(); }); });
    sync();
}

Ringer::~Ringer() = default;

void Ringer::sync() {
    if (Alarms::get().ringing().empty()) {
        m_surface.reset();
        return;
    }
    if (m_surface) return; // the cards follow the service themselves
    wl::Output* out = m_output();
    if (!out) return;
    wl::LayerSurface::Options o;
    o.ns = "material3ui-alarm";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom; // centred across
    o.width = static_cast<int>(kWidth + kMargin * 2);
    m_surface = std::make_unique<wl::LayerSurface>(out, o);
    auto root = std::make_unique<Column>();
    root->setPadding(Insets(kMargin))->setAlignItems(Align::Center);
    auto* cards = addRingingCards(root.get(), kWidth);
    // In from above, as a heads-up notification
    cards->setOpacity(0, false);
    cards->setTranslateY(-40, false);
    cards->setOpacity(1);
    cards->setTranslateY(0);
    auto* raw = m_surface.get();
    root->onArranged = [raw, cards] { raw->setInputRegion(std::vector<RectF>{cards->rect()}); };
    m_surface->setInputRegion(std::vector<RectF>{});
    m_surface->setRoot(std::move(root));
}

} // namespace m3::shell
