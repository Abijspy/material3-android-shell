#include "services/Network.h"

#include "core/Guard.h"
#include "core/Log.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
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

uint64_t readU64(const std::string& path) {
    uint64_t v = 0;
    std::ifstream(path) >> v;
    return v;
}

// NM80211ApSecurityFlags
std::string securityName(uint32_t flags, uint32_t wpa, uint32_t rsn) {
    if (rsn & 0x400) return "WPA3";
    if (rsn & 0x200) return "WPA2 Enterprise";
    if (rsn) return "WPA2";
    if (wpa & 0x200) return "WPA Enterprise";
    if (wpa) return "WPA";
    if (rsn & 0x800 || wpa & 0x800) return "Enhanced open";
    return flags & 1 ? "WEP" : "Open";
}

// First address of an AddressData/NameserverData list (aa{sv})
std::string firstAddress(const dbus::Value& props, const char* key, bool withPrefix) {
    auto it = props.find(key);
    if (it == props.end() || !it->is_array() || it->empty()) return "";
    const auto& a = (*it)[0];
    std::string out = str(a, "address");
    if (withPrefix && !out.empty() && a.contains("prefix")) out += "/" + std::to_string(num<uint32_t>(a, "prefix"));
    return out;
}

std::string allAddresses(const dbus::Value& props, const char* key) {
    auto it = props.find(key);
    if (it == props.end() || !it->is_array()) return "";
    std::string out;
    for (const auto& a : *it)
        if (auto v = str(a, "address"); !v.empty()) out += (out.empty() ? "" : ", ") + v;
    return out;
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
    // Wired adapters come and go (USB adapters, docks)
    auto later = [this](const dbus::Value&, auto*) {
        EventLoop::get().debounce(m_wiredDebounce, 300, [this] { loadWired(); });
    };
    m_devicesAdded = bus.subscribe(kNM, kNMPath, kNM, "DeviceAdded", later);
    m_devicesRemoved = bus.subscribe(kNM, kNMPath, kNM, "DeviceRemoved", later);
    loadWired();
    // NetworkManager updates a connected network's strength only when it
    // rescans (minutes apart): read the live link quality instead
    m_signalTimer = EventLoop::get().addTimer(3000, [this] { sampleSignal(); }, true);
}

void Network::loadWired() {
    dbus::Bus::system().call(kNM, kNMPath, kNM, "GetDevices", {}, [this](const dbus::Value& a, const std::string& err) {
        if (!err.empty() || a.empty()) return;
        std::vector<std::string> paths;
        for (const auto& dev : a[0]) paths.push_back(dev.get<std::string>());
        // Gone devices leave
        const auto before = m_wired.size();
        std::erase_if(m_wired, [&](const Wired& w) { return std::ranges::find(paths, w.path) == paths.end(); });
        std::erase_if(m_wiredSubs, [&](const auto& kv) { return std::ranges::find(paths, kv.first) == paths.end(); });
        if (m_wired.size() != before) changed.emit();
        for (const auto& p : paths) loadWiredDevice(p);
    });
}

