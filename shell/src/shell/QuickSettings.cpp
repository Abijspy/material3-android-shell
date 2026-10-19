#include "services/Audio.h"
#include "services/Idle.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Wallpaper.h"
#include "core/Config.h"
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

// ── Tiles ──
//
// Every tile Quick Settings can show. Which ones, in what order and at what
// size live in config.json (/quickSettings/tiles: [{"id", "large"}]), edited
// from the panel's pencil button.

struct TileDef {
    const char* id;
    const char* icon;
    const char* title;
    std::function<bool()> active;
    std::function<std::string()> status;    // under the title on large tiles
    std::function<std::string()> glyph;     // an icon that follows the state, if any
    std::function<void(Popup&)> press;
    int details = 0;                        // 1 Wi-Fi list, 2 Bluetooth list
};

const std::vector<TileDef>& tileDefs() {
    static const std::vector<TileDef> defs = {
        {"wifi", "wifi", "Wi-Fi", [] { return Network::get().wifiEnabled(); },
         [] {
             auto& n = Network::get();
             return !n.wifiEnabled() ? std::string("Off") : n.kind() == Network::Kind::Wifi ? n.name() : std::string("On");
         },
         {}, [](Popup&) { Network::get().setWifiEnabled(!Network::get().wifiEnabled()); }, 1},
        {"bluetooth", "bluetooth", "Bluetooth", [] { return Bluetooth::get().powered(); },
         [] {
             auto& b = Bluetooth::get();
             return !b.available() ? std::string("Unavailable") : !b.powered() ? std::string("Off")
                    : b.connected().empty() ? std::string("On") : b.connected().front();
         },
         {}, [](Popup&) { Bluetooth::get().setPowered(!Bluetooth::get().powered()); }, 2},
        {"dnd", "do_not_disturb_on", "Do not disturb", [] { return Notifications::get().doNotDisturb(); },
         [] { return std::string(Notifications::get().doNotDisturb() ? "On" : "Off"); }, {},
         [](Popup&) { Notifications::get().setDoNotDisturb(!Notifications::get().doNotDisturb()); }},
        {"caffeine", "coffee", "Caffeine", [] { return Idle::get().inhibited(); },
         [] { return std::string(Idle::get().inhibited() ? "Staying awake" : "Off"); }, {},
         [](Popup&) { Idle::get().setInhibited(!Idle::get().inhibited()); }},
        {"nightlight", "nightlight", "Night light", [] { return g_nightLight; },
         [] { return std::string(g_nightLight ? "4500 K" : "Off"); }, {},
         [](Popup&) {
             g_nightLight = !g_nightLight;
             spawn(g_nightLight ? "pkill -x hyprsunset; hyprsunset -t 4500" : "pkill -x hyprsunset");
         }},
        {"mic", "mic", "Microphone",
         [] {
             const auto* s = Audio::get().source();
             return s && !s->muted;
         },
         [] {
             const auto* s = Audio::get().source();
             return std::string(!s ? "None" : s->muted ? "Muted" : "On");
         },
         [] {
             const auto* s = Audio::get().source();
             return std::string(s && !s->muted ? "mic" : "mic_off");
         },
         [](Popup&) { Audio::get().toggleMuted(true); }},
        {"sound", "volume_up", "Sound",
         [] {
             const auto* s = Audio::get().sink();
             return s && !s->muted;
         },
         [] {
             const auto* s = Audio::get().sink();
             return std::string(!s ? "None" : s->muted ? "Muted" : "On");
         },
         [] {
             const auto* s = Audio::get().sink();
             return std::string(s && !s->muted ? "volume_up" : "volume_off");
         },
         [](Popup&) { Audio::get().toggleMuted(); }},
        {"darkmode", "dark_mode", "Dark theme", [] { return Config::get().value<bool>("/background/darkmode", true); },
         [] { return std::string(Config::get().value<bool>("/background/darkmode", true) ? "On" : "Off"); }, {},
         [](Popup&) {
             Config::get().set("/background/darkmode", !Config::get().value<bool>("/background/darkmode", true));
             Wallpaper::get().recolour();
         }},
        {"airplane", "flight", "Airplane mode",
         [] { return !Network::get().wifiEnabled() && !Bluetooth::get().powered(); },
         [] { return std::string(!Network::get().wifiEnabled() && !Bluetooth::get().powered() ? "On" : "Off"); }, {},
         [](Popup&) {
             // Off again turns Wi-Fi back on; Bluetooth stays as the user leaves it
             const bool on = !Network::get().wifiEnabled() && !Bluetooth::get().powered();
             Network::get().setWifiEnabled(on);
             if (!on) Bluetooth::get().setPowered(false);
         }},
        {"powersaver", "eco", "Power saver", [] { return Power::get().profile() == "power-saver"; },
         [] { return std::string(Power::get().profile() == "power-saver" ? "On" : "Off"); }, {},
         [](Popup&) { Power::get().setProfile(Power::get().profile() == "power-saver" ? "balanced" : "power-saver"); }},
        {"screenshot", "screenshot_region", "Screenshot", [] { return false; }, [] { return std::string("Region"); }, {},
         [](Popup& popup) {
             popup.close();
             spawn("material3ui msg screenshot region");
         }},
    };
    return defs;
}

