#include "services/Network.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <systemd/sd-bus.h>

namespace m3 {

namespace {
const char* kNM = "org.freedesktop.NetworkManager";
const char* kNMPath = "/org/freedesktop/NetworkManager";
const char* kBlueZ = "org.bluez";

std::string str(const dbus::Value& props, const char* key) {
    auto it = props.find(key);
    return it != props.end() && it->is_string() ? it->get<std::string>() : "";
}

template <typename T>
T num(const dbus::Value& props, const char* key, T fallback = {}) {
    auto it = props.find(key);
    return it != props.end() && it->is_number() ? it->get<T>() : fallback;
}

bool flag(const dbus::Value& props, const char* key) {
    auto it = props.find(key);
    return it != props.end() && it->is_boolean() && it->get<bool>();
}

int readInt(const std::string& path) {
    int v = 0;
    std::ifstream(path) >> v;
    return v;
}

} // namespace

// ── Network ──

Network& Network::get() {
    static Network instance;
    return instance;
}

Network::Network() {
    auto& bus = dbus::Bus::system();
    m_nmChanged = bus.onPropertiesChanged(kNM, kNMPath, [this](const dbus::Value&, const std::string&) { refresh(); });
    m_owner = bus.subscribe("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                            [this](const dbus::Value& a, auto*) {
                                if (!a.empty() && a[0] == kNM) refresh();
                            });
    refresh();
    findWifiDevice();
}

void Network::findWifiDevice() {
    dbus::Bus::system().call(kNM, kNMPath, kNM, "GetDevices", {}, [this](const dbus::Value& a, const std::string& err) {
        if (!err.empty() || a.empty()) return;
        for (const auto& dev : a[0]) {
            const std::string path = dev.get<std::string>();
            dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.Device", [this, path](const dbus::Value& p, bool ok) {
                // DeviceType 2 is Wi-Fi
                if (!ok || num<uint32_t>(p, "DeviceType") != 2 || !m_wifiDevice.empty()) return;
                m_wifiDevice = path;
                m_devSub = dbus::Bus::system().subscribe(kNM, path, "org.freedesktop.NetworkManager.Device.Wireless", "",
                                                         [this](const dbus::Value&, auto*) { loadAccessPoints(); });
                loadAccessPoints();
            });
        }
    });
}

void Network::scan() {
    if (m_wifiDevice.empty()) return;
    dbus::Bus::system().call(kNM, m_wifiDevice, "org.freedesktop.NetworkManager.Device.Wireless", "RequestScan",
                             [](sd_bus_message* m) { sd_bus_message_append(m, "a{sv}", 0); },
                             [this](const dbus::Value&, const std::string&) { loadAccessPoints(); });
}

void Network::loadAccessPoints() {
    if (m_wifiDevice.empty()) return;
    auto& bus = dbus::Bus::system();
    // Saved Wi-Fi connections, by SSID, so known networks connect without a password
    bus.call(kNM, "/org/freedesktop/NetworkManager/Settings", "org.freedesktop.NetworkManager.Settings", "ListConnections", {},
             [this](const dbus::Value& a, const std::string& err) {
                 if (!err.empty() || a.empty()) return;
                 for (const auto& c : a[0]) {
                     const std::string path = c.get<std::string>();
                     dbus::Bus::system().call(kNM, path, "org.freedesktop.NetworkManager.Settings.Connection", "GetSettings", {},
                                              [this, path](const dbus::Value& r, const std::string& e) {
                         if (!e.empty() || r.empty()) return;
                         auto w = r[0].find("802-11-wireless");
                         if (w == r[0].end()) return;
                         if (auto ssid = w->find("ssid"); ssid != w->end() && ssid->is_binary()) {
                             const auto& b = ssid->get_binary();
                             m_savedBySsid[std::string(b.begin(), b.end())] = path;
                             for (auto& ap : m_aps) ap.saved = m_savedBySsid.contains(ap.ssid);
                             accessPointsChanged.emit();
                         }
                     });
                 }
             });
    bus.call(kNM, m_wifiDevice, "org.freedesktop.NetworkManager.Device.Wireless", "GetAllAccessPoints", {},
             [this](const dbus::Value& a, const std::string& err) {
        if (!err.empty() || a.empty()) return;
        auto list = std::make_shared<std::vector<AccessPoint>>();
        auto remaining = std::make_shared<size_t>(a[0].size());
        if (*remaining == 0) {
            m_aps.clear();
            accessPointsChanged.emit();
        }
        for (const auto& p : a[0]) {
            const std::string path = p.get<std::string>();
            dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.AccessPoint",
                                       [this, path, list, remaining](const dbus::Value& props, bool ok) {
                if (ok) {
                    AccessPoint ap;
                    ap.path = path;
                    if (auto it = props.find("Ssid"); it != props.end() && it->is_binary())
                        ap.ssid.assign(it->get_binary().begin(), it->get_binary().end());
                    ap.strength = num<int>(props, "Strength");
                    ap.secured = (num<uint32_t>(props, "Flags") & 1) || num<uint32_t>(props, "WpaFlags") ||
                                 num<uint32_t>(props, "RsnFlags");
                    ap.active = path == m_apPath;
                    ap.saved = m_savedBySsid.contains(ap.ssid);
                    if (!ap.ssid.empty()) list->push_back(ap);
                }
                if (--*remaining > 0) return;
                // One entry per SSID (several radios share one), strongest first
                std::ranges::sort(*list, [](auto& x, auto& y) { return x.active != y.active ? x.active : x.strength > y.strength; });
                std::vector<AccessPoint> unique;
                for (auto& ap : *list)
                    if (std::ranges::find(unique, ap.ssid, &AccessPoint::ssid) == unique.end()) unique.push_back(ap);
                m_aps = std::move(unique);
                accessPointsChanged.emit();
            });
        }
    });
}

