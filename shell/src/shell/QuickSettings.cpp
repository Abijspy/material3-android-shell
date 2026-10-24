#include "services/Audio.h"
#include "services/Idle.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Search.h"
#include "services/Vpn.h"
#include "quickshare/QuickShare.h"
#include "services/Wallpaper.h"
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Auth.h"
#include "core/Paths.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    std::strftime(buf, sizeof buf, "%a, %-d %b", &tm); // "Mon, 19 Oct", as Android's shade
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
void wifiView(Flex* body, const std::function<void()>& back, bool details, std::function<void(Flex*)> connected) {
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
    scroll->maxHeight = connected ? 1e6f : 340;
    auto* list = scroll->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2);
    auto expanded = std::make_shared<std::string>(); // SSID asking for a password
    auto detailsOpen = std::make_shared<bool>(false); // the connected entry's dropdown

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
            if (ap.saved) {
                const std::string ssid = ap.ssid;
                confirmButton(row, "wifi:" + ssid, "delete", [ssid] { Network::get().forget(ssid); });
            }
            // Settings: the connected entry opens like a dropdown
            if (ap.active && connected)
                row->add<Icon>(*detailsOpen ? "expand_less" : "expand_more", 22)->setColor("on_secondary_container", false);
            // The connected entry carries its own details and speeds
            if (ap.active && connected && *detailsOpen) {
                auto* info = entry->add<Flex>(Flex::Direction::Column);
                info->setAlignItems(Align::Stretch)->setPadding({0, 4, 0, 4});
                connected(info);
            } else if (ap.active && details) {
                auto* info = entry->add<Flex>(Flex::Direction::Column);
                info->setAlignItems(Align::Start)->setPadding({32, 0, 0, 2});
                connectedInfo(info, "on_secondary_container");
            }
            auto apCopy = ap;
            const bool tapDisconnects = !connected;
            entry->onClicked = [=](uint32_t) {
                if (apCopy.active) {
                    if (tapDisconnects) return Network::get().disconnect();
                    *detailsOpen = !*detailsOpen;
                    // Rebuilding destroys this row: not while its handler runs
                    live->syncSoon();
                    return;
                }
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
        {"vpn", "vpn_key", "VPN", [] { return Vpn::get().anyActive(); },
         [] {
             auto& v = Vpn::get();
             if (!v.busy().empty()) return std::string("Connecting…");
             return v.anyActive() ? v.activeName() : std::string("Off");
         },
         [] { return std::string(Vpn::get().anyActive() ? "vpn_lock" : "vpn_key"); },
         [](Popup& popup) {
             // Nothing to switch yet: where VPNs are added
             auto& v = Vpn::get();
             if (v.connections().empty() && (v.warp() == Vpn::Warp::Absent || v.warp() == Vpn::Warp::NoDaemon ||
                                             v.warp() == Vpn::Warp::Unregistered)) {
                 popup.close();
                 settingsPage("vpn");
                 return;
             }
             v.toggleLast();
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
        {"quickshare", "nearby", "Quick Share", [] { return quickshare::QuickShare::get().receiving(); },
         [] { return std::string(quickshare::QuickShare::get().receiving() ? "Visible nearby" : "Off"); },
         [] { return quickshare::QuickShare::symbol(); },
         [](Popup&) { Config::get().set("/quickshare/receive", !Config::get().value<bool>("/quickshare/receive", true)); }},
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
        std::function<void()> use = [def, popup, live] {
            if (!popup) return;
            def->press(*popup);
            if (live) live->syncSoon(); // night light and the like have no service to say so
        };
        if (t.large) {
            auto* tile = row->add<Tile>(def->icon, def->title);
            tile->grow = false;
            tile->fixedWidth = kCol * 2 + 8;
            tile->onToggle = use;
            if (def->details == 1 && showWifi)
                tile->onDetails = showWifi;
            else if (def->details == 2 && showBluetooth)
                tile->onDetails = showBluetooth;
            updates.push_back([tile, def] { tile->set(def->active(), def->status(), def->glyph ? def->glyph() : ""); });
        } else {
            auto* tile = row->add<SmallTile>(def->icon, kCol);
            tile->onClicked = [use](uint32_t button) {
                if (button == BTN_LEFT) use();
            };
            updates.push_back([tile, def] { tile->set(def->active(), def->glyph ? def->glyph() : ""); });
        }
    }
    return [updates] {
        for (const auto& u : updates) u();
    };
}

// ── Editor ──
//
// As Android 17's: the tiles in a neutral state. Hold and drag one to move
// it; tap one to select it: its corner badge removes it, and dragging the
// handle on its right edge stretches it to large or shrinks it to small.
// The tiles not in use sit below; a tap adds one.

// Takes the press over a tile in the editor: a tap selects, a drag moves
// the whole tile with the pointer and drops it where it is released
class DragCatcher : public Node {
public:
    std::function<void()> onTap;
    std::function<void(float, float)> onDrop; // where it was released
    Node* moving = nullptr;                   // follows the pointer

    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float y) override {
        m_down = button == BTN_LEFT;
        m_x0 = m_x = x;
        m_y0 = m_y = y;
        m_dragging = false;
    }
    void onDrag(float x, float y) override {
        if (!m_down) return;
        m_x = x;
        m_y = y;
        if (!m_dragging && std::hypot(x - m_x0, y - m_y0) > 6) m_dragging = true;
        if (m_dragging && moving) {
            moving->setTranslateX(x - m_x0, false);
            moving->setTranslateY(y - m_y0, false);
        }
    }
    void onRelease() override {
        if (!m_down) return;
        m_down = false;
        if (m_dragging) {
            if (moving) {
                moving->setTranslateX(0, false);
                moving->setTranslateY(0, false);
            }
            if (onDrop) onDrop(m_x, m_y);
        } else if (onTap) {
            onTap();
        }
    }

protected:
    SizeF doMeasure(SizeF) override { return {}; }

private:
    bool m_down = false, m_dragging = false;
    float m_x0 = 0, m_y0 = 0, m_x = 0, m_y = 0;
};