void Network::loadWiredDevice(const std::string& path) {
    auto& bus = dbus::Bus::system();
    bus.getAll(kNM, path, "org.freedesktop.NetworkManager.Device", [this, path](const dbus::Value& p, bool ok) {
        // DeviceType 1 is Ethernet
        if (!ok || num<uint32_t>(p, "DeviceType") != 1) return;
        auto it = std::ranges::find(m_wired, path, &Wired::path);
        if (it == m_wired.end()) {
            m_wired.push_back({path});
            it = std::prev(m_wired.end());
            // Its state changes (cable in or out, connected) arrive here
            m_wiredSubs[path] = dbus::Bus::system().onPropertiesChanged(kNM, path, [this, path](const dbus::Value&, const std::string&) {
                loadWiredDevice(path);
            });
        }
        it->iface = str(p, "Interface");
        it->state = static_cast<int>(num<uint32_t>(p, "State"));
        const std::string ip4 = str(p, "Ip4Config");
        changed.emit();
        dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.Device.Wired", [this, path](const dbus::Value& w, bool ok) {
            auto it = std::ranges::find(m_wired, path, &Wired::path);
            if (!ok || it == m_wired.end()) return;
            it->speed = static_cast<int>(num<uint32_t>(w, "Speed"));
            changed.emit();
        });
        if (ip4.empty() || ip4 == "/" || !it->connected()) {
            it->ipv4.clear();
            return;
        }
        dbus::Bus::system().getAll(kNM, ip4, "org.freedesktop.NetworkManager.IP4Config", [this, path](const dbus::Value& c, bool ok) {
            auto it = std::ranges::find(m_wired, path, &Wired::path);
            if (!ok || it == m_wired.end()) return;
            if (auto a = c.find("AddressData"); a != c.end() && a->is_array() && !a->empty() && (*a)[0].is_object())
                it->ipv4 = str((*a)[0], "address");
            changed.emit();
        });
    });
}

void Network::connectWired(const std::string& path) {
    m_error.clear();
    // "/" for the connection: NetworkManager picks the device's best one
    dbus::Bus::system().call(kNM, kNMPath, kNM, "ActivateConnection",
                             dbus::args(dbus::ObjectPath{"/"}, dbus::ObjectPath{path}, dbus::ObjectPath{"/"}),
                             [this](const dbus::Value&, const std::string& err) {
        if (!err.empty()) {
            m_error = "Couldn't connect the Ethernet: " + err.substr(err.find(": ") == std::string::npos ? 0 : err.find(": ") + 2);
            changed.emit();
        }
    });
}

void Network::disconnectWired(const std::string& path) {
    dbus::Bus::system().call(kNM, path, "org.freedesktop.NetworkManager.Device", "Disconnect");
}

void Network::sampleSignal() {
    if (m_kind != Kind::Wifi) return;
    // /proc/net/wireless: "wlan0: 0000   70.  -36.  -256 …" (link quality out of 70)
    std::ifstream in("/proc/net/wireless");
    std::string line;
    for (int i = 0; i < 2 && std::getline(in, line); ++i) {} // headers
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string iface, status;
        double link = 0;
        ss >> iface >> status >> link;
        if (!iface.empty() && iface.back() == ':') iface.pop_back();
        if (!m_details.iface.empty() && iface != m_details.iface) continue;
        const int pct = std::clamp(static_cast<int>(std::lround(link * 100 / 70)), 0, 100);
        // Small wobbles would rebuild open panels for nothing
        if (pct > 0 && std::abs(pct - m_strength) >= 3) {
            m_strength = pct;
            changed.emit();
        }
        return;
    }
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

void Network::forget(const std::string& ssid) {
    const auto saved = m_savedBySsid.find(ssid);
    if (saved == m_savedBySsid.end()) return;
    const std::string path = saved->second;
    m_savedBySsid.erase(saved);
    for (auto& ap : m_aps)
        if (ap.ssid == ssid) ap.saved = false;
    accessPointsChanged.emit();
    dbus::Bus::system().call(kNM, path, "org.freedesktop.NetworkManager.Settings.Connection", "Delete", {},
                             [this](const dbus::Value&, const std::string& err) {
                                 if (!err.empty()) log::warn("network: forgetting failed: {}", err);
                                 refresh();
                                 changed.emit();
                             });
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
        m_details = {};
        changed.emit();
        detailsChanged.emit();
        return;
    }
    dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.Connection.Active",
                               [this](const dbus::Value& p, bool ok) {
        if (!ok) return;
        const std::string type = str(p, "Type");
        m_name = str(p, "Id");
        loadDetails(p);
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
            if (c.contains("Frequency")) {
                m_details.frequency = num<int>(c, "Frequency");
                detailsChanged.emit();
            }
        });
    }
    dbus::Bus::system().getAll(kNM, path, "org.freedesktop.NetworkManager.AccessPoint", [this](const dbus::Value& p, bool) {
        m_strength = num<int>(p, "Strength");
        m_details.frequency = num<int>(p, "Frequency");
        m_details.maxRate = num<int>(p, "MaxBitrate") / 1000;
        m_details.security = securityName(num<uint32_t>(p, "Flags"), num<uint32_t>(p, "WpaFlags"), num<uint32_t>(p, "RsnFlags"));
        detailsChanged.emit();
        if (auto it = p.find("Ssid"); it != p.end() && it->is_binary()) {
            const auto& bytes = it->get_binary();
            m_name.assign(bytes.begin(), bytes.end());
        }
        changed.emit();
    });
}