void Network::connect(const AccessPoint& ap, const std::string& password) {
    if (m_wifiDevice.empty()) return;
    m_error.clear();
    auto done = [this](const dbus::Value&, const std::string& err) {
        if (!err.empty()) log::warn("network: connecting failed: {}", err);
        m_error = err.empty() ? "" : "Could not connect";
        refresh();
        changed.emit();
    };
    if (auto saved = m_savedBySsid.find(ap.ssid); saved != m_savedBySsid.end() && password.empty()) {
        dbus::Bus::system().call(kNM, kNMPath, kNM, "ActivateConnection",
                                 dbus::args(dbus::ObjectPath{saved->second}, dbus::ObjectPath{m_wifiDevice}, dbus::ObjectPath{ap.path}),
                                 done);
        return;
    }
    // A new network: NetworkManager fills in the rest from the access point
    const std::string device = m_wifiDevice, apPath = ap.path;
    dbus::Bus::system().call(kNM, kNMPath, kNM, "AddAndActivateConnection",
                             [password, device, apPath](sd_bus_message* m) {
                                 if (password.empty())
                                     sd_bus_message_append(m, "a{sa{sv}}", 0);
                                 else
                                     sd_bus_message_append(m, "a{sa{sv}}", 1, "802-11-wireless-security", 2, "key-mgmt", "s",
                                                           "wpa-psk", "psk", "s", password.c_str());
                                 sd_bus_message_append(m, "oo", device.c_str(), apPath.c_str());
                             },
                             done);
}

void Network::disconnect() {
    if (!m_wifiDevice.empty())
        dbus::Bus::system().call(kNM, m_wifiDevice, "org.freedesktop.NetworkManager.Device", "Disconnect");
}

void Network::refresh() {
    dbus::Bus::system().getAll(kNM, kNMPath, kNM, [this](const dbus::Value& p, bool ok) {
        m_available = ok;
        if (!ok) {
            m_kind = Kind::None;
            changed.emit();
            return;
        }
        m_wifiEnabled = flag(p, "WirelessEnabled");
        const auto state = num<uint32_t>(p, "State");
        m_connecting = state == 40;
        loadPrimary(str(p, "PrimaryConnection"));
    });
}

void Network::loadPrimary(const std::string& path) {
    if (path.empty() || path == "/") {
        m_kind = Kind::None;
        m_name.clear();
        m_apChanged = {};
        changed.emit();
        return;
    }
    dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.Connection.Active",
                               [this](const dbus::Value& p, bool ok) {
        if (!ok) return;
        const std::string type = str(p, "Type");
        m_name = str(p, "Id");
        if (type == "802-11-wireless") {
            m_kind = Kind::Wifi;
            const auto devices = p.value("Devices", dbus::Value::array());
            if (devices.empty()) return changed.emit();
            dbus::Bus::system().getAll(kNM, devices[0].get<std::string>(), "org.freedesktop.NetworkManager.Device.Wireless",
                                       [this](const dbus::Value& d, bool) { loadAccessPoint(str(d, "ActiveAccessPoint")); });
            return;
        }
        m_kind = type == "802-3-ethernet" ? Kind::Ethernet : Kind::Other;
        m_apChanged = {};
        changed.emit();
    });
}

