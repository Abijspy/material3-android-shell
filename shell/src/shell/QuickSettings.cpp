#include "services/Audio.h"
#include "services/Idle.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

bool g_nightLight = false;

std::string nowTime() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", &tm);
    return buf;
}

std::string today() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, "%A, %-d %B", &tm);
    return buf;
}

// Speeds of the current connection: "5 GHz · 866 Mb/s"
std::string linkSummary() {
    const auto& d = Network::get().details();
    std::string out = d.band();
    if (auto c = out.find(" · channel"); c != std::string::npos) out = out.substr(0, c);
    if (d.linkRate > 0) out += (out.empty() ? "" : " · ") + std::format("{} Mb/s", d.linkRate);
    return out;
}

// Download and upload, side by side; returns the two value labels
std::pair<Label*, Label*> rates(Node* parent, float size) {
    auto* box = parent->add<Flex>(Flex::Direction::Row);
    box->setSpacing(10);
    std::pair<Label*, Label*> out;
    for (int i = 0; i < 2; ++i) {
        auto* r = box->add<Flex>(Flex::Direction::Row);
        r->setSpacing(2);
        r->add<Icon>(i == 0 ? "arrow_downward" : "arrow_upward", size + 2)->setColor(i == 0 ? "primary" : "tertiary", false);
        auto* l = r->add<Label>("–");
        l->setSize(size)->setWeight(500)->setFamily(Theme::get().fontFamily());
        l->minWidth = size * 4.6f; // steady width as digits change
        (i == 0 ? out.first : out.second) = l;
    }
    return out;
}

// Inside the connected network's entry: link, live speeds and address,
// refreshed every second without rebuilding the list around it
void connectedInfo(Flex* parent, const char* fg) {
    auto* live = parent->add<Live>();
    live->setSpacing(6);
    auto* link = live->add<Label>();
    link->setSize(12)->setColor(fg, false)->setMaxWidth(340)->setMaxLines(2);
    auto [down, up] = rates(live, 13);
    for (Label* l : {down, up}) l->setColor(fg, false);
    auto* addr = live->add<Label>();
    addr->setSize(12)->setColor(fg, false)->setMaxWidth(340);
    auto lease = Network::get().watchTraffic();
    live->sync = [=] {
        (void)lease; // sampling runs while the entry is shown
        auto& n = Network::get();
        const auto& d = n.details();
        std::string sub = n.kind() == Network::Kind::Wifi ? std::format("Signal {}%", n.strength()) : "Connected";
        if (auto l = linkSummary(); !l.empty()) sub += " · " + l;
        if (!d.security.empty()) sub += " · " + d.security;
        link->setText(sub);
        down->setText(formatRate(d.rxRate));
        up->setText(formatRate(d.txRate));
        addr->setText(d.ipv4.empty() ? "Getting an address…" : "IP " + d.ipv4 + (d.gateway.empty() ? "" : "  ·  Gateway " + d.gateway));
    };
    live->watch(Network::get().changed);
    live->watch(Network::get().detailsChanged);
    live->sync();
}

} // namespace

std::string formatRate(double bps) {
    if (bps < 1000) return std::format("{:.0f} B/s", bps);
    if (bps < 1e6) return std::format("{:.0f} KB/s", bps / 1e3);
    if (bps < 1e9) return std::format("{:.1f} MB/s", bps / 1e6);
    return std::format("{:.2f} GB/s", bps / 1e9);
}