void Network::loadDetails(const dbus::Value& active) {
    auto& bus = dbus::Bus::system();
    const bool wifi = str(active, "Type") == "802-11-wireless";
    // Fields are overwritten as replies arrive (NM refreshes often; clearing
    // first would flicker); a different connection starts from scratch
    if (!wifi) {
        m_details.frequency = m_details.maxRate = 0;
        m_details.security.clear();
    }
    if (const auto ip4 = str(active, "Ip4Config"); !ip4.empty() && ip4 != "/")
        bus.getAll(kNM, ip4, "org.freedesktop.NetworkManager.IP4Config", [this](const dbus::Value& p, bool ok) {
            if (!ok) return;
            m_details.ipv4 = firstAddress(p, "AddressData", true);
            m_details.gateway = str(p, "Gateway");
            m_details.dns = allAddresses(p, "NameserverData");
            detailsChanged.emit();
        });
    if (const auto ip6 = str(active, "Ip6Config"); !ip6.empty() && ip6 != "/")
        bus.getAll(kNM, ip6, "org.freedesktop.NetworkManager.IP6Config", [this](const dbus::Value& p, bool ok) {
            if (!ok) return;
            m_details.ipv6.clear();
            // The first global address; link-local ones say little
            if (auto it = p.find("AddressData"); it != p.end() && it->is_array())
                for (const auto& a : *it)
                    if (auto v = str(a, "address"); !v.empty() && !v.starts_with("fe80")) {
                        m_details.ipv6 = v;
                        break;
                    }
            detailsChanged.emit();
        });
    const auto devices = active.value("Devices", dbus::Value::array());
    if (devices.empty()) return detailsChanged.emit();
    const std::string dev = devices[0].get<std::string>();
    bus.getAll(kNM, dev, "org.freedesktop.NetworkManager.Device", [this](const dbus::Value& p, bool ok) {
        if (!ok) return;
        const std::string iface = str(p, "IpInterface").empty() ? str(p, "Interface") : str(p, "IpInterface");
        if (iface != m_details.iface) m_lastSample = {}; // other counters: no rate from the jump
        m_details.iface = iface;
        m_details.mac = str(p, "HwAddress");
        sampleTraffic();
        detailsChanged.emit();
    });
    bus.getAll(kNM, dev, wifi ? "org.freedesktop.NetworkManager.Device.Wireless" : "org.freedesktop.NetworkManager.Device.Wired",
               [this, wifi](const dbus::Value& p, bool ok) {
        if (!ok) return;
        // Wireless Bitrate is kbit/s; wired Speed is Mb/s
        m_details.linkRate = wifi ? num<int>(p, "Bitrate") / 1000 : num<int>(p, "Speed");
        if (m_details.mac.empty()) m_details.mac = str(p, "PermHwAddress");
        detailsChanged.emit();
    });
}

