#include "shell/bar/Status.h"

#include "shell/bar/Widgets.h"

#include "render/Images.h"
#include "services/Audio.h"
#include "services/Mpris.h"
#include "shell/PanelKit.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Tray.h"
#include "ui/Theme.h"

#include <cmath>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

namespace {

std::string volumeIcon() {
    const auto* s = Audio::get().sink();
    if (!s) return "volume_off";
    if (s->muted || s->volume() <= 0.001f) return "volume_off";
    return s->volume() < 0.34f ? "volume_mute" : s->volume() < 0.67f ? "volume_down" : "volume_up";
}

// A tray icon: draws the item's pixmap or themed icon, re-rendered at the
// surface scale
class TrayIcon : public Button {
public:
    TrayIcon(Tray::Item item, OpenPopup open) : m_item(std::move(item)) {
        pill(this);
        tooltip = m_item.title.empty() ? m_item.id : m_item.title;
        fixedWidth = 28;
        fixedHeight = 28;
        onClicked = [this, open](uint32_t b) {
            const int x = static_cast<int>(rect().x), y = static_cast<int>(rect().bottom());
            if (b == BTN_LEFT) Tray::get().activate(m_item.key, x, y);
            else if (b == BTN_MIDDLE) Tray::get().secondaryActivate(m_item.key, x, y);
            else if (b == BTN_RIGHT && !m_item.menuPath.empty()) open("traymenu:" + m_item.key, alongBar(this));
            else if (b == BTN_RIGHT) Tray::get().contextMenu(m_item.key, x, y);
        };
        onScrolled = [this](int d) { Tray::get().scroll(m_item.key, d * 120); };
    }

protected:
    void doPaint(Renderer& r) override {
        Button::doPaint(r);
        const int px = static_cast<int>(std::lround(18 * r.scale()));
        const Images::Texture* tex = nullptr;
        if (!m_item.pixmap.empty())
            tex = Images::get().pixmap(m_item.key + '#' + std::to_string(m_item.revision), m_item.pixmapW,
                                       m_item.pixmapH, m_item.pixmap);
        if (!tex) tex = Images::get().icon(m_item.iconName, px, m_item.iconThemePath);
        const auto& b = rect();
        if (tex) r.image(tex->id, {b.x + (b.w - 18) / 2, b.y + (b.h - 18) / 2, 18, 18});
        else r.rect({b.x + 9, b.y + 9, 10, 10}, Theme::get().color("on_surface_variant"), Radii(5));
    }

private:
    Tray::Item m_item;
};

} // namespace

// ── StatusIcons ──

StatusIcons::StatusIcons(OpenPopup open, bool vertical) : Flex(vertical ? Direction::Column : Direction::Row) {
    setSpacing(2);
    auto make = [&](const char* popup, Icon*& icon) {
        auto* b = add<Button>();
        if (vertical) {
            pill(b)->setPadding({0, 6})->setJustify(Align::Center);
            b->fixedWidth = 28;
        } else {
            pill(b)->setPadding({6, 0});
            b->fixedHeight = 28;
        }
        icon = b->add<Icon>("", 18);
        b->onClicked = [this, b, open, popup](uint32_t button) {
            if (button == BTN_LEFT) open(popup, alongBar(b));
        };
        return b;
    };
    m_net = make("quicksettings", m_netIcon);
    m_bt = make("quicksettings", m_btIcon);
    m_vol = make("audio", m_volIcon);
    m_bell = make("notifications", m_bellIcon);
    m_vol->onScrolled = [](int d) { Audio::get().stepVolume(-0.05f * static_cast<float>(d)); };
    m_vol->onClicked = [this, open](uint32_t button) {
        if (button == BTN_LEFT) open("audio", alongBar(m_vol));
        if (button == BTN_MIDDLE) Audio::get().toggleMuted();
    };
    m_a = Network::get().changed.connect([this] { sync(); });
    m_b = Bluetooth::get().changed.connect([this] { sync(); });
    m_c = Audio::get().changed.connect([this] { sync(); });
    m_d = Notifications::get().changed.connect([this] { sync(); });
    sync();
}