// The selected tile's right edge: drag it out to make the tile large, in to
// make it small. The tile follows the pointer, then snaps to the nearer size.
class ResizeHandle : public Node {
public:
    Node* tile = nullptr;            // whose width follows the drag
    float small = 0, large = 0;      // the two widths it snaps between
    std::function<void(bool)> onResized; // large?

    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float) override {
        m_down = button == BTN_LEFT && tile;
        m_x0 = x;
        m_w0 = tile ? tile->rect().w : 0;
    }
    void onDrag(float x, float) override {
        if (!m_down) return;
        tile->fixedWidth = std::clamp(m_w0 + x - m_x0, small, large);
        tile->requestLayout();
    }
    void onRelease() override {
        if (!m_down) return;
        m_down = false;
        const float w = tile->fixedWidth.value_or(m_w0);
        const bool toLarge = w > (small + large) / 2;
        tile->fixedWidth = toLarge ? large : small;
        tile->requestLayout();
        if (onResized) onResized(toLarge);
    }

protected:
    SizeF doMeasure(SizeF) override { return {}; }
    void doArrange() override {
        // The grip: a short pill centred in the handle
        if (!children().empty()) children()[0]->arrange({rect().x + 8, rect().y + 7, 6, rect().h - 14});
    }

private:
    bool m_down = false;
    float m_x0 = 0, m_w0 = 0;
};

// One tile in the editor: the tile, the catcher over it, and (selected) the
// remove badge on its top-left corner and the resize handle on its right edge
class EditTile : public Node {
public:
    Node* hitTest(float x, float y, bool any) override {
        // The badge hangs over the corner: it answers outside the tile too
        const auto& k = children();
        for (size_t i = k.size(); i-- > 2;)
            if (Node* hit = k[i]->hitTest(x, y, any)) return hit;
        return Node::hitTest(x, y, any);
    }

protected:
    SizeF doMeasure(SizeF available) override { return children().empty() ? SizeF{} : children()[0]->measure(available); }
    void doArrange() override {
        const auto& r = rect();
        const auto& k = children();
        if (k.size() > 0) k[0]->arrange(r);
        if (k.size() > 1) k[1]->arrange(r);
        if (k.size() > 2) k[2]->arrange({r.x - 6, r.y - 6, 26, 26});
        if (k.size() > 3) k[3]->arrange({r.right() - 22, r.y + r.h / 2 - 20, 22, 40});
    }
};