void Network::loadAccessPoint(const std::string& path) {
    if (path.empty() || path == "/") return changed.emit();
    if (path != m_apPath) {
        m_apPath = path;
        // Signal strength changes arrive on the access point object
        m_apChanged = dbus::Bus::system().onPropertiesChanged(kNM, path, [this](const dbus::Value& c, const std::string&) {
            if (c.contains("Strength")) {
                m_strength = c["Strength"].get<int>();
                changed.emit();
            }
        });
    }
    dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.AccessPoint", [this](const dbus::Value& p, bool) {
        m_strength = num<int>(p, "Strength");
        if (auto it = p.find("Ssid"); it != p.end() && it->is_binary()) {
            const auto& bytes = it->get_binary();
            m_name.assign(bytes.begin(), bytes.end());
        }
        changed.emit();
    });
}

void Network::setWifiEnabled(bool on) {
    dbus::Bus::system().setProperty(kNM, kNMPath, kNM, "WirelessEnabled", dbus::variant(on));
}

std::string Network::icon() const {
    switch (m_kind) {
    case Kind::Ethernet: return "lan";
    case Kind::Other: return "vpn_key";
    case Kind::Wifi:
        if (m_strength >= 80) return "signal_wifi_4_bar";
        if (m_strength >= 55) return "network_wifi_3_bar";
        if (m_strength >= 30) return "network_wifi_2_bar";
        return "network_wifi_1_bar";
    case Kind::None: break;
    }
    if (!m_available) return "signal_disconnected";
    return m_wifiEnabled ? "signal_wifi_0_bar" : "signal_wifi_off";
}

// ── Bluetooth ──

Bluetooth& Bluetooth::get() {
    static Bluetooth instance;
    return instance;
}

Bluetooth::Bluetooth() {
    auto& bus = dbus::Bus::system();
    auto later = [this](const dbus::Value&, auto*) {
        EventLoop::get().debounce(m_debounce, 100, [this] { refresh(); });
    };
    m_props = bus.subscribe(kBlueZ, "", "org.freedesktop.DBus.Properties", "PropertiesChanged", later);
    m_added = bus.subscribe(kBlueZ, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", later);
    m_removed = bus.subscribe(kBlueZ, "/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", later);
    refresh();
}

void Bluetooth::refresh() {
    dbus::Bus::system().call(kBlueZ, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", {},
                             [this](const dbus::Value& a, const std::string& err) {
        m_available = false;
        m_powered = false;
        m_connected.clear();
        m_devices.clear();
        if (err.empty() && !a.empty() && a[0].is_object()) {
            for (const auto& [path, ifaces] : a[0].items()) {
                if (auto ad = ifaces.find("org.bluez.Adapter1"); ad != ifaces.end() && !m_available) {
                    m_available = true;
                    m_adapter = path;
                    m_powered = flag(*ad, "Powered");
                    m_discovering = flag(*ad, "Discovering");
                }
                auto dev = ifaces.find("org.bluez.Device1");
                if (dev == ifaces.end()) continue;
                Device d{path, str(*dev, "Alias"), str(*dev, "Icon"), flag(*dev, "Paired"), flag(*dev, "Connected"),
                         flag(*dev, "Trusted"), num<int>(*dev, "RSSI")};
                if (d.name.empty()) d.name = str(*dev, "Address");
                if (d.connected) m_connected.push_back(d.name);
                m_devices.push_back(std::move(d));
            }
        }
        std::ranges::sort(m_devices, [](const Device& x, const Device& y) {
            if (x.connected != y.connected) return x.connected;
            if (x.paired != y.paired) return x.paired;
            return x.rssi > y.rssi;
        });
        changed.emit();
    });
}

void Bluetooth::setPowered(bool on) {
    if (m_available) dbus::Bus::system().setProperty(kBlueZ, m_adapter, "org.bluez.Adapter1", "Powered", dbus::variant(on));
}

void Bluetooth::setDiscovering(bool on) {
    if (!m_available) return;
    dbus::Bus::system().call(kBlueZ, m_adapter, "org.bluez.Adapter1", on ? "StartDiscovery" : "StopDiscovery", {},
                             [this](const dbus::Value&, const std::string&) { refresh(); });
}

void Bluetooth::connect(const std::string& path) {
    dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Connect");
}

void Bluetooth::disconnect(const std::string& path) {
    dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Disconnect");
}

void Bluetooth::pair(const std::string& path) {
    dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Pair", {}, [path](const dbus::Value&, const std::string& err) {
        if (!err.empty()) return;
        // Remember it and use it right away
        dbus::Bus::system().setProperty(kBlueZ, path, "org.bluez.Device1", "Trusted", dbus::variant(true));
        dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Connect");
    });
}