// Wi-Fi networks, tap to connect; secured new ones ask for the password
void wifiView(Flex* body, const std::function<void()>& back, bool details) {
    body->clearChildren();
    auto* live = body->add<Live>();
    auto* head = live->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8);
    if (back) iconButton(head, "arrow_back", back);
    auto* title = head->add<Label>("Wi-Fi");
    title->setSize(20)->setWeight(500);
    title->grow = true;
    iconButton(head, "refresh", [] { Network::get().scan(); });
    auto* sw = head->add<Switch>();
    sw->onToggled = [](bool on) { Network::get().setWifiEnabled(on); };

    auto* error = live->add<Label>();
    error->setSize(13)->setColor("error", false);
    auto* scroll = live->add<ScrollView>();
    scroll->maxHeight = 340;
    auto* list = scroll->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2);
    auto expanded = std::make_shared<std::string>(); // SSID asking for a password

    live->sync = [=] {
        auto& n = Network::get();
        sw->setChecked(n.wifiEnabled());
        error->setText(n.lastError());
        error->setVisible(!n.lastError().empty());
        list->clearChildren();
        if (!n.wifiEnabled()) {
            caption(list, "Wi-Fi is off");
            return;
        }
        if (n.accessPoints().empty()) caption(list, "Searching…");
        for (const auto& ap : n.accessPoints()) {
            auto* entry = list->add<Button>(Flex::Direction::Column);
            entry->setAlignItems(Align::Stretch)->setSpacing(8)->setRadius(Radii(16));
            entry->setPadding({14, 10});
            entry->pressedRadius = 10;
            auto* row = entry->add<Flex>(Flex::Direction::Row);
            row->setSpacing(12);
            if (ap.active) entry->setBackground("secondary_container", false);
            const char* bars = ap.strength >= 80 ? "signal_wifi_4_bar" : ap.strength >= 55 ? "network_wifi_3_bar"
                             : ap.strength >= 30 ? "network_wifi_2_bar" : "network_wifi_1_bar";
            row->add<Icon>(bars, 20)->setColor(ap.active ? "on_secondary_container" : "on_surface_variant", false);
            auto* texts = row->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start);
            texts->grow = true;
            texts->add<Label>(ap.ssid)->setSize(14)->setMaxWidth(240)->setColor(ap.active ? "on_secondary_container" : "on_surface", false);
            if (ap.active || ap.saved)
                texts->add<Label>(ap.active ? "Connected" : "Saved")->setSize(12)->setColor(ap.active ? "on_secondary_container" : "on_surface_variant", false);
            if (ap.secured) row->add<Icon>("lock", 16)->setColor("on_surface_variant", false);
            // The connected entry carries its own details and speeds
            if (ap.active && details) {
                auto* info = entry->add<Flex>(Flex::Direction::Column);
                info->setAlignItems(Align::Start)->setPadding({32, 0, 0, 2});
                connectedInfo(info, "on_secondary_container");
            }
            auto apCopy = ap;
            entry->onClicked = [=](uint32_t) {
                if (apCopy.active) return Network::get().disconnect();
                if (apCopy.secured && !apCopy.saved) {
                    *expanded = *expanded == apCopy.ssid ? "" : apCopy.ssid;
                    // Rebuilding destroys this row: not while its handler runs
                    live->syncSoon();
                    return;
                }
                Network::get().connect(apCopy);
            };
            if (*expanded == ap.ssid) {
                auto* form = list->add<Flex>(Flex::Direction::Row);
                form->setPadding({8, 4})->setSpacing(8);
                auto* pw = form->add<TextField>();
                pw->setSecret(true)->setPlaceholder("Password");
                pw->grow = true;
                pw->minWidth = 120;
                auto connect = [=](const std::string& text) {
                    if (text.empty()) return;
                    Network::get().connect(apCopy, text);
                    expanded->clear();
                };
                pw->onSubmit = connect;
                auto* go = form->add<Button>();
                pill(go->setBackground("primary", false))->setPadding({16, 10});
                go->add<Label>("Connect")->setSize(14)->setWeight(500)->setColor("on_primary", false);
                go->onClicked = [=](uint32_t) { connect(pw->text()); };
                pw->focus();
            }
        }
    };
    live->watch(Network::get().changed);
    live->watch(Network::get().accessPointsChanged);
    live->sync();
    Network::get().scan();
}

namespace {

void mainView(Flex* body, Popup& popup, const std::function<void()>& showWifi, const std::function<void()>& showBluetooth) {
    body->clearChildren();
    auto* live = body->add<Live>();

    // M3 Expressive, as Android 16/17's shade: a large clock and the date,
    // round tonal buttons on the right
    auto* head = live->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8)->setPadding({4, 0, 0, 4});
    auto* when = head->add<Flex>(Flex::Direction::Column);
    when->setAlignItems(Align::Start);
    when->grow = true;
    auto* clock = when->add<Label>(nowTime());
    clock->setSize(44)->setWeight(400);
    when->add<Label>(today())->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    auto roundButton = [&](const char* icon, std::function<void()> fn) {
        auto* b = head->add<Button>();
        pill(b->setBackground("surface_container_highest", false))->setPadding(Insets(11));
        b->pressedRadius = 12;
        b->add<Icon>(icon, 22)->setColor("on_surface", false);
        b->onClicked = [fn = std::move(fn)](uint32_t button) {
            if (button == BTN_LEFT) fn();
        };
    };
    roundButton("settings", [] {
        if (open) open("settings");
    });
    roundButton("lock", [&popup] {
        popup.close();
        if (lock) lock();
    });
    roundButton("power_settings_new", [] {
        if (open) open("power");
    });

    // Large sliders with their icon inside; the speaker icon mutes
    Slider* light = nullptr;
    if (Brightness::get().available()) {
        light = live->add<Slider>();
        light->setIcon("brightness_medium");
        light->setValue(Brightness::get().value(), false);
        light->onChanged = [](float v) { Brightness::get().set(v); };
    }
    auto* volume = live->add<Slider>();
    volume->setIcon("volume_up");
    volume->onChanged = [](float v) { Audio::get().setVolume(v); };
    volume->onIconClicked = [] { Audio::get().toggleMuted(); };