Network::Lease Network::watchTraffic() {
    if (m_trafficLeases++ == 0) {
        m_lastSample = {};
        sampleTraffic();
        m_trafficTimer = EventLoop::get().addTimer(1000, [this] {
            sampleTraffic();
            // The Wi-Fi bitrate moves with the radio: refresh it with the counters
            if (m_kind == Kind::Wifi && !m_wifiDevice.empty())
                dbus::Bus::system().getAll(kNM, m_wifiDevice, "org.freedesktop.NetworkManager.Device.Wireless",
                                           [this](const dbus::Value& p, bool ok) {
                    if (ok) m_details.linkRate = num<int>(p, "Bitrate") / 1000;
                });
            detailsChanged.emit();
        }, true);
    }
    return Lease(nullptr, [this](void*) {
        if (--m_trafficLeases == 0 && m_trafficTimer) {
            EventLoop::get().cancel(m_trafficTimer);
            m_trafficTimer = 0;
        }
    });
}

void Network::sampleTraffic() {
    if (m_details.iface.empty()) return;
    const std::string base = "/sys/class/net/" + m_details.iface + "/statistics/";
    const uint64_t rx = readU64(base + "rx_bytes"), tx = readU64(base + "tx_bytes");
    const auto now = std::chrono::steady_clock::now();
    if (m_lastSample != std::chrono::steady_clock::time_point{} && rx >= m_details.rxTotal && tx >= m_details.txTotal) {
        const double secs = std::chrono::duration<double>(now - m_lastSample).count();
        if (secs > 0.2) {
            m_details.rxRate = static_cast<double>(rx - m_details.rxTotal) / secs;
            m_details.txRate = static_cast<double>(tx - m_details.txTotal) / secs;
        }
    }
    m_details.rxTotal = rx;
    m_details.txTotal = tx;
    m_lastSample = now;
}

std::string Network::Details::band() const {
    if (frequency <= 0) return "";
    int channel = 0;
    std::string ghz;
    if (frequency < 3000) {
        ghz = "2.4 GHz";
        channel = frequency == 2484 ? 14 : (frequency - 2407) / 5;
    } else if (frequency < 5925) {
        ghz = "5 GHz";
        channel = (frequency - 5000) / 5;
    } else {
        ghz = "6 GHz";
        channel = (frequency - 5950) / 5;
    }
    return ghz + " · channel " + std::to_string(channel);
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
                         flag(*dev, "Trusted"), !str(*dev, "Name").empty(), num<int>(*dev, "RSSI")};
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
        if (m_available && !m_agentRegistered) registerAgent();
        // Switched on while a device list is open: look for devices
        if (m_powered && !m_wasPowered && m_scanLeases > 0 && !m_discovering) setDiscovering(true);
        m_wasPowered = m_powered;
        // A code shown for pairing started on the other device: done once paired
        if (m_request.kind == Request::Kind::ShowCode) {
            auto it = std::ranges::find(m_devices, m_request.device, &Device::path);
            if (it != m_devices.end() && it->paired) clearRequest();
        }
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

namespace {
const char* kAgentPath = "/org/material3ui/BluetoothAgent";

// BlueZ errors read "org.bluez.Error.AuthenticationFailed: …"; keep the words
std::string readable(const std::string& err) {
    const auto colon = err.find(": ");
    std::string s = colon != std::string::npos ? err.substr(colon + 2) : err;
    if (s.find("AuthenticationFailed") != std::string::npos || s == "Authentication Failed") return "the code was not accepted";
    if (s.find("AuthenticationCanceled") != std::string::npos || s == "Authentication Canceled") return "pairing was cancelled";
    if (s.find("ConnectionAttemptFailed") != std::string::npos || s.find("Page Timeout") != std::string::npos)
        return "it did not answer; is it nearby and in pairing mode?";
    return s;
}
} // namespace

std::string Bluetooth::nameOf(const std::string& path) const {
    auto it = std::ranges::find(m_devices, path, &Device::path);
    return it != m_devices.end() ? it->name : "the device";
}

std::string Bluetooth::busy(const std::string& path) const {
    auto it = m_busy.find(path);
    return it != m_busy.end() ? it->second : "";
}

