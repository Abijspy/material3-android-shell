#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "dbus/Bus.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace m3 {

// NetworkManager over the system bus: what the primary connection is, Wi-Fi
// signal strength and the Wi-Fi radio switch.
class Network {
public:
    enum class Kind { None, Wifi, Ethernet, Other };

    static Network& get();

    bool available() const { return m_available; }
    Kind kind() const { return m_kind; }
    const std::string& name() const { return m_name; } // SSID or connection id
    int strength() const { return m_strength; }         // 0–100, Wi-Fi only
    bool wifiEnabled() const { return m_wifiEnabled; }
    bool connecting() const { return m_connecting; }

    void setWifiEnabled(bool on);
    std::string icon() const;

    // Wi-Fi networks seen by the first wireless device, strongest first
    struct AccessPoint {
        std::string path, ssid;
        int strength = 0;
        bool secured = false, active = false, saved = false;
    };
    const std::vector<AccessPoint>& accessPoints() const { return m_aps; }
    void scan();
    // Saved or open networks connect directly; new secured ones need `password`
    void connect(const AccessPoint& ap, const std::string& password = {});
    void disconnect();
    // Deletes the saved connection for `ssid` (its password with it); a
    // network in use disconnects
    void forget(const std::string& ssid);
    std::string lastError() const { return m_error; }

    // Wired adapters and their state (NetworkManager)
    struct Wired {
        std::string path, iface = {}, ipv4 = {};
        int state = 0; // NM: 20 no cable, 30 disconnected, 40–90 connecting, 100 connected
        int speed = 0; // Mb/s, while linked
        bool cable() const { return state > 20; }
        bool connected() const { return state == 100; }
        bool connecting() const { return state > 30 && state < 100; }
    };
    const std::vector<Wired>& wired() const { return m_wired; }
    void connectWired(const std::string& path); // the device's best connection, as NM picks
    void disconnectWired(const std::string& path);

    // The primary connection in depth (Settings and Quick Settings show it)
    struct Details {
        std::string iface, mac, ipv4, gateway, dns, ipv6;
        std::string security;        // Wi-Fi: "WPA3", "WPA2", "Open"…
        int frequency = 0;           // Wi-Fi, MHz
        int linkRate = 0;            // Mb/s: Wi-Fi bitrate or Ethernet speed
        int maxRate = 0;             // Wi-Fi: the access point's best, Mb/s
        double rxRate = 0, txRate = 0; // bytes/s, while traffic is watched
        uint64_t rxTotal = 0, txTotal = 0;
        std::string band() const;    // "5 GHz · channel 36"
    };
    const Details& details() const { return m_details; }
    // Live download/upload rates, sampled each second while a lease is held
    using Lease = std::shared_ptr<void>;
    [[nodiscard]] Lease watchTraffic();

    Signal<> changed;
    Signal<> accessPointsChanged;
    Signal<> detailsChanged; // addresses, rates and traffic

private:
    Network();
    void findWifiDevice();
    void loadWired();
    void loadWiredDevice(const std::string& path);
    std::vector<Wired> m_wired;
    std::map<std::string, dbus::Subscription> m_wiredSubs;
    dbus::Subscription m_devicesAdded, m_devicesRemoved;
    EventLoop::TimerId m_wiredDebounce = 0;
    void loadAccessPoints();
    void refresh();
    void loadPrimary(const std::string& path);
    void loadAccessPoint(const std::string& path);
    void loadDetails(const dbus::Value& active);
    void sampleTraffic();

    bool m_available = false, m_wifiEnabled = false, m_connecting = false;
    Kind m_kind = Kind::None;
    std::string m_name, m_apPath, m_wifiDevice, m_error;
    std::vector<AccessPoint> m_aps;
    std::map<std::string, std::string> m_savedBySsid; // ssid → connection path
    dbus::Subscription m_devSub;
    int m_strength = 0;
    dbus::Subscription m_nmChanged, m_apChanged, m_owner;
    Details m_details;
    int m_trafficLeases = 0;
    EventLoop::TimerId m_trafficTimer = 0;
    EventLoop::TimerId m_signalTimer = 0;
    void sampleSignal();
    std::chrono::steady_clock::time_point m_lastSample{};
};

// BlueZ: adapter power, devices, discovery, and the pairing agent that
// asks the user to confirm or enter codes.
class Bluetooth {
public:
    using Lease = std::shared_ptr<void>;
    static Bluetooth& get();

    bool available() const { return m_available; }
    bool powered() const { return m_powered; }
    const std::string& adapter() const { return m_adapter; } // its BlueZ path
    const std::vector<std::string>& connected() const { return m_connected; }
    void setPowered(bool on);
    std::string icon() const;

