#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "dbus/Bus.h"

#include <string>
#include <vector>

namespace m3 {

// Your phone, through KDE Connect's daemon (kdeconnectd, on the session bus):
// its devices, their battery, pairing, and ringing, pinging and sending
// them files or the clipboard. Its notifications arrive as ordinary
// desktop notifications, from kdeconnectd itself. Used once switched on in
// Settings → Connected devices (/kdeconnect/enabled): asking it anything
// starts the daemon, which then announces this computer on the network.
class KdeConnect {
public:
    struct Device {
        std::string id, name, type; // type: smartphone, tablet, desktop, laptop, tv
        bool reachable = false, paired = false, pairRequested = false;
        int battery = -1; // 0–100 where it reports one
        bool charging = false;
        std::vector<std::string> plugins;
        bool has(const std::string& plugin) const;
        std::string glyph() const; // a Material Symbol for its type
    };
    static KdeConnect& get();
    static bool installed();
    bool enabled() const;
    void start(); // as Settings left it: listens and lists, if on
    bool running() const { return m_running; }
    const std::vector<Device>& devices() const { return m_devices; }
    // The paired, reachable one to show (in the bar): null when none
    const Device* phone() const;
    const std::string& name() const { return m_name; } // what this computer announces itself as

    void pair(const std::string& id);
    void unpair(const std::string& id);
    void accept(const std::string& id);
    void reject(const std::string& id);
    void ring(const std::string& id);
    void ping(const std::string& id);
    void sendClipboard(const std::string& id);
    void share(const std::string& id, const std::vector<std::string>& files);
    void refresh();

    Signal<> changed;

private:
    KdeConnect() = default;
    void listen();
    void device(const std::string& id, std::function<void(Device)> done);
    std::vector<Device> m_devices;
    std::string m_name;
    bool m_running = false, m_listening = false, m_refreshing = false, m_again = false;
    dbus::Subscription m_signals, m_owner;
    EventLoop::TimerId m_soon = 0;
};

} // namespace m3