void Bluetooth::connect(const std::string& path) {
    m_busy[path] = "Connecting…";
    m_error.clear();
    changed.emit();
    dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Connect", {}, [this, path](const dbus::Value&, const std::string& err) {
        m_busy.erase(path);
        if (!err.empty()) m_error = "Couldn't connect to " + nameOf(path) + ": " + readable(err);
        changed.emit();
    });
}

void Bluetooth::disconnect(const std::string& path) {
    dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Disconnect");
}

void Bluetooth::pair(const std::string& path) {
    m_busy[path] = "Pairing…";
    m_error.clear();
    changed.emit();
    dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "Pair", {}, [this, path](const dbus::Value&, const std::string& err) {
        m_busy.erase(path);
        if (!err.empty()) {
            // "Already exists": paired before; just connect
            if (err.find("AlreadyExists") == std::string::npos) {
                // Cancelled or declined here: nothing to report
                const bool ours = err.find("Rejected") != std::string::npos || err.find("Canceled") != std::string::npos;
                m_error = ours ? "" : "Couldn't pair with " + nameOf(path) + ": " + readable(err);
                clearRequest();
                changed.emit();
                return;
            }
        }
        // Remember it and use it right away
        dbus::Bus::system().setProperty(kBlueZ, path, "org.bluez.Device1", "Trusted", dbus::variant(true));
        clearRequest();
        connect(path);
    });
}

void Bluetooth::cancelPairing() {
    for (const auto& [path, what] : m_busy)
        if (what == "Pairing…") dbus::Bus::system().call(kBlueZ, path, "org.bluez.Device1", "CancelPairing");
    reject();
}

Bluetooth::Lease Bluetooth::scan() {
    if (m_scanLeases++ == 0 && m_powered) setDiscovering(true);
    return Lease(nullptr, [this](void*) {
        if (--m_scanLeases == 0 && m_discovering) setDiscovering(false);
    });
}

Bluetooth::Lease Bluetooth::showRequests() {
    ++m_requestViews;
    return Lease(nullptr, [this](void*) { --m_requestViews; });
}

// ── Pairing agent ──

void Bluetooth::registerAgent() {
    auto& bus = dbus::Bus::system();
    if (!bus.connected()) return;
    m_agentRegistered = true; // one attempt at a time; reset on failure
    if (!m_agentObject)
        sd_bus_add_object(bus.raw(), &m_agentObject, kAgentPath,
                          [](sd_bus_message* m, void* self, sd_bus_error*) {
                              int r = -EIO; // a method that throws answers with an error instead
                              guard("the pairing agent", [&] { r = static_cast<Bluetooth*>(self)->handleAgent(m); });
                              return r;
                          },
                          this);
    // KeyboardDisplay: we can both show codes and take them
    bus.call(kBlueZ, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
             dbus::args(dbus::ObjectPath{kAgentPath}, "KeyboardDisplay"), [this](const dbus::Value&, const std::string& err) {
        if (!err.empty() && err.find("AlreadyExists") == std::string::npos) {
            log::warn("bluetooth: agent not registered ({})", err);
            m_agentRegistered = false;
            return;
        }
        // Default: pairing started from the phone asks us too
        dbus::Bus::system().call(kBlueZ, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent",
                                 dbus::args(dbus::ObjectPath{kAgentPath}));
    });
}

void Bluetooth::ask(Request::Kind kind, sd_bus_message* m, const std::string& device, const std::string& code) {
    reject(); // one question at a time
    if (m) m_pending = sd_bus_message_ref(m);
    m_request = {kind, device, nameOf(device), code};
    changed.emit();
    if (m_requestViews == 0) requested.emit();
}

void Bluetooth::clearRequest() {
    if (m_pending) sd_bus_message_unref(m_pending);
    m_pending = nullptr;
    if (m_request.kind == Request::Kind::None) return;
    m_request = {};
    changed.emit();
}

