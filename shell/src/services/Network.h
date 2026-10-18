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
    std::string lastError() const { return m_error; }

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
    std::chrono::steady_clock::time_point m_lastSample{};
};

// BlueZ: adapter power and connected devices.
class Bluetooth {
public:
    static Bluetooth& get();

    bool available() const { return m_available; }
    bool powered() const { return m_powered; }
    const std::vector<std::string>& connected() const { return m_connected; }
    void setPowered(bool on);
    std::string icon() const;

    struct Device {
        std::string path, name, icon; // icon: BlueZ's freedesktop icon name
        bool paired = false, connected = false, trusted = false;
        int rssi = 0;
    };
    const std::vector<Device>& devices() const { return m_devices; } // connected, paired, then nearby
    bool discovering() const { return m_discovering; }
    void setDiscovering(bool on);
    void connect(const std::string& path);
    void disconnect(const std::string& path);
    void pair(const std::string& path); // then trust and connect
    void forget(const std::string& path);

    Signal<> changed;

private:
    Bluetooth();
    void refresh();

    bool m_available = false, m_powered = false, m_discovering = false;
    std::string m_adapter;
    std::vector<std::string> m_connected;
    std::vector<Device> m_devices;
    dbus::Subscription m_props, m_added, m_removed;
    EventLoop::TimerId m_debounce = 0;
};

// Battery from UPower's display device, or sysfs when UPower is absent.
class Power {
public:
    static Power& get();

    bool hasBattery() const { return m_present; }
    int percent() const { return m_percent; }
    bool charging() const { return m_charging; }
    bool full() const { return m_full; }
    long long secondsLeft() const { return m_seconds; } // to empty or full; 0 unknown
    std::string icon() const;

    Signal<> changed;

private:
    Power();
    void fromUPower(const dbus::Value& props);
    void pollSysfs();

    bool m_present = false, m_charging = false, m_full = false;
    int m_percent = 0;
    long long m_seconds = 0;
    std::string m_sysfs;
    dbus::Subscription m_sub;
    EventLoop::TimerId m_poll = 0;
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
