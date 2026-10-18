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

#include <ctime>
#include <format>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

bool g_nightLight = false;

std::string today() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, "%A, %-d %B", &tm);
    return buf;
}

// Wi-Fi networks, tap to connect; secured new ones ask for the password
void wifiView(Flex* body, const std::function<void()>& back) {
    body->clearChildren();
    auto* live = body->add<Live>();
    auto* head = live->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8);
    iconButton(head, "arrow_back", back);
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
            auto* row = list->add<Button>(Flex::Direction::Row);
            row->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(16));
            if (ap.active) row->setBackground("secondary_container", false);
            const char* bars = ap.strength >= 80 ? "signal_wifi_4_bar" : ap.strength >= 55 ? "network_wifi_3_bar"
                             : ap.strength >= 30 ? "network_wifi_2_bar" : "network_wifi_1_bar";
            row->add<Icon>(bars, 20)->setColor(ap.active ? "on_secondary_container" : "on_surface_variant", false);
            auto* texts = row->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start);
            texts->grow = true;
            texts->add<Label>(ap.ssid)->setSize(14)->setMaxWidth(240)->setColor(ap.active ? "on_secondary_container" : "on_surface", false);
            if (ap.active || ap.saved)
                texts->add<Label>(ap.active ? "Connected" : "Saved")->setSize(12)->setColor("on_surface_variant", false);
            if (ap.secured) row->add<Icon>("lock", 16)->setColor("on_surface_variant", false);
            auto apCopy = ap;
            row->onClicked = [=](uint32_t) {
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

void mainView(Flex* body, Popup& popup, const std::function<void()>& showWifi) {
    body->clearChildren();
    auto* live = body->add<Live>();

    // Date, then lock and power
    auto* head = live->add<Flex>(Flex::Direction::Row);
    head->setSpacing(4);
    auto* date = head->add<Label>(today());
    date->setSize(18)->setWeight(500);
    date->grow = true;
    iconButton(head, "lock", [&popup] {
        popup.close();
        if (lock) lock();
    });
    iconButton(head, "power_settings_new", [] {
        if (open) open("power");
    });

    // Sliders
    auto sliderRow = [&](const char* icon, std::function<void()> onIcon) {
        auto* row = live->add<Flex>(Flex::Direction::Row);
        row->setSpacing(8);
        auto* b = iconButton(row, icon, std::move(onIcon), 22);
        auto* sl = row->add<Slider>();
        sl->grow = true;
        return std::pair{b, sl};
    };
    auto [volButton, volume] = sliderRow("volume_up", [] { Audio::get().toggleMuted(); });
    volume->onChanged = [](float v) { Audio::get().setVolume(v); };
    Slider* light = nullptr;
    if (Brightness::get().available()) {
        light = sliderRow("brightness_medium", {}).second;
        light->setValue(Brightness::get().value(), false);
        light->onChanged = [](float v) { Brightness::get().set(v); };
    }

    // Tiles, two per row
    auto row1 = live->add<Flex>(Flex::Direction::Row);
    row1->setSpacing(8)->setAlignItems(Align::Stretch);
    auto* wifi = row1->add<Tile>("wifi", "Wi-Fi");
    auto* bt = row1->add<Tile>("bluetooth", "Bluetooth");
    auto row2 = live->add<Flex>(Flex::Direction::Row);
    row2->setSpacing(8)->setAlignItems(Align::Stretch);
    auto* dnd = row2->add<Tile>("do_not_disturb_on", "Do not disturb");
    auto* caffeine = row2->add<Tile>("coffee", "Caffeine");
    auto row3 = live->add<Flex>(Flex::Direction::Row);
    row3->setSpacing(8)->setAlignItems(Align::Stretch);
    auto* night = row3->add<Tile>("nightlight", "Night light");
    auto* mic = row3->add<Tile>("mic", "Microphone");
    // Two equal columns: (card 440 − padding 40 − gap 8) / 2
    for (Tile* t : {wifi, bt, dnd, caffeine, night, mic}) {
        t->grow = false;
        t->fixedWidth = 196;
    }

    wifi->enableDetails();
    wifi->onDetails = showWifi;
    wifi->onClicked = [](uint32_t) { Network::get().setWifiEnabled(!Network::get().wifiEnabled()); };
    bt->onClicked = [](uint32_t) { Bluetooth::get().setPowered(!Bluetooth::get().powered()); };
    dnd->onClicked = [](uint32_t) { Notifications::get().setDoNotDisturb(!Notifications::get().doNotDisturb()); };
    caffeine->onClicked = [](uint32_t) { Idle::get().setInhibited(!Idle::get().inhibited()); };
    night->onClicked = [live](uint32_t) {
        g_nightLight = !g_nightLight;
        spawn(g_nightLight ? "pkill -x hyprsunset; hyprsunset -t 4500" : "pkill -x hyprsunset");
        live->syncSoon();
    };
    mic->onClicked = [](uint32_t) { Audio::get().toggleMuted(true); };

    // Now playing
    auto* media = live->add<Flex>(Flex::Direction::Row);
    media->setPadding({16, 12})->setSpacing(12)->setRadius(Radii(20))->setBackground("surface_container_high", false);
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
            static_cast<Icon*>(volButton->children()[0].get())->setText(s->muted ? "volume_off" : "volume_up");
        }
        if (light) light->setValue(Brightness::get().value());
        auto& n = Network::get();
        wifi->set(n.wifiEnabled(), !n.wifiEnabled() ? "Off" : n.kind() == Network::Kind::Wifi ? n.name() : "On");
        auto& b = Bluetooth::get();
        bt->set(b.powered(), !b.available() ? "Unavailable" : !b.powered() ? "Off"
                           : b.connected().empty() ? "On" : b.connected().front());
        dnd->set(Notifications::get().doNotDisturb(), Notifications::get().doNotDisturb() ? "On" : "Off");
        caffeine->set(Idle::get().inhibited(), Idle::get().inhibited() ? "Staying awake" : "Off");
        night->set(g_nightLight, g_nightLight ? "4500 K" : "Off");
        const auto* src = a.source();
        mic->set(src && !src->muted, !src ? "None" : src->muted ? "Muted" : "On");
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
    live->watch(Bluetooth::get().changed);
    live->watch(Notifications::get().changed);
    live->watch(Idle::get().changed);
    live->watch(Mpris::get().changed);
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
    *showMain = [body, &popup, showWifi] { EventLoop::get().defer([body, &popup, showWifi] { mainView(body, popup, showWifi); }); };
    if (view == "wifi")
        wifiView(body, *showMain);
    else
        mainView(body, popup, showWifi);
    // Night light state from the running processes
    m3::run({"pgrep", "-x", "hyprsunset"}, [](int code, const std::string&) { g_nightLight = code == 0; });
}

} // namespace m3::shell::panels