void Bluetooth::accept(const std::string& entered) {
    if (m_pending) {
        switch (m_request.kind) {
        case Request::Kind::EnterPin: sd_bus_reply_method_return(m_pending, "s", entered.c_str()); break;
        case Request::Kind::EnterPasskey: {
            char* end = nullptr;
            const unsigned long key = std::strtoul(entered.c_str(), &end, 10);
            if (entered.empty() || *end || key > 999999) return; // six digits, nothing else
            sd_bus_reply_method_return(m_pending, "u", static_cast<uint32_t>(key));
            break;
        }
        default: sd_bus_reply_method_return(m_pending, ""); break;
        }
    }
    clearRequest();
}

void Bluetooth::reject() {
    if (m_pending) sd_bus_reply_method_errorf(m_pending, "org.bluez.Error.Rejected", "Rejected by the user");
    clearRequest();
}

int Bluetooth::handleAgent(sd_bus_message* m) {
    if (!sd_bus_message_is_method_call(m, "org.bluez.Agent1", nullptr)) return 0;
    const std::string member = sd_bus_message_get_member(m);
    const char* device = nullptr;
    if (member == "Release") {
        m_agentRegistered = false;
        clearRequest();
        return sd_bus_reply_method_return(m, "");
    }
    if (member == "Cancel") {
        clearRequest();
        return sd_bus_reply_method_return(m, "");
    }
    if (member == "RequestPinCode" || member == "RequestPasskey" || member == "RequestAuthorization") {
        sd_bus_message_read(m, "o", &device);
        ask(member == "RequestPinCode" ? Request::Kind::EnterPin : member == "RequestPasskey" ? Request::Kind::EnterPasskey
                                                                                              : Request::Kind::Authorize,
            m, device ? device : "");
        return 1; // answered when the user decides
    }
    if (member == "RequestConfirmation") {
        uint32_t passkey = 0;
        sd_bus_message_read(m, "ou", &device, &passkey);
        ask(Request::Kind::Confirm, m, device ? device : "", std::format("{:06d}", passkey));
        return 1;
    }
    if (member == "DisplayPinCode" || member == "DisplayPasskey") {
        std::string code;
        if (member == "DisplayPinCode") {
            const char* pin = nullptr;
            sd_bus_message_read(m, "os", &device, &pin);
            code = pin ? pin : "";
        } else {
            uint32_t passkey = 0;
            uint16_t entered = 0;
            sd_bus_message_read(m, "ouq", &device, &passkey, &entered);
            code = std::format("{:06d}", passkey);
        }
        // Shown, not answered: the other side types it
        ask(Request::Kind::ShowCode, nullptr, device ? device : "", code);
        return sd_bus_reply_method_return(m, "");
    }
    if (member == "AuthorizeService") {
        const char* uuid = nullptr;
        sd_bus_message_read(m, "os", &device, &uuid);
        // Paired or trusted devices may use their services; others are asked
        auto it = std::ranges::find(m_devices, std::string(device ? device : ""), &Device::path);
        if (it != m_devices.end() && (it->paired || it->trusted)) return sd_bus_reply_method_return(m, "");
        ask(Request::Kind::Authorize, m, device ? device : "");
        return 1;
    }
    return 0;
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
    // Plugging in or out: UPower's own object says, even when the battery's
    // numbers don't move (full, or held at a charge limit)
    m_mainsSub = dbus::Bus::system().onPropertiesChanged("org.freedesktop.UPower", "/org/freedesktop/UPower",
                                                         [this](const dbus::Value&, const std::string&) { readMains(); });
    load();
    findSysfs();
    readHealth();
    readMains();
    loadProfiles();
    // Energy every minute, for drivers that report no rate (time left)
    m_sampler = EventLoop::get().addTimer(60000, [this] { estimate(); }, true);
}

