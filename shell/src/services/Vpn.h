#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <memory>
#include <string>
#include <vector>

namespace m3 {

// VPNs: NetworkManager's VPN and WireGuard connections (through nmcli:
// connect, disconnect, import a .conf or .ovpn file, delete), and Cloudflare
// WARP through warp-cli when it's installed.
class Vpn {
public:
    struct Connection {
        std::string name, uuid;
        std::string type;      // "wireguard", or the VPN plugin's ("openvpn"…)
        bool active = false;   // connected
        bool activating = false;
    };
    enum class Warp { Absent, NoDaemon, Unregistered, Disconnected, Connecting, Connected };

    static Vpn& get();

    const std::vector<Connection>& connections() const { return m_connections; }
    bool anyActive() const; // a VPN (or WARP) carries the traffic
    std::string activeName() const;
    void up(const std::string& uuid);
    void down(const std::string& uuid);
    void remove(const std::string& uuid);
    // A WireGuard .conf or an OpenVPN .ovpn; the error (a missing plugin…)
    // goes to lastError()
    void import(const std::string& path);
    // The one used last, on or off (Quick Settings' tile)
    void toggleLast();

    Warp warp() const { return m_warp; }
    void warpConnect(bool on);
    void warpRegister();
    void warpStartDaemon(); // asks for the admin password

    const std::string& lastError() const { return m_error; }
    const std::string& busy() const { return m_busy; } // a uuid (or "warp") mid-change

    void refresh();
    // Watched every few seconds while a lease is held (the VPN page)
    using Lease = std::shared_ptr<void>;
    [[nodiscard]] Lease watch();

    Signal<> changed;

private:
    Vpn();
    void refreshWarp();
    void finish(const std::string& error);

    std::vector<Connection> m_connections;
    Warp m_warp = Warp::Absent;
    std::string m_error, m_busy;
    int m_watchers = 0;
    EventLoop::TimerId m_timer = 0;
};

} // namespace m3