const TileDef* findTile(const std::string& id) {
    for (const auto& d : tileDefs())
        if (id == d.id) return &d;
    return nullptr;
}

struct Placed {
    std::string id;
    bool large = false;
};

std::vector<Placed> tileLayout() {
    std::vector<Placed> out;
    const auto saved = Config::get().value<nlohmann::json>("/quickSettings/tiles", nullptr);
    if (saved.is_array())
        for (const auto& t : saved)
            if (t.is_object() && findTile(t.value("id", ""))) out.push_back({t.value("id", ""), t.value("large", false)});
    if (out.empty() && !saved.is_array())
        out = {{"wifi", true}, {"bluetooth", true}, {"dnd", false}, {"caffeine", false}, {"nightlight", false}, {"mic", false}};
    return out;
}

void setTileLayout(const std::vector<Placed>& tiles) {
    auto j = nlohmann::json::array();
    for (const auto& t : tiles) j.push_back({{"id", t.id}, {"large", t.large}});
    Config::get().set("/quickSettings/tiles", j);
}

// Lays tiles on a 4-column grid (a large tile takes two) inside `parent`
// and returns a function that brings them up to date
std::function<void()> buildTiles(Node* parent, const std::vector<Placed>& tiles, Popup* popup, kit::Live* live,
                                 const std::function<void()>& showWifi, const std::function<void()>& showBluetooth) {
    // Card 440 − padding 40 = 400: four 94 px columns, 8 px apart
    constexpr float kCol = (400 - 3 * 8) / 4.0f;
    std::vector<std::function<void()>> updates;
    Flex* row = nullptr;
    int used = 4;
    for (const auto& t : tiles) {
        const auto* def = findTile(t.id);
        if (!def) continue;
        const int span = t.large ? 2 : 1;
        if (used + span > 4) {
            row = parent->add<Flex>(Flex::Direction::Row);
            row->setSpacing(8)->setAlignItems(Align::Stretch);
            used = 0;
        }
        used += span;
        auto onPress = [def, popup, live](uint32_t button) {
            if (button != BTN_LEFT || !popup) return;
            def->press(*popup);
            if (live) live->syncSoon(); // night light and the like have no service to say so
        };
        if (t.large) {
            auto* tile = row->add<Tile>(def->icon, def->title);
            tile->grow = false;
            tile->fixedWidth = kCol * 2 + 8;
            tile->onClicked = onPress;
            if (def->details == 1 && showWifi) {
                tile->enableDetails();
                tile->onDetails = showWifi;
            }
            if (def->details == 2 && showBluetooth) {
                tile->enableDetails();
                tile->onDetails = showBluetooth;
            }
            updates.push_back([tile, def] { tile->set(def->active(), def->status()); });
        } else {
            auto* tile = row->add<SmallTile>(def->icon, kCol);
            tile->onClicked = onPress;
            updates.push_back([tile, def] { tile->set(def->active(), def->glyph ? def->glyph() : ""); });
        }
    }
    return [updates] {
        for (const auto& u : updates) u();
    };
}