    // Two large tiles, then four small ones (card 440 − padding 40 = 400)
    auto row1 = live->add<Flex>(Flex::Direction::Row);
    row1->setSpacing(8)->setAlignItems(Align::Stretch);
    auto* wifi = row1->add<Tile>("wifi", "Wi-Fi");
    auto* bt = row1->add<Tile>("bluetooth", "Bluetooth");
    for (Tile* t : {wifi, bt}) {
        t->grow = false;
        t->fixedWidth = 196;
    }
    auto row2 = live->add<Flex>(Flex::Direction::Row);
    row2->setSpacing(8);
    constexpr float kSmall = (400 - 3 * 8) / 4.0f;
    auto* dnd = row2->add<SmallTile>("do_not_disturb_on", kSmall);
    auto* caffeine = row2->add<SmallTile>("coffee", kSmall);
    auto* night = row2->add<SmallTile>("nightlight", kSmall);
    auto* mic = row2->add<SmallTile>("mic", kSmall);

    wifi->enableDetails();
    wifi->onDetails = showWifi;
    wifi->onClicked = [](uint32_t) { Network::get().setWifiEnabled(!Network::get().wifiEnabled()); };
    bt->onClicked = [](uint32_t) { Bluetooth::get().setPowered(!Bluetooth::get().powered()); };
    bt->enableDetails();
    bt->onDetails = showBluetooth;
    dnd->onClicked = [](uint32_t) { Notifications::get().setDoNotDisturb(!Notifications::get().doNotDisturb()); };
    caffeine->onClicked = [](uint32_t) { Idle::get().setInhibited(!Idle::get().inhibited()); };
    night->onClicked = [live](uint32_t) {
        g_nightLight = !g_nightLight;
        spawn(g_nightLight ? "pkill -x hyprsunset; hyprsunset -t 4500" : "pkill -x hyprsunset");
        live->syncSoon();
    };
    mic->onClicked = [](uint32_t) { Audio::get().toggleMuted(true); };

    // Battery: charge, what it is doing, and the power mode
    auto* battery = live->add<Flex>(Flex::Direction::Row);
    battery->setPadding({20, 10, 12, 10})->setSpacing(12)->setRadius(Radii(28))->setBackground("surface_container_highest", false);
    auto* batIcon = battery->add<Icon>("battery_full", 22);
    auto* batText = battery->add<Flex>(Flex::Direction::Column);
    batText->setAlignItems(Align::Start);
    batText->grow = true;
    auto* batPercent = batText->add<Label>();
    batPercent->setSize(14)->setWeight(500);
    auto* batStatus = batText->add<Label>();
    batStatus->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(170);
    std::vector<std::pair<std::string, Button*>> modes;
    for (const auto& [profile, icon] : {std::pair{"power-saver", "eco"}, {"balanced", "balance"}, {"performance", "bolt"}})
        modes.push_back({profile, iconButton(battery, icon, [p = std::string(profile)] { Power::get().setProfile(p); })});

    // The connection at a glance, with live speeds; opens the Wi-Fi view
    auto* net = live->add<Button>(Flex::Direction::Row);
    net->setPadding({20, 12})->setSpacing(12)->setRadius(Radii(28))->setBackground("surface_container_highest", false);
    net->pressedRadius = 14;
    auto* netIcon = net->add<Icon>("wifi", 22);
    netIcon->setColor("primary", false);
    auto* netText = net->add<Flex>(Flex::Direction::Column);
    netText->setAlignItems(Align::Start);
    netText->grow = true;
    auto* netName = netText->add<Label>();
    netName->setSize(14)->setWeight(500)->setMaxWidth(150);
    auto* netLink = netText->add<Label>();
    netLink->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(150);
    auto [netDown, netUp] = rates(net, 12);
    net->onClicked = [showWifi](uint32_t) { showWifi(); };
    auto lease = Network::get().watchTraffic();

    // Now playing
    auto* media = live->add<Flex>(Flex::Direction::Row);
    media->setPadding({20, 12})->setSpacing(12)->setRadius(Radii(28))->setBackground("surface_container_highest", false);
    media->add<Icon>("music_note", 24)->setColor("primary");
    auto* mediaText = media->add<Flex>(Flex::Direction::Column);
    mediaText->setAlignItems(Align::Start);
    mediaText->grow = true;
    auto* track = mediaText->add<Label>();
    track->setSize(14)->setWeight(500)->setMaxWidth(200);
    auto* artist = mediaText->add<Label>();
    artist->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(200);
    iconButton(media, "skip_previous", [] { Mpris::get().previous(); });
    auto* play = iconButton(media, "play_arrow", [] { Mpris::get().playPause(); });
    iconButton(media, "skip_next", [] { Mpris::get().next(); });