    struct Device {
        std::string path, name, icon; // icon: BlueZ's freedesktop icon name
        bool paired = false, connected = false, trusted = false;
        bool named = false; // has a real name (unnamed ones are beacons and the like)
        int rssi = 0;
    };
    const std::vector<Device>& devices() const { return m_devices; } // connected, paired, then nearby
    bool discovering() const { return m_discovering; }
    void setDiscovering(bool on);
    void connect(const std::string& path);
    void disconnect(const std::string& path);
    void pair(const std::string& path); // then trust and connect
    void cancelPairing();
    void forget(const std::string& path);
    // "Pairing…", "Connecting…" while an action on that device runs
    std::string busy(const std::string& path) const;
    const std::string& lastError() const { return m_error; }

    // Discovery while a device list is open; stops with the last lease
    [[nodiscard]] Lease scan();

    // A code to confirm, show or enter, or a connection to allow
    struct Request {
        enum class Kind { None, Confirm, ShowCode, EnterPin, EnterPasskey, Authorize };
        Kind kind = Kind::None;
        std::string device, name, code; // code: the PIN or 6-digit passkey
    };
    const Request& request() const { return m_request; }
    void accept(const std::string& entered = {}); // confirm, allow, or submit what was typed
    void reject();
    // Views that show requests hold one; without any, `requested` fires
    [[nodiscard]] Lease showRequests();
    Signal<> changed;
    Signal<> requested; // a request arrived and no view is showing them

private:
    Bluetooth();
    void refresh();
    void registerAgent();
    int handleAgent(sd_bus_message* m);
    void ask(Request::Kind kind, sd_bus_message* m, const std::string& device, const std::string& code = {});
    void clearRequest();
    std::string nameOf(const std::string& path) const;

    Request m_request;
    sd_bus_message* m_pending = nullptr; // the agent call awaiting the user's answer
    sd_bus_slot* m_agentObject = nullptr;
    bool m_agentRegistered = false, m_wasPowered = false;
    int m_scanLeases = 0, m_requestViews = 0;
    std::map<std::string, std::string> m_busy;
    std::string m_error;

    bool m_available = false, m_powered = false, m_discovering = false;
    std::string m_adapter;
    std::vector<std::string> m_connected;
    std::vector<Device> m_devices;
    dbus::Subscription m_props, m_added, m_removed;
    EventLoop::TimerId m_debounce = 0;
};

// Battery from UPower's display device, or sysfs when UPower is absent;
// power profiles from power-profiles-daemon (or tuned-ppd) when running.
class Power {
public:
    static Power& get();

    bool hasBattery() const { return m_present; }
    int percent() const { return m_percent; }
    bool charging() const { return m_charging; }
    bool full() const { return m_full; }
    // On mains power (charging, full, or held below a charge limit)
    bool pluggedIn() const { return m_plugged; }
    long long secondsLeft() const { return m_seconds; } // to empty or full; 0 unknown
    double watts() const { return m_watts; }             // charge or discharge rate; 0 unknown
    int health() const { return m_health; }              // full charge vs design, %; 0 unknown
    std::string icon() const;
    // "Charging · 1 h 20 min to full", "On battery · 3 h 5 min left" …
    std::string status() const;

    // "power-saver", "balanced", "performance"; empty without the daemon
    const std::string& profile() const { return m_profile; }
    const std::vector<std::string>& profiles() const { return m_profiles; }
    void setProfile(const std::string& profile);

    Signal<> changed;

private:
    Power();
    void fromUPower(const dbus::Value& props);
    void pollSysfs();
    void findSysfs();
    void readHealth();
    void loadProfiles(size_t from = 0);
    void readMains();      // AC online: UPower's OnBattery, else sysfs
    void estimate();       // time left from energy over time, when the driver gives no rate
    void finish();         // after any reading: derived values, then changed

    bool m_present = false, m_charging = false, m_full = false, m_plugged = false;
    int m_percent = 0, m_health = 0;
    long long m_seconds = 0;
    double m_watts = 0;
    std::string m_sysfs;
    std::string m_profile, m_ppd, m_ppdPath; // the daemon's bus name and object, once found
    std::vector<std::string> m_profiles;
    dbus::Subscription m_sub, m_profileSub, m_mainsSub;
    EventLoop::TimerId m_poll = 0, m_sampler = 0;
    bool m_upowerMains = false; // OnBattery read from UPower (else sysfs)
    // Energy readings (µWh, or µAh) over time, for drivers with no rate
    std::vector<std::pair<std::chrono::steady_clock::time_point, double>> m_samples;
};

// Backlight through sysfs, written via logind so no root is needed.
class Brightness {
public:
    static Brightness& get();

    bool available() const { return !m_device.empty(); }
    float value() const; // 0–1
    void set(float v);
    void step(float delta);

    Signal<> changed;

private:
    Brightness();
    std::string m_device; // e.g. intel_backlight
    int m_max = 1, m_current = 0;
};

} // namespace m3
