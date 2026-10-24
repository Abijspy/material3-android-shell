#include "services/Vpn.h"

#include "core/Config.h"
#include "core/Process.h"
#include "services/Network.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

// nmcli -t escapes ':' inside fields as "\:"
std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out(1);
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            out.back() += line[++i];
        } else if (line[i] == ':') {
            out.emplace_back();
        } else {
            out.back() += line[i];
        }
    }
    return out;
}

// The last line of an error, without nmcli's "Error: " prefix
std::string tidy(const std::string& out) {
    std::string s = out;
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    if (const auto nl = s.rfind('\n'); nl != std::string::npos) s = s.substr(nl + 1);
    if (s.starts_with("Error: ")) s = s.substr(7);
    return s.empty() ? "It didn't work" : s;
}

bool hasWarp() {
    std::error_code ec;
    return fs::exists("/usr/bin/warp-cli", ec);
}

} // namespace

Vpn& Vpn::get() {
    static Vpn instance;
    return instance;
}

Vpn::Vpn() {
    refresh();
    // NetworkManager's primary connection changes when a VPN comes or goes
    Network::get().changed.connectForever([this] { refresh(); });
}

bool Vpn::anyActive() const {
    return m_warp == Warp::Connected || std::ranges::any_of(m_connections, &Connection::active);
}

std::string Vpn::activeName() const {
    for (const auto& c : m_connections)
        if (c.active) return c.name;
    return m_warp == Warp::Connected ? "Cloudflare WARP" : "";
}

void Vpn::refresh() {
    run({"nmcli", "-t", "-f", "NAME,UUID,TYPE,ACTIVE,STATE", "connection", "show"}, [this](int code, const std::string& out) {
        std::vector<Connection> list;
        if (code == 0) {
            std::stringstream ss(out);
            for (std::string line; std::getline(ss, line);) {
                const auto f = fields(line);
                if (f.size() < 5 || (f[2] != "vpn" && f[2] != "wireguard")) continue;
                // A VPN's plugin ("openvpn"…) is looked up below
                list.push_back({f[0], f[1], f[2] == "vpn" ? "VPN" : f[2], f[3] == "yes" && f[4] == "activated", f[4] == "activating"});
            }
        }
        std::ranges::sort(list, {}, &Connection::name);
        const bool same = list.size() == m_connections.size() &&
                          std::ranges::equal(list, m_connections, [](const Connection& a, const Connection& b) {
                              return a.uuid == b.uuid && a.name == b.name && a.active == b.active && a.activating == b.activating;
                          });
        m_connections = std::move(list);
        if (!same) changed.emit();
        // Each VPN's plugin, once: "openvpn", "wireguard"…
        for (const auto& c : m_connections) {
            if (c.type != "VPN") continue;
            const std::string uuid = c.uuid;
            run({"nmcli", "-g", "vpn.service-type", "connection", "show", uuid}, [this, uuid](int code, const std::string& out) {
                if (code != 0) return;
                std::string t = out.substr(0, out.find('\n'));
                if (const auto dot = t.rfind('.'); dot != std::string::npos) t = t.substr(dot + 1);
                for (auto& x : m_connections)
                    if (x.uuid == uuid && x.type != t) {
                        x.type = t;
                        changed.emit();
                    }
            });
        }
    });
    refreshWarp();
}

void Vpn::refreshWarp() {
    if (!hasWarp()) {
        m_warp = Warp::Absent;
        return;
    }
    run({"warp-cli", "--accept-tos", "status"}, [this](int, const std::string& out) {
        Warp w = Warp::Disconnected;
        if (out.find("Unable to connect to the CloudflareWARP daemon") != std::string::npos) w = Warp::NoDaemon;
        else if (out.find("Registration Missing") != std::string::npos || out.find("registration missing") != std::string::npos)
            w = Warp::Unregistered;
        else if (out.find("Status update: Connected") != std::string::npos) w = Warp::Connected;
        else if (out.find("Status update: Connecting") != std::string::npos) w = Warp::Connecting;
        if (w != m_warp) {
            m_warp = w;
            changed.emit();
        }
    });
}

void Vpn::finish(const std::string& error) {
    m_busy.clear();
    m_error = error;
    refresh();
    changed.emit();
}

