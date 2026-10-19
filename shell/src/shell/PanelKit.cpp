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
    pill(this)->setPadding({10, 10, 18, 10})->setSpacing(12);
    setBackground(ColorRef("on_surface", 0.1f), false);
    pressedRadius = 18;
    minHeight = 64;
    grow = true;
    m_chip = add<Button>(Direction::Row);
    m_chip->setJustify(Align::Center);
    m_chip->fixedWidth = m_chip->fixedHeight = 44;
    m_chip->pressedRadius = 10;
    m_icon = m_chip->add<Icon>(icon, 22);
    auto* texts = add<Flex>(Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(0);
    texts->grow = true;
    m_title = texts->add<Label>(title);
    m_title->setSize(15)->setWeight(600)->setMaxWidth(112);
    m_status = texts->add<Label>();
    m_status->setSize(13)->setMaxWidth(112);
    // With details, the body opens them and the chip toggles
    onClicked = [this](uint32_t button) {
        if (button != BTN_LEFT) return;
        if (onDetails) onDetails();
        else if (onToggle) onToggle();
    };
    m_chip->onClicked = [this](uint32_t button) {
        if (button != BTN_LEFT) return;
        if (onToggle) onToggle();
    };
    set(false, "");
}

void Tile::set(bool active, const std::string& status, const std::string& icon) {
    m_active = active;
    const bool chipOnly = static_cast<bool>(onDetails);
    const bool filled = active && !chipOnly;
    // Off: a translucent tint, so the blurred desktop shows through
    setBackground(filled ? ColorRef("primary") : ColorRef("on_surface", 0.1f));
    // The chip: a circle while off, a rounded square while on
    m_chip->setRadius(Radii(active ? 14 : 22));
    m_chip->setBackground(active && chipOnly ? ColorRef("primary") : filled ? ColorRef::none() : ColorRef("on_surface", 0.1f));
    if (!icon.empty()) m_icon->setText(icon);
    m_icon->setFill(active)->setColor(active ? "on_primary" : "on_surface");
    m_title->setColor(filled ? "on_primary" : "on_surface");
    m_status->setText(status);
    m_status->setVisible(!status.empty());
    m_status->setColor(filled ? "on_primary" : "on_surface_variant");
}

void Tile::setSelected(bool selected) {
    setBorder(selected ? 3 : 0, "on_surface");
}

SmallTile::SmallTile(const std::string& icon, float width) : Button(Direction::Row) {
    pill(this)->setJustify(Align::Center);
    setBackground(ColorRef("on_surface", 0.1f), false);
    pressedRadius = 18;
    fixedWidth = width;
    fixedHeight = 64;
    m_icon = add<Icon>(icon, 24);
    set(false);
}

void SmallTile::set(bool active, const std::string& icon) {
    setBackground(active ? ColorRef("primary") : ColorRef("on_surface", 0.1f));
    if (!icon.empty()) m_icon->setText(icon);
    m_icon->setFill(active)->setColor(active ? "on_primary" : "on_surface");
}

void SmallTile::setSelected(bool selected) {
    setBorder(selected ? 3 : 0, "on_surface");
}

} // namespace m3::shell::kit