void editView(Flex* body, const std::function<void()>& back) {
    body->clearChildren();
    auto* live = body->add<Live>();
    live->setSpacing(12);
    auto selected = std::make_shared<int>(-1);
    live->sync = [live, back, selected] {
        live->clearChildren();
        auto* head = live->add<Flex>(Flex::Direction::Row);
        head->setSpacing(8);
        iconButton(head, "arrow_back", back);
        auto* title = head->add<Label>("Edit");
        title->setSize(22)->setWeight(500);
        title->grow = true;
        auto* reset = head->add<Button>();
        pill(reset->setBackground(ColorRef("on_surface", 0.1f), false))->setPadding({16, 9});
        reset->pressedRadius = 10;
        reset->add<Label>("Reset")->setSize(14)->setWeight(500);
        reset->onClicked = [live, selected](uint32_t) {
            *selected = -1;
            Config::get().set("/quickSettings/tiles", nullptr);
            live->syncSoon();
        };

        auto tiles = tileLayout();
        if (*selected >= static_cast<int>(tiles.size())) *selected = -1;
        auto save = [live](const std::vector<Placed>& t) {
            setTileLayout(t);
            live->syncSoon();
        };
        auto* scroll = live->add<ScrollView>();
        scroll->maxHeight = 500;
        auto* content = scroll->add<Flex>(Flex::Direction::Column);
        content->setAlignItems(Align::Stretch)->setSpacing(12)->setPadding({6, 6, 6, 0});
        caption(content, "Hold and drag to rearrange tiles");

        // Your tiles, neutral, on the panel's 4-column grid: 400 less the
        // scrollbar's gutter (14) and the 6 px either side the badges
        // overhang by
        constexpr float kCol = (374 - 3 * 8) / 4.0f;
        auto edits = std::make_shared<std::vector<EditTile*>>();
        auto* grid = content->add<Flex>(Flex::Direction::Column);
        grid->setSpacing(8)->setAlignItems(Align::Start);
        grid->alignSelf = Align::Center; // even margins either side
        Flex* row = nullptr;
        int used = 4;
        for (size_t i = 0; i < tiles.size(); ++i) {
            const auto* def = findTile(tiles[i].id);
            if (!def) continue;
            const int span = tiles[i].large ? 2 : 1;
            if (used + span > 4) {
                row = grid->add<Flex>(Flex::Direction::Row);
                row->setSpacing(8);
                used = 0;
            }
            used += span;
            const bool on = *selected == static_cast<int>(i);
            auto* edit = row->add<EditTile>();
            edits->push_back(edit);
            if (tiles[i].large) {
                auto* t = edit->add<Tile>(def->icon, def->title);
                t->grow = false;
                t->fixedWidth = kCol * 2 + 8;
                t->set(false, "");
                t->setSelected(on);
            } else {
                auto* t = edit->add<SmallTile>(def->icon, kCol);
                t->setSelected(on);
            }
            auto* catcher = edit->add<DragCatcher>();
            catcher->moving = edit;
            const int index = static_cast<int>(i);
            catcher->onTap = [selected, live, index] {
                *selected = *selected == index ? -1 : index;
                live->syncSoon();
            };
            // Dropped over another tile: it takes that tile's place
            catcher->onDrop = [edits, tiles, index, selected, save](float x, float y) mutable {
                for (size_t to = 0; to < edits->size(); ++to) {
                    if (!(*edits)[to]->rect().contains(x, y) || static_cast<int>(to) == index) continue;
                    const Placed moved = tiles[static_cast<size_t>(index)];
                    tiles.erase(tiles.begin() + index);
                    tiles.insert(tiles.begin() + static_cast<long>(to), moved);
                    *selected = static_cast<int>(to);
                    save(tiles);
                    return;
                }
            };
            if (on) {
                // Remove: the badge on the corner
                auto* badge = edit->add<Button>();
                pill(badge->setBackground("on_surface", false))->setJustify(Align::Center);
                badge->add<Icon>("remove", 18)->setColor("surface", false);
                badge->onClicked = [tiles, index, selected, save](uint32_t button) mutable {
                    if (button != BTN_LEFT) return;
                    tiles.erase(tiles.begin() + index);
                    *selected = -1;
                    save(tiles);
                };
                // Resize: drag the handle on the right edge
                auto* handle = edit->add<ResizeHandle>();
                auto* grip = handle->add<Flex>(Flex::Direction::Row);
                pill(grip->setBackground("primary", false));
                handle->tile = edit->children()[0].get();
                handle->small = kCol;
                handle->large = kCol * 2 + 8;
                handle->onResized = [tiles, index, save](bool large) mutable {
                    if (tiles[static_cast<size_t>(index)].large == large) return;
                    tiles[static_cast<size_t>(index)].large = large;
                    save(tiles);
                };
            }
        }
        if (tiles.empty()) caption(grid, "No tiles: add some below");

        auto* divider = content->add<Flex>(Flex::Direction::Row);
        divider->fixedHeight = 1;
        divider->setBackground(ColorRef("outline_variant"), false);

        // Tiles not in use: a tap adds one
        caption(content, "Tap to add tiles");
        auto* avail = content->add<Flow>();
        avail->spacing = 8;
        avail->alignSelf = Align::Center;
        bool any = false;
        for (const auto& def : tileDefs()) {
            if (std::ranges::any_of(tiles, [&](const Placed& p) { return p.id == def.id; })) continue;
            any = true;
            auto* item = avail->add<Button>(Flex::Direction::Column);
            item->setPadding({4, 8})->setSpacing(6)->setRadius(Radii(20));
            item->pressedRadius = 12;
            item->fixedWidth = kCol;
            auto* chip = item->add<Flex>(Flex::Direction::Row);
            pill(chip->setBackground(ColorRef("on_surface", 0.1f), false))->setJustify(Align::Center);
            chip->fixedWidth = 64;
            chip->fixedHeight = 52;
            chip->alignSelf = Align::Center;
            chip->add<Icon>(def.icon, 22)->setColor("on_surface", false);
            auto* label = item->add<Label>(def.title);
            label->setSize(12)->setMaxWidth(kCol - 8)->setMaxLines(2)->setCentered(true)->setColor("on_surface_variant", false);
            label->alignSelf = Align::Center;
            const std::string id = def.id;
            item->onClicked = [tiles, id, save](uint32_t) mutable {
                tiles.push_back({id, false});
                save(tiles);
            };
        }
        if (!any) caption(content, "Every tile is in use");
    };
    live->sync();
}