void StatusIcons::sync() {
    m_netIcon->setText(Network::get().icon());
    m_net->setVisible(Network::get().available());
    m_btIcon->setText(Bluetooth::get().icon());
    m_bt->setVisible(Bluetooth::get().available());
    m_volIcon->setText(volumeIcon());
    m_vol->setVisible(Audio::get().ready());
    auto& n = Notifications::get();
    m_bellIcon->setText(n.doNotDisturb() ? "notifications_off" : n.current().empty() ? "notifications" : "notifications_unread");
    m_bellIcon->setFill(!n.current().empty());
    // What each says, for its tooltip
    auto& net = Network::get();
    m_net->setTooltip(net.kind() == Network::Kind::None ? "Not connected" : net.name().empty() ? "Connected" : net.name());
    m_bt->setTooltip(Bluetooth::get().powered() ? "Bluetooth on" : "Bluetooth off");
    const auto* sink = Audio::get().sink();
    m_vol->setTooltip(!sink ? "Sound" : sink->muted ? "Muted" : std::format("Volume {}%", std::lround(sink->volume() * 100)));
    m_bell->setTooltip(n.doNotDisturb() ? "Do not disturb" : n.current().empty() ? "No notifications"
                       : std::format("{} notification{}", n.current().size(), n.current().size() == 1 ? "" : "s"));
}

// ── BatteryStatus ──

BatteryStatus::BatteryStatus(OpenPopup open, bool vertical) : Button(vertical ? Direction::Column : Direction::Row), m_vertical(vertical) {
    if (vertical) {
        // The icon over its percentage, small enough for the bar's width
        pill(this)->setPadding({0, 8})->setSpacing(2);
        fixedWidth = 28;
    } else {
        pill(this)->setPadding({10, 0})->setSpacing(4);
        fixedHeight = 28;
    }
    onClicked = [this, open = std::move(open)](uint32_t button) {
        if (button == BTN_LEFT) open("battery", alongBar(this));
    };
    m_icon = add<Icon>("battery_full", 18);
    m_label = add<Label>();
    m_label->setSize(vertical ? 11 : 13)->setWeight(500);
    m_conn = Power::get().changed.connect([this] { sync(); });
    sync();
}

void BatteryStatus::sync() {
    auto& p = Power::get();
    setVisible(p.hasBattery());
    m_icon->setText(p.icon());
    m_icon->setColor(p.percent() <= 15 && !p.charging() ? ColorRef("error") : ColorRef("on_surface"));
    m_label->setText(std::to_string(p.percent()) + (m_vertical ? "" : "%"));
    setTooltip(std::format("Battery {}% · {}", p.percent(), p.status()));
}

// ── Media ──

Media::Media(bool vertical) : Button(Direction::Row) {
    pill(this)->setPadding({vertical ? 1.0f : 10.0f, 0})->setSpacing(6);
    fixedHeight = 28;
    // The play state inside a ring of the track's progress
    m_icon = add<kit::WavyRing>(26)->add<Icon>("music_note", 14);
    m_icon->setColor("primary");
    m_label = add<Label>();
    m_label->setSize(13)->setMaxWidth(240);
    m_label->setVisible(!vertical);
    onClicked = [](uint32_t b) {
        if (b == BTN_LEFT) Mpris::get().playPause();
        else if (b == BTN_RIGHT) Mpris::get().next();
    };
    onScrolled = [](int d) { d > 0 ? Mpris::get().next() : Mpris::get().previous(); };
    m_conn = Mpris::get().changed.connect([this] { sync(); });
    sync();
}

void Media::sync() {
    const auto* p = Mpris::get().active();
    setVisible(p && !p->title.empty());
    if (!p) return;
    m_icon->setText(p->status == "Playing" ? "graphic_eq" : "pause");
    m_label->setText(p->artist.empty() ? p->title : p->title + " · " + p->artist);
    setTooltip(m_label->text());
}

// ── TrayIcons ──

TrayIcons::TrayIcons(OpenPopup open, bool vertical) : Flex(vertical ? Direction::Column : Direction::Row), m_open(std::move(open)) {
    setSpacing(2);
    collapseWhenEmpty = true;
    m_conn = Tray::get().changed.connect([this] { sync(); });
    sync();
}

void TrayIcons::sync() {
    clearChildren();
    for (const auto& item : Tray::get().items())
        if (item.status != "Passive") add<TrayIcon>(item, m_open);
}

} // namespace m3::shell