// The editor: a live preview, your tiles (size, order, remove), and the
// rest to add
void editView(Flex* body, const std::function<void()>& back) {
    body->clearChildren();
    auto* live = body->add<Live>();
    live->setSpacing(10);
    live->sync = [live, back] {
        live->clearChildren();
        auto* head = live->add<Flex>(Flex::Direction::Row);
        head->setSpacing(8);
        iconButton(head, "arrow_back", back);
        auto* title = head->add<Label>("Edit tiles");
        title->setSize(20)->setWeight(500);
        title->grow = true;
        auto* reset = head->add<Button>();
        pill(reset->setBackground("surface_container_highest", false))->setPadding({14, 8});
        reset->add<Label>("Reset")->setSize(13)->setWeight(500);
        reset->onClicked = [live](uint32_t) {
            Config::get().set("/quickSettings/tiles", nullptr);
            live->syncSoon();
        };

        auto tiles = tileLayout();
        // How the panel will look, as it is now
        auto* preview = live->add<Flex>(Flex::Direction::Column);
        preview->setSpacing(8)->setAlignItems(Align::Start);
        buildTiles(preview, tiles, nullptr, nullptr, {}, {})();

        auto* scroll = live->add<ScrollView>();
        scroll->maxHeight = 300;
        auto* list = scroll->add<Flex>(Flex::Direction::Column);
        list->setAlignItems(Align::Stretch)->setSpacing(4);
        auto save = [live](const std::vector<Placed>& t) {
            setTileLayout(t);
            live->syncSoon();
        };
        caption(list, "Your tiles");
        for (size_t i = 0; i < tiles.size(); ++i) {
            const auto* def = findTile(tiles[i].id);
            auto* r = list->add<Flex>(Flex::Direction::Row);
            r->setPadding({14, 6, 6, 6})->setSpacing(6)->setRadius(Radii(18))->setBackground("surface_container_highest", false);
            r->add<Icon>(def->icon, 20)->setColor("on_surface_variant", false);
            auto* name = r->add<Label>(def->title);
            name->setSize(14)->setMaxWidth(150);
            name->grow = true;
            // Size: a tonal chip that flips between Large and Small
            auto* size = r->add<Button>();
            pill(size->setBackground(tiles[i].large ? "secondary_container" : "surface_container_high", false))->setPadding({12, 6});
            size->add<Label>(tiles[i].large ? "Large" : "Small")->setSize(12)->setWeight(500);
            size->onClicked = [tiles, i, save](uint32_t) mutable {
                tiles[i].large = !tiles[i].large;
                save(tiles);
            };
            iconButton(r, "keyboard_arrow_up", [tiles, i, save]() mutable {
                if (i == 0) return;
                std::swap(tiles[i], tiles[i - 1]);
                save(tiles);
            }, 18);
            iconButton(r, "keyboard_arrow_down", [tiles, i, save]() mutable {
                if (i + 1 >= tiles.size()) return;
                std::swap(tiles[i], tiles[i + 1]);
                save(tiles);
            }, 18);
            iconButton(r, "remove_circle", [tiles, i, save]() mutable {
                tiles.erase(tiles.begin() + static_cast<long>(i));
                save(tiles);
            }, 18);
        }
        if (tiles.empty()) caption(list, "No tiles: add some below");
        bool more = false;
        for (const auto& def : tileDefs()) {
            if (std::ranges::any_of(tiles, [&](const Placed& p) { return p.id == def.id; })) continue;
            if (!more) caption(list, "Add tiles");
            more = true;
            auto* r = list->add<Button>(Flex::Direction::Row);
            r->setPadding({14, 10})->setSpacing(10)->setRadius(Radii(18));
            r->pressedRadius = 10;
            r->add<Icon>(def.icon, 20)->setColor("on_surface_variant", false);
            auto* name = r->add<Label>(def.title);
            name->setSize(14);
            name->grow = true;
            r->add<Icon>("add_circle", 20)->setColor("primary", false);
            const std::string id = def.id;
            r->onClicked = [tiles, id, save](uint32_t) mutable {
                tiles.push_back({id, false});
                save(tiles);
            };
        }
    };
    live->sync();
}

void mainView(Flex* body, Popup& popup, const std::function<void()>& showWifi, const std::function<void()>& showBluetooth,
              const std::function<void()>& showEdit) {
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
    roundButton("edit", showEdit);
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

    // Your tiles, as arranged in the editor
    auto* grid = live->add<Flex>(Flex::Direction::Column);
    grid->setSpacing(8)->setAlignItems(Align::Start);
    const auto updateTiles = buildTiles(grid, tileLayout(), &popup, live, showWifi, showBluetooth);

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
        updateTiles();
        (void)lease;
        net->setVisible(n.kind() != Network::Kind::None);
        netIcon->setText(n.icon());
        netName->setText(n.name());
        const auto summary = linkSummary();
        netLink->setText(summary.empty() ? (n.kind() == Network::Kind::Wifi ? std::format("Signal {}%", n.strength()) : "Connected") : summary);
        netDown->setText(formatRate(n.details().rxRate));
        netUp->setText(formatRate(n.details().txRate));
        clock->setText(nowTime());
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
    live->watch(Config::get().changed); // dark theme, and tiles edited elsewhere
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
    auto showEdit = [body, showMain] { EventLoop::get().defer([body, showMain] { editView(body, *showMain); }); };
    *showMain = [body, &popup, showWifi, showBluetooth, showEdit] {
        EventLoop::get().defer([body, &popup, showWifi, showBluetooth, showEdit] { mainView(body, popup, showWifi, showBluetooth, showEdit); });
    };
    if (view == "wifi")
        wifiView(body, *showMain);
    else if (view == "bluetooth")
        bluetoothView(body, *showMain);
    else if (view == "edit")
        editView(body, *showMain);
    else
        mainView(body, popup, showWifi, showBluetooth, showEdit);
    // Night light state from the running processes
    m3::run({"pgrep", "-x", "hyprsunset"}, [](int code, const std::string&) { g_nightLight = code == 0; });
}

} // namespace m3::shell::panels
