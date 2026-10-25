// Where the hooks' events come from: the services' signals, turned into
// events as their state changes (Hooks::watch). The session's own (locking,
// logging out, sleeping), the wallpaper's and the screens' fire where those
// happen.
#include "services/Hooks.h"

#include "core/Config.h"
#include "services/Hyprland.h"
#include "services/Idle.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Screenshot.h"
#include "services/Vpn.h"
#include "services/Wellbeing.h"

#include <chrono>
#include <format>
#include <memory>

namespace m3 {

namespace {

// The services read their state over D-Bus a moment after startup: what
// they find then is where things stand, not news. For a few seconds the
// trackers only take note
std::chrono::steady_clock::time_point g_watched;
bool settled() {
    return std::chrono::steady_clock::now() - g_watched > std::chrono::seconds(5);
}

void fireSettled(const std::string& event, const Hooks::Env& env = {}) {
    if (settled()) Hooks::get().fire(event, env);
}

// A state that fires `on` or `off` when it turns
struct Flip {
    bool value;
    void operator()(bool now, const char* on, const char* off, const Hooks::Env& env = {}) {
        if (now == value) return;
        value = now;
        fireSettled(now ? on : off, env);
    }
};

const char* urgency(Notifications::Urgency u) {
    return u == Notifications::Low ? "low" : u == Notifications::Critical ? "critical" : "normal";
}

} // namespace

void Hooks::watch() {
    g_watched = std::chrono::steady_clock::now();
    // Light or dark
    auto dark = std::make_shared<bool>(Config::get().value<bool>("/background/darkmode", true));
    Config::get().changed.connectForever([dark] {
        const bool now = Config::get().value<bool>("/background/darkmode", true);
        if (now == *dark) return;
        *dark = now;
        fireSettled("theme_mode_changed", {{"M3_THEME_MODE", now ? "dark" : "light"}, {"M3_THEME_MODE_PREVIOUS", now ? "light" : "dark"}});
    });

    // Away and back
    auto away = std::make_shared<Flip>(Flip{Idle::get().away()});
    Idle::get().changed.connectForever([away] { (*away)(Idle::get().away(), "idle_started", "idle_ended"); });

    // The battery and the power mode
    struct Battery {
        std::string state; // charging, discharging, full
        int percent = -1;
        bool low = false;
        std::string profile;
    };
    auto stateOf = [] {
        auto& p = Power::get();
        return p.full() ? std::string("full") : p.charging() || p.pluggedIn() ? std::string("charging") : std::string("discharging");
    };
    auto battery = std::make_shared<Battery>(Battery{stateOf(), Power::get().percent(), false, Power::get().profile()});
    Power::get().changed.connectForever([battery, stateOf] {
        auto& p = Power::get();
        if (!p.profile().empty() && p.profile() != battery->profile) {
            if (!battery->profile.empty())
                fireSettled("power_profile_changed", {{"M3_POWER_PROFILE", p.profile()}, {"M3_POWER_PROFILE_PREVIOUS", battery->profile}});
            battery->profile = p.profile();
        }
        if (!p.hasBattery()) return;
        const Env env = {{"M3_BATTERY_PERCENT", std::to_string(p.percent())}};
        if (const auto state = stateOf(); state != battery->state) {
            battery->state = state;
            fireSettled(state == "full" ? "battery_full" : state == "charging" ? "battery_charging" : "battery_discharging", env);
        }
        if (p.percent() != battery->percent) {
            battery->percent = p.percent();
            fireSettled("battery_percentage_changed", {env[0], {"M3_BATTERY_STATE", battery->state}});
        }
        // Low: once on the way down, again only after it's been back up
        const bool low = battery->state == "discharging" && p.percent() < 15;
        if (low && !battery->low) fireSettled("battery_low", env);
        battery->low = low;
    });

    // The network, Wi-Fi, Bluetooth and the VPN
    auto& net = Network::get();
    auto online = std::make_shared<Flip>(Flip{net.kind() != Network::Kind::None});
    auto wifi = std::make_shared<Flip>(Flip{net.wifiEnabled()});
    Network::get().changed.connectForever([online, wifi] {
        auto& n = Network::get();
        const char* type = n.kind() == Network::Kind::Wifi ? "wifi" : n.kind() == Network::Kind::Ethernet ? "ethernet" : "other";
        (*online)(n.kind() != Network::Kind::None, "network_connected", "network_disconnected",
                  {{"M3_NETWORK_NAME", n.name()}, {"M3_NETWORK_TYPE", type}});
        (*wifi)(n.wifiEnabled(), "wifi_enabled", "wifi_disabled");
    });
    auto bluetooth = std::make_shared<Flip>(Flip{Bluetooth::get().powered()});
    Bluetooth::get().changed.connectForever([bluetooth] { (*bluetooth)(Bluetooth::get().powered(), "bluetooth_enabled", "bluetooth_disabled"); });
    auto vpn = std::make_shared<Flip>(Flip{Vpn::get().anyActive()});
    Vpn::get().changed.connectForever([vpn] { (*vpn)(Vpn::get().anyActive(), "vpn_connected", "vpn_disconnected", {{"M3_VPN_NAME", Vpn::get().activeName()}}); });

    // Notifications, Do not disturb and music
    Notifications::get().posted.connectForever([](uint32_t id) {
        const auto* n = Notifications::get().find(id);
        if (!n) return;
        fireSettled("notification_received", {{"M3_NOTIFICATION_APP", n->app},
                                                     {"M3_NOTIFICATION_SUMMARY", n->summary},
                                                     {"M3_NOTIFICATION_BODY", n->body},
                                                     {"M3_NOTIFICATION_URGENCY", urgency(n->urgency)}});
    });
    auto dnd = std::make_shared<Flip>(Flip{Notifications::get().doNotDisturb()});
    Notifications::get().changed.connectForever([dnd] { (*dnd)(Notifications::get().doNotDisturb(), "dnd_enabled", "dnd_disabled"); });
    auto track = std::make_shared<std::string>();
    if (const auto* p = Mpris::get().active()) *track = p->title + "\x1f" + p->artist;
    Mpris::get().changed.connectForever([track] {
        const auto* p = Mpris::get().active();
        if (!p || p->title.empty()) return;
        const std::string key = p->title + "\x1f" + p->artist;
        if (key == *track) return;
        *track = key;
        fireSettled("track_changed", {{"M3_MEDIA_TITLE", p->title}, {"M3_MEDIA_ARTIST", p->artist}, {"M3_MEDIA_ALBUM", p->album},
                                            {"M3_MEDIA_PLAYER", p->identity}});
    });

    // Workspaces, screenshots and recordings
    auto workspace = std::make_shared<int>(Hyprland::get().activeWorkspace(""));
    Hyprland::get().workspacesChanged.connectForever([workspace] {
        const int now = Hyprland::get().activeWorkspace("");
        if (now <= 0 || now == *workspace) return;
        const int was = *workspace;
        *workspace = now;
        fireSettled("workspace_changed", {{"M3_WORKSPACE", std::to_string(now)}, {"M3_WORKSPACE_PREVIOUS", std::to_string(was)}});
    });
    Screenshot::get().taken.connectForever([](const std::string& file) { fireSettled("screenshot_taken", {{"M3_FILE", file}}); });
    Screenshot::get().recorded.connectForever([](const std::string& file) { fireSettled("recording_saved", {{"M3_FILE", file}}); });
    auto recording = std::make_shared<bool>(Screenshot::get().recording());
    Screenshot::get().recordingChanged.connectForever([recording] {
        const bool now = Screenshot::get().recording();
        if (now && !*recording) fireSettled("recording_started");
        *recording = now;
    });

    // Digital Wellbeing's modes
    auto focus = std::make_shared<Flip>(Flip{Wellbeing::get().focus()});
    auto bedtime = std::make_shared<Flip>(Flip{Wellbeing::get().bedtime()});
    Wellbeing::get().changed.connectForever([focus, bedtime] {
        (*focus)(Wellbeing::get().focus(), "focus_enabled", "focus_disabled");
        (*bedtime)(Wellbeing::get().bedtime(), "bedtime_started", "bedtime_ended");
    });
}

} // namespace m3