void Vpn::up(const std::string& uuid) {
    m_busy = uuid;
    m_error.clear();
    Config::get().set("/vpn/last", uuid);
    changed.emit();
    // --ask off: a VPN wanting a password it doesn't have says so
    run({"nmcli", "connection", "up", "uuid", uuid}, [this](int code, const std::string& out) { finish(code == 0 ? "" : tidy(out)); });
}

void Vpn::down(const std::string& uuid) {
    m_busy = uuid;
    m_error.clear();
    changed.emit();
    run({"nmcli", "connection", "down", "uuid", uuid}, [this](int code, const std::string& out) { finish(code == 0 ? "" : tidy(out)); });
}

void Vpn::remove(const std::string& uuid) {
    m_error.clear();
    run({"nmcli", "connection", "delete", "uuid", uuid}, [this](int code, const std::string& out) { finish(code == 0 ? "" : tidy(out)); });
}

void Vpn::import(const std::string& path) {
    const std::string ext = fs::path(path).extension().string();
    const std::string type = ext == ".conf" ? "wireguard" : ext == ".ovpn" ? "openvpn" : "";
    if (type.empty()) return finish("Choose a WireGuard .conf or an OpenVPN .ovpn file");
    m_error.clear();
    run({"nmcli", "connection", "import", "type", type, "file", path}, [this, type](int code, const std::string& out) {
        std::string error = code == 0 ? "" : tidy(out);
        // NetworkManager connects an imported WireGuard tunnel at once, and at
        // every boot: off until the user turns it on. "… (uuid) successfully added."
        const auto open = out.rfind('('), close = out.rfind(')');
        if (code == 0 && open != std::string::npos && close > open) {
            const std::string uuid = out.substr(open + 1, close - open - 1);
            run({"sh", "-c", "nmcli connection modify uuid \"$1\" connection.autoconnect no; nmcli connection down uuid \"$1\"", "sh", uuid},
                [this](int, const std::string&) { refresh(); });
        }
        // nmcli's way of saying the plugin isn't installed
        if (code != 0 && type == "openvpn" && (out.find("plugin") != std::string::npos || out.find("unknown") != std::string::npos))
            error = "OpenVPN needs NetworkManager's plugin: install networkmanager-openvpn";
        finish(error);
    });
}

void Vpn::toggleLast() {
    for (const auto& c : m_connections)
        if (c.active) return down(c.uuid);
    if (m_warp == Warp::Connected) return warpConnect(false);
    const std::string last = Config::get().value<std::string>("/vpn/last", "");
    for (const auto& c : m_connections)
        if (c.uuid == last) return up(c.uuid);
    if (!m_connections.empty()) return up(m_connections.front().uuid);
    if (m_warp == Warp::Disconnected) warpConnect(true);
}

void Vpn::warpConnect(bool on) {
    m_busy = "warp";
    m_error.clear();
    changed.emit();
    run({"warp-cli", "--accept-tos", on ? "connect" : "disconnect"}, [this](int code, const std::string& out) {
        // The status settles a moment later
        EventLoop::get().addTimer(1500, [this] { refreshWarp(); });
        finish(code == 0 ? "" : tidy(out));
    });
}

void Vpn::warpRegister() {
    m_busy = "warp";
    changed.emit();
    run({"warp-cli", "--accept-tos", "registration", "new"}, [this](int code, const std::string& out) { finish(code == 0 ? "" : tidy(out)); });
}

void Vpn::warpStartDaemon() {
    m_busy = "warp";
    changed.emit();
    run({"pkexec", "systemctl", "enable", "--now", "warp-svc"}, [this](int code, const std::string& out) {
        // The daemon takes a moment to answer
        EventLoop::get().addTimer(2000, [this] { refreshWarp(); });
        finish(code == 0 ? "" : code == 126 ? "" : tidy(out)); // 126: the prompt was dismissed
    });
}

Vpn::Lease Vpn::watch() {
    if (m_watchers++ == 0) {
        refresh();
        m_timer = EventLoop::get().addTimer(4000, [this] { refresh(); }, true);
    }
    return std::shared_ptr<void>(nullptr, [](void*) {
        auto& v = Vpn::get();
        if (--v.m_watchers == 0 && v.m_timer) {
            EventLoop::get().cancel(v.m_timer);
            v.m_timer = 0;
        }
    });
}

} // namespace m3