void Bluetooth::forget(const std::string& path) {
    if (m_available) dbus::Bus::system().call(kBlueZ, m_adapter, "org.bluez.Adapter1", "RemoveDevice", dbus::args(dbus::ObjectPath{path}));
}

std::string Bluetooth::icon() const {
    if (!m_powered) return "bluetooth_disabled";
    return m_connected.empty() ? "bluetooth" : "bluetooth_connected";
}

// ── Power ──

Power& Power::get() {
    static Power instance;
    return instance;
}

Power::Power() {
    const std::string dev = "/org/freedesktop/UPower/devices/DisplayDevice";
    auto load = [this, dev] {
        dbus::Bus::system().getAll("org.freedesktop.UPower", dev, "org.freedesktop.UPower.Device",
                                   [this](const dbus::Value& p, bool ok) {
            if (ok) {
                fromUPower(p);
            } else if (!m_poll) {
                pollSysfs();
                m_poll = EventLoop::get().addTimer(30000, [this] { pollSysfs(); }, true);
            }
        });
    };
    m_sub = dbus::Bus::system().onPropertiesChanged("org.freedesktop.UPower", dev,
                                                    [load](const dbus::Value&, const std::string&) { load(); });
    load();
}

void Power::fromUPower(const dbus::Value& p) {
    // DisplayDevice type 2 is a battery; desktops report 0 with IsPresent false
    m_present = flag(p, "IsPresent") && num<uint32_t>(p, "Type") == 2;
    m_percent = static_cast<int>(std::lround(num<double>(p, "Percentage")));
    const auto state = num<uint32_t>(p, "State");
    m_charging = state == 1 || state == 5;
    m_full = state == 4;
    m_seconds = m_charging ? num<long long>(p, "TimeToFull") : num<long long>(p, "TimeToEmpty");
    changed.emit();
}

void Power::pollSysfs() {
    namespace fs = std::filesystem;
    if (m_sysfs.empty()) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator("/sys/class/power_supply", ec)) {
            std::string type;
            std::ifstream(e.path() / "type") >> type;
            if (type == "Battery") {
                m_sysfs = e.path();
                break;
            }
        }
    }
    m_present = !m_sysfs.empty();
    if (m_present) {
        m_percent = readInt(m_sysfs + "/capacity");
        std::string status;
        std::ifstream(m_sysfs + "/status") >> status;
        m_charging = status == "Charging";
        m_full = status == "Full";
        m_seconds = 0;
    }
    changed.emit();
}

std::string Power::icon() const {
    if (m_charging) return "battery_charging_full";
    if (m_full || m_percent >= 95) return "battery_full";
    // Material Symbols has battery_0_bar … battery_6_bar
    return "battery_" + std::to_string(std::clamp(m_percent * 7 / 100, 0, 6)) + "_bar";
}

// ── Brightness ──

Brightness& Brightness::get() {
    static Brightness instance;
    return instance;
}

Brightness::Brightness() {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/backlight", ec)) {
        m_device = e.path().filename();
        m_max = std::max(1, readInt(e.path() / "max_brightness"));
        m_current = readInt(e.path() / "brightness");
        break;
    }
}

float Brightness::value() const {
    return static_cast<float>(m_current) / static_cast<float>(m_max);
}

void Brightness::set(float v) {
    if (m_device.empty()) return;
    // Never fully dark: 1% floor so the screen stays usable
    m_current = std::clamp(static_cast<int>(std::lround(v * static_cast<float>(m_max))), std::max(1, m_max / 100), m_max);
    dbus::Bus::system().call("org.freedesktop.login1", "/org/freedesktop/login1/session/auto",
                             "org.freedesktop.login1.Session", "SetBrightness",
                             dbus::args("backlight", m_device, static_cast<uint32_t>(m_current)));
    changed.emit();
}

void Brightness::step(float delta) {
    if (m_device.empty()) return;
    // Something else may have changed it (keys handled by the firmware)
    m_current = readInt("/sys/class/backlight/" + m_device + "/brightness");
    set(value() + delta);
}

} // namespace m3
