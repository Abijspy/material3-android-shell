#include "services/KdeConnect.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Process.h"

#include <algorithm>
#include <memory>

namespace m3 {

namespace {

constexpr const char* kService = "org.kde.kdeconnect";
constexpr const char* kDaemon = "/modules/kdeconnect";

std::string path(const std::string& id, const std::string& plugin = {}) {
    return std::string(kDaemon) + "/devices/" + id + (plugin.empty() ? "" : "/" + plugin);
}

void act(const std::string& id, const std::string& plugin, const std::string& method, const dbus::Writer& args = {}) {
    dbus::Bus::session().call(kService, path(id, plugin), "org.kde.kdeconnect.device." + plugin, method, args,
                              [method](const dbus::Value&, const std::string& err) {
                                  if (!err.empty()) log::warn("KDE Connect {}: {}", method, err);
                              });
}

} // namespace

bool KdeConnect::Device::has(const std::string& plugin) const {
    return std::ranges::find(plugins, "kdeconnect_" + plugin) != plugins.end();
}

std::string KdeConnect::Device::glyph() const {
    if (type == "tablet") return "tablet_android";
    if (type == "laptop") return "laptop";
    if (type == "desktop") return "desktop_windows";
    if (type == "tv") return "tv";
    return "smartphone";
}

KdeConnect& KdeConnect::get() {
    static KdeConnect instance;
    return instance;
}

bool KdeConnect::installed() {
    return haveCommand("kdeconnect-cli");
}

bool KdeConnect::enabled() const {
    return Config::get().value<bool>("/kdeconnect/enabled", false);
}

void KdeConnect::start() {
    // Switched on (or off) in Settings: from then on
    Config::get().changed.connectForever([this] {
        if (enabled() && !m_listening) listen();
        if (!enabled() && (m_running || !m_devices.empty())) {
            m_running = false;
            m_devices.clear();
            changed.emit();
        }
    });
    if (enabled()) listen();
}

void KdeConnect::listen() {
    if (!installed()) return;
    m_listening = true;
    auto& bus = dbus::Bus::session();
    // Anything it says (a device seen, paired, its battery) means a look
    m_signals = bus.subscribe(kService, "", "", "", [this](const dbus::Value&, sd_bus_message*) {
        EventLoop::get().debounce(m_soon, 300, [this] { refresh(); });
    });
    m_owner = bus.subscribe("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                            [this](const dbus::Value& a, sd_bus_message*) {
                                if (a.is_array() && !a.empty() && a[0] == kService) refresh();
                            });
    refresh();
}

void KdeConnect::refresh() {
    if (!enabled() || !installed()) return;
    if (m_refreshing) {
        m_again = true;
        return;
    }
    m_refreshing = true;
    auto& bus = dbus::Bus::session();
    // Asking starts the daemon (D-Bus activation)
    bus.call(kService, kDaemon, "org.kde.kdeconnect.daemon", "announcedName", {}, [this](const dbus::Value& a, const std::string& err) {
        if (err.empty() && a.is_array() && !a.empty() && a[0].is_string()) m_name = a[0].get<std::string>();
    });
    bus.call(kService, kDaemon, "org.kde.kdeconnect.daemon", "devices", dbus::args(false, false),
             [this](const dbus::Value& a, const std::string& err) {
        auto finish = [this] {
            m_refreshing = false;
            changed.emit();
            if (m_again) {
                m_again = false;
                refresh();
            }
        };
        m_running = err.empty();
        if (!err.empty() || !a.is_array() || a.empty() || !a[0].is_array()) {
            m_devices.clear();
            return finish();
        }
        std::vector<std::string> ids;
        for (const auto& id : a[0])
            if (id.is_string()) ids.push_back(id.get<std::string>());
        if (ids.empty()) {
            m_devices.clear();
            return finish();
        }
        // Each device's details; once all are in, the list
        auto got = std::make_shared<std::vector<Device>>();
        auto left = std::make_shared<size_t>(ids.size());
        for (const auto& id : ids)
            device(id, [this, got, left, finish](Device d) {
                got->push_back(std::move(d));
                if (--*left > 0) return;
                std::ranges::sort(*got, [](const Device& x, const Device& y) {
                    if (x.paired != y.paired) return x.paired;
                    if (x.reachable != y.reachable) return x.reachable;
                    return x.name < y.name;
                });
                m_devices = std::move(*got);
                finish();
            });
    });
}

void KdeConnect::device(const std::string& id, std::function<void(Device)> done) {
    auto& bus = dbus::Bus::session();
    bus.getAll(kService, path(id), "org.kde.kdeconnect.device", [id, done](const dbus::Value& p, bool) {
        Device d;
        d.id = id;
        d.name = p.value("name", id);
        d.type = p.value("type", "smartphone");
        d.reachable = p.value("isReachable", false);
        d.paired = p.value("isPaired", false);
        d.pairRequested = p.value("isPairRequestedByPeer", false);
        if (auto it = p.find("supportedPlugins"); it != p.end() && it->is_array())
            for (const auto& pl : *it)
                if (pl.is_string()) d.plugins.push_back(pl.get<std::string>());
        if (!d.paired || !d.reachable || !d.has("battery")) return done(std::move(d));
        dbus::Bus::session().getAll(kService, path(id, "battery"), "org.kde.kdeconnect.device.battery",
                                    [d, done](const dbus::Value& b, bool ok) mutable {
                                        if (ok) {
                                            d.battery = b.value("charge", -1);
                                            d.charging = b.value("isCharging", false);
                                        }
                                        done(std::move(d));
                                    });
    });
}

const KdeConnect::Device* KdeConnect::phone() const {
    for (const auto& d : m_devices)
        if (d.paired && d.reachable) return &d;
    return nullptr;
}

void KdeConnect::pair(const std::string& id) {
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device", "requestPairing");
}
void KdeConnect::unpair(const std::string& id) {
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device", "unpair");
}
void KdeConnect::accept(const std::string& id) {
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device", "acceptPairing");
}
void KdeConnect::reject(const std::string& id) {
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device", "rejectPairing");
}
void KdeConnect::ring(const std::string& id) {
    act(id, "findmyphone", "ring");
}
void KdeConnect::ping(const std::string& id) {
    act(id, "ping", "sendPing");
}
void KdeConnect::sendClipboard(const std::string& id) {
    act(id, "clipboard", "sendClipboard");
}
void KdeConnect::share(const std::string& id, const std::vector<std::string>& files) {
    for (const auto& f : files) act(id, "share", "shareUrl", dbus::args(std::string("file://") + f));
}

} // namespace m3
