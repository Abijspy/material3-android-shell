#include "shell/PanelKit.h"

#include "core/EventLoop.h"

#include <linux/input-event-codes.h>

namespace m3::shell::kit {

using namespace m3::ui;

Live::Live() : Flex(Direction::Column) {
    setAlignItems(Align::Stretch)->setSpacing(12);
}

Live::~Live() {
    *m_alive = false;
}

void Live::watch(Signal<>& s) {
    conns.push_back(s.connect([this] { syncSoon(); }));
}

void Live::syncSoon() {
    if (m_pending) return;
    m_pending = true;
    EventLoop::get().defer([this, alive = m_alive] {
        if (!*alive) return;
        m_pending = false;
        if (sync) sync();
    });
}

Flex* header(Node* parent, const std::string& icon, const std::string& title) {
    auto* row = parent->add<Flex>(Flex::Direction::Row);
    row->setSpacing(12);
    row->add<Icon>(icon, 24)->setColor("primary");
    auto* t = row->add<Label>(title);
    t->setSize(20)->setWeight(500);
    t->grow = true;
    return row;
}

Label* caption(Node* parent, const std::string& text) {
    auto* l = parent->add<Label>(text);
    l->setSize(13)->setWeight(500)->setColor("on_surface_variant", false);
    l->alignSelf = Align::Start;
    return l;
}

Meter::Meter(float value, ColorRef color, float height)
    : m_value(std::clamp(value, 0.0f, 1.0f)), m_height(height), m_color(std::move(color)) {}

void Meter::doPaint(Renderer& r) {
    const Radii round(m_height / 2);
    r.rect(rect(), ColorRef("surface_container_highest").resolve(), round);
    RectF fill = rect();
    fill.w *= m_value;
    if (fill.w > 0) r.rect(fill, m_color.resolve(), round);
}

Button* iconButton(Node* parent, const std::string& icon, std::function<void()> onClick, float size) {
    auto* b = parent->add<Button>();
    pill(b)->setPadding(Insets(8));
    b->add<Icon>(icon, size)->setColor("on_surface_variant", false);
    b->onClicked = [onClick = std::move(onClick)](uint32_t button) {
        if (button == BTN_LEFT && onClick) onClick();
    };
    return b;
}

Tile::Tile(const std::string& icon, const std::string& title) : Button(Direction::Row) {
    setPadding({16, 12})->setSpacing(12)->setRadius(Radii(20));
    setBackground("surface_container_high", false);
    minHeight = 64;
    grow = true;
    m_icon = add<Icon>(icon, 22);
    auto* texts = add<Flex>(Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(1);
    texts->grow = true;
    m_title = texts->add<Label>(title);
    m_title->setSize(14)->setWeight(500)->setMaxWidth(120);
    m_status = texts->add<Label>();
    m_status->setSize(12)->setMaxWidth(120);
    set(false, "");
}

void Tile::enableDetails() {
    if (m_chevron) return;
    auto* b = add<Button>();
    pill(b)->setPadding(Insets(4));
    m_chevron = b->add<Icon>("chevron_right", 20);
    b->onClicked = [this](uint32_t) {
        if (onDetails) onDetails();
    };
    set(m_active, m_status->text());
}

void Tile::set(bool active, const std::string& status) {
    m_active = active;
    setBackground(active ? "primary_container" : "surface_container_high");
    const char* fg = active ? "on_primary_container" : "on_surface";
    m_icon->setColor(active ? "on_primary_container" : "on_surface_variant");
    m_icon->setFill(active);
    m_title->setColor(fg);
    m_status->setText(status);
    m_status->setColor(active ? "on_primary_container" : "on_surface_variant");
    if (m_chevron) m_chevron->setColor(fg);
}

} // namespace m3::shell::kit