void Power::readMains() {
    dbus::Bus::system().getAll("org.freedesktop.UPower", "/org/freedesktop/UPower", "org.freedesktop.UPower",
                               [this](const dbus::Value& p, bool ok) {
        if (ok && p.contains("OnBattery")) {
            m_upowerMains = true;
            const bool plugged = !flag(p, "OnBattery");
            if (plugged != m_plugged) {
                m_plugged = plugged;
                m_samples.clear(); // a new direction: start the estimate again
                finish();
            }
            return;
        }
        // No UPower: the mains adapter in sysfs
        namespace fs = std::filesystem;
        std::error_code ec;
        bool plugged = false;
        for (const auto& e : fs::directory_iterator("/sys/class/power_supply", ec)) {
            std::string type;
            std::ifstream(e.path() / "type") >> type;
            if (type == "Mains" || type == "USB") plugged = plugged || readInt((e.path() / "online").string()) == 1;
        }
        if (plugged != m_plugged) {
            m_plugged = plugged;
            m_samples.clear();
            finish();
        }
    });
}

void Power::estimate() {
    if (m_sysfs.empty() || (m_watts > 0 && m_seconds > 0)) return;
    // Energy (µWh) or charge (µAh), whichever the driver keeps
    double now = 0, full = 0;
    for (const char* kind : {"energy", "charge"}) {
        long long n = -1, f = -1;
        std::ifstream(m_sysfs + "/" + kind + "_now") >> n;
        std::ifstream(m_sysfs + "/" + kind + "_full") >> f;
        if (n >= 0 && f > 0) {
            now = static_cast<double>(n);
            full = static_cast<double>(f);
            break;
        }
    }
    if (full <= 0) return;
    const auto t = std::chrono::steady_clock::now();
    m_samples.push_back({t, now});
    // A quarter of an hour of readings is plenty, and follows changes in load
    while (m_samples.size() > 16) m_samples.erase(m_samples.begin());
    if (m_samples.size() < 3) return;
    const double hours = std::chrono::duration<double, std::ratio<3600>>(t - m_samples.front().first).count();
    const double perHour = (now - m_samples.front().second) / hours; // + charging, - draining
    if (std::abs(perHour) < full * 0.002) return;                     // too little change to tell
    const double secs = perHour < 0 ? now / -perHour * 3600 : (full - now) / perHour * 3600;
    if ((perHour < 0 && !m_charging) || (perHour > 0 && m_charging)) {
        m_seconds = static_cast<long long>(secs);
        changed.emit();
    }
}

void Power::finish() {
    // A full battery on mains isn't charging, whatever the firmware says
    if (m_plugged && m_percent >= 100) m_full = true;
    if (!m_plugged) m_full = false;
    changed.emit();
}