void mainView(Flex* body, Popup& popup, const std::function<void()>& showWifi, const std::function<void()>& showBluetooth,
              const std::function<void()>& showEdit) {
    body->clearChildren();
    // Taller than the screen (a small one, music playing): it scrolls
    auto* scroller = body->add<ScrollView>();
    scroller->maxHeight = 1e6f;
    scroller->overlayBar = true;
    auto* live = scroller->add<Live>();

    // M3 Expressive, as Android 16/17's shade, in two rows: the large clock
    // with the battery and your picture at its end; the date with the round
    // tonal buttons at its end
    auto* head = live->add<Flex>(Flex::Direction::Column);
    head->setAlignItems(Align::Stretch)->setSpacing(2)->setPadding({4, 0, 0, 4});
    auto* top = head->add<Flex>(Flex::Direction::Row);
    top->setSpacing(10);
    auto* clock = top->add<Label>(nowTime());
    clock->setSize(44)->setWeight(400);
    auto* gap = top->add<Node>();
    gap->grow = true;
    // The battery: its icon and level (a tap opens its popup)
    auto* battery = top->add<Button>(Flex::Direction::Row);
    pill(battery)->setPadding({8, 6})->setSpacing(2);
    battery->pressedRadius = 8;
    auto* batteryIcon = battery->add<Icon>("battery_full", 18);
    auto* batteryText = battery->add<Label>();
    batteryText->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    battery->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT && open) open("battery");
    };
    // You: your picture, opening Settings → Users
    {
        const std::string user = std::getenv("USER") ? std::getenv("USER") : "";
        auto* me = top->add<Button>();
        pill(me)->setPadding(Insets(0));
        me->pressedRadius = 14;
        me->add<AvatarView>(paths::home() + "/.face", auth::realName(user), 44);
        me->onClicked = [](uint32_t button) {
            if (button == BTN_LEFT && open) open("settings users");
        };
    }
    auto* bottom = head->add<Flex>(Flex::Direction::Row);
    bottom->setSpacing(8);
    auto* date = bottom->add<Label>(today());
    date->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    date->grow = true;
    auto roundButton = [&](const char* icon, std::function<void()> fn) {
        auto* b = bottom->add<Button>();
        pill(b->setBackground(ColorRef("on_surface", 0.1f), false))->setPadding(Insets(10));
        b->pressedRadius = 12;
        b->add<Icon>(icon, 20)->setColor("on_surface", false);
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

    // Now playing: the M3 Expressive player, while something plays
    auto* media = live->add<MediaCard>(true);

    live->sync = [=] {
        auto& a = Audio::get();
        if (const auto* s = a.sink()) {
            volume->setValue(s->volume());
            volume->setIcon(s->muted ? "volume_off" : "volume_up");
        }
        if (light) light->setValue(Brightness::get().value());
        updateTiles();
        auto& pw = Power::get();
        battery->setVisible(pw.hasBattery());
        batteryIcon->setText(pw.icon());
        batteryIcon->setColor(pw.percent() <= 15 && !pw.charging() ? "error" : "on_surface_variant");
        batteryText->setText(std::to_string(pw.percent()) + "%");
        clock->setText(nowTime());
        media->setVisible(Mpris::get().active() != nullptr);
    };
    live->watch(Audio::get().changed);
    live->watch(Brightness::get().changed);
    live->watch(Network::get().changed);
    live->watch(Bluetooth::get().changed);
    live->watch(Notifications::get().changed);
    live->watch(Idle::get().changed);
    live->watch(Vpn::get().changed);
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