    live->sync = [=] {
        auto& a = Audio::get();
        if (const auto* s = a.sink()) {
            volume->setValue(s->volume());
            volume->setIcon(s->muted ? "volume_off" : "volume_up");
        }
        if (light) light->setValue(Brightness::get().value());
        auto& n = Network::get();
        wifi->set(n.wifiEnabled(), !n.wifiEnabled() ? "Off" : n.kind() == Network::Kind::Wifi ? n.name() : "On");
        (void)lease;
        net->setVisible(n.kind() != Network::Kind::None);
        netIcon->setText(n.icon());
        netName->setText(n.name());
        const auto summary = linkSummary();
        netLink->setText(summary.empty() ? (n.kind() == Network::Kind::Wifi ? std::format("Signal {}%", n.strength()) : "Connected") : summary);
        netDown->setText(formatRate(n.details().rxRate));
        netUp->setText(formatRate(n.details().txRate));
        auto& b = Bluetooth::get();
        bt->set(b.powered(), !b.available() ? "Unavailable" : !b.powered() ? "Off"
                           : b.connected().empty() ? "On" : b.connected().front());
        clock->setText(nowTime());
        dnd->set(Notifications::get().doNotDisturb());
        caffeine->set(Idle::get().inhibited());
        night->set(g_nightLight);
        const auto* src = a.source();
        mic->set(src && !src->muted, src && !src->muted ? "mic" : "mic_off");
        auto& pw = Power::get();
        battery->setVisible(pw.hasBattery() || !pw.profile().empty());
        batIcon->setText(pw.hasBattery() ? pw.icon() : "power");
        batIcon->setColor(pw.hasBattery() && pw.percent() <= 15 && !pw.charging() ? "error" : "primary");
        batPercent->setText(pw.hasBattery() ? std::to_string(pw.percent()) + "%" : "Power mode");
        batStatus->setText(pw.hasBattery() ? pw.status() : "");
        batStatus->setVisible(pw.hasBattery());
        for (auto& [profile, button] : modes) {
            const bool on = pw.profile() == profile;
            button->setVisible(std::ranges::find(pw.profiles(), profile) != pw.profiles().end());
            button->setBackground(on ? ColorRef("primary") : ColorRef::none());
            static_cast<Icon*>(button->children()[0].get())->setFill(on)->setColor(on ? "on_primary" : "on_surface_variant");
        }
        const auto* p = Mpris::get().active();
        media->setVisible(p != nullptr);
        if (p) {
            track->setText(p->title);
            artist->setText(p->artist.empty() ? p->identity : p->artist);
            static_cast<Icon*>(play->children()[0].get())->setText(p->status == "Playing" ? "pause" : "play_arrow");
        }
    };
    live->watch(Audio::get().changed);
    live->watch(Brightness::get().changed);
    live->watch(Network::get().changed);
    live->watch(Network::get().detailsChanged);
    live->watch(Bluetooth::get().changed);
    live->watch(Notifications::get().changed);
    live->watch(Idle::get().changed);
    live->watch(Mpris::get().changed);
    live->watch(Power::get().changed);
    live->sync();
}

} // namespace

void quickSettings(Flex* card, Popup& popup, const std::string& view) {
    auto* body = card->add<Flex>(Flex::Direction::Column);
    body->setAlignItems(Align::Stretch);
    // The views swap inside `body`; build after this function returns so the
    // first view's buttons can rebuild it
    auto showMain = std::make_shared<std::function<void()>>();
    // Switching views destroys the button that asked for it: defer
    auto showWifi = [body, showMain] { EventLoop::get().defer([body, showMain] { wifiView(body, *showMain); }); };
    auto showBluetooth = [body, showMain] { EventLoop::get().defer([body, showMain] { bluetoothView(body, *showMain); }); };
    *showMain = [body, &popup, showWifi, showBluetooth] {
        EventLoop::get().defer([body, &popup, showWifi, showBluetooth] { mainView(body, popup, showWifi, showBluetooth); });
    };
    if (view == "wifi")
        wifiView(body, *showMain);
    else if (view == "bluetooth")
        bluetoothView(body, *showMain);
    else
        mainView(body, popup, showWifi, showBluetooth);
    // Night light state from the running processes
    m3::run({"pgrep", "-x", "hyprsunset"}, [](int code, const std::string&) { g_nightLight = code == 0; });
}

} // namespace m3::shell::panels