void Power::findSysfs() {
    namespace fs = std::filesystem;
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

void Power::readHealth() {
    if (m_sysfs.empty()) return;
    // Energy (µWh) or charge (µAh), whichever the driver reports
    long long full = 0, design = 0;
    for (const char* kind : {"energy", "charge"}) {
        std::ifstream(m_sysfs + "/" + kind + "_full") >> full;
        std::ifstream(m_sysfs + "/" + kind + "_full_design") >> design;
        if (full > 0 && design > 0) break;
    }
    m_health = full > 0 && design > 0 ? static_cast<int>(std::lround(100.0 * static_cast<double>(full) / static_cast<double>(design))) : 0;
}

namespace {
// The UPower name first; power-profiles-daemon before 0.20 and tuned-ppd
// answer on the old one
const std::pair<const char*, const char*> kProfileDaemons[] = {
    {"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles"},
    {"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles"},
};
} // namespace

void Power::loadProfiles(size_t from) {
    if (from >= std::size(kProfileDaemons)) return;
    // Once found, only that daemon is reread
    const std::string name = m_ppd.empty() ? kProfileDaemons[from].first : m_ppd;
    const std::string path = m_ppd.empty() ? kProfileDaemons[from].second : m_ppdPath;
    dbus::Bus::system().getAll(name, path, name, [this, name, path, from](const dbus::Value& p, bool ok) {
        if (!ok) {
            if (m_ppd.empty()) loadProfiles(from + 1);
            return;
        }
        m_profile = str(p, "ActiveProfile");
        m_profiles.clear();
        if (auto it = p.find("Profiles"); it != p.end() && it->is_array())
            for (const auto& entry : *it)
                if (entry.is_object()) m_profiles.push_back(str(entry, "Profile"));
        if (m_ppd.empty()) {
            m_ppd = name;
            m_ppdPath = path;
            m_profileSub = dbus::Bus::system().onPropertiesChanged(name, path, [this](const dbus::Value&, const std::string&) { loadProfiles(); });
        }
        changed.emit();
    });
}

void Power::setProfile(const std::string& profile) {
    if (m_ppd.empty() || profile == m_profile) return;
    m_profile = profile; // shown at once; the daemon's change confirms it
    changed.emit();
    dbus::Bus::system().setProperty(m_ppd, m_ppdPath, m_ppd, "ActiveProfile", dbus::variant(profile));
}

void Power::fromUPower(const dbus::Value& p) {
    // DisplayDevice type 2 is a battery; desktops report 0 with IsPresent false
    m_present = flag(p, "IsPresent") && num<uint32_t>(p, "Type") == 2;
    m_percent = static_cast<int>(std::lround(num<double>(p, "Percentage")));
    // 1 charging, 2 discharging, 4 full, 5 plugged in but held (a charge
    // limit), 6 about to discharge
    const auto state = num<uint32_t>(p, "State");
    const bool wasCharging = m_charging;
    m_charging = state == 1;
    m_full = state == 4;
    if (state == 1 || state == 4 || state == 5) m_plugged = true;
    if (state == 2 || state == 6) m_plugged = m_upowerMains ? m_plugged : false;
    if (m_charging != wasCharging) m_samples.clear();
    const long long secs = m_charging ? num<long long>(p, "TimeToFull") : num<long long>(p, "TimeToEmpty");
    m_watts = num<double>(p, "EnergyRate");
    // Keep an estimate made from energy readings when UPower has none
    if (secs > 0 || m_watts > 0) m_seconds = secs;
    else if (m_charging != wasCharging) m_seconds = 0;
    readHealth();
    finish();
}

void Power::pollSysfs() {
    if (m_sysfs.empty()) findSysfs();
    m_present = !m_sysfs.empty();
    if (m_present) {
        m_percent = readInt(m_sysfs + "/capacity");
        std::string status;
        std::ifstream(m_sysfs + "/status") >> status;
        m_charging = status == "Charging";
        m_full = status == "Full";
        // "Not charging": on mains, held at a limit
        if (status == "Charging" || status == "Full" || status == "Not") m_plugged = true;
        // power_now in µW; absent on charge-reporting drivers
        long long power = 0;
        std::ifstream(m_sysfs + "/power_now") >> power;
        m_watts = static_cast<double>(power) / 1e6;
    }
    finish();
}

std::string Power::icon() const {
    // Charging: the bolt, at the level it has reached
    if (m_charging) {
        for (auto [below, name] : {std::pair{25, "battery_charging_20"}, {40, "battery_charging_30"}, {55, "battery_charging_50"},
                                   {70, "battery_charging_60"}, {85, "battery_charging_80"}, {95, "battery_charging_90"}})
            if (m_percent < below) return name;
        return "battery_charging_full";
    }
    if (m_full || m_percent >= 95) return "battery_full";
    // Material Symbols has battery_0_bar … battery_6_bar
    return "battery_" + std::to_string(std::clamp(m_percent * 7 / 100, 0, 6)) + "_bar";
}

std::string Power::status() const {
    if (m_full) return "Fully charged";
    std::string left;
    if (m_seconds > 0) {
        const long long h = m_seconds / 3600, min = (m_seconds % 3600) / 60;
        left = h > 0 ? std::to_string(h) + " h " + std::to_string(min) + " min" : std::to_string(min) + " min";
    }
    if (m_charging) return left.empty() ? "Charging" : "Charging · " + left + " to full";
    if (m_plugged) return "Plugged in, not charging";
    return left.empty() ? "On battery" : "On battery · " + left + " left";
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
