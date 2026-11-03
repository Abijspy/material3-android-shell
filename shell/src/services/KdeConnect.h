#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "dbus/Bus.h"

#include <cstdint>
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

    // Text messages (the phone's SMS plugin): each conversation's latest,
    // a conversation's messages as the phone sends them, replying, and a
    // new one to a number. Names from the contacts the phone shares
    struct Message {
        int64_t thread = 0;
        std::string body;
        std::vector<std::string> addresses;
        int64_t date = 0; // ms since the epoch
        bool sent = false, read = true;
    };
    void loadConversations(const std::string& id);              // asks the phone for them
    const std::vector<Message>& conversations() const { return m_conversations; } // newest first
    void loadThread(const std::string& id, int64_t thread);
    std::vector<Message> thread(int64_t thread) const;          // oldest first
    void reply(const std::string& id, int64_t thread, const std::string& text);
    void sendNew(const std::string& id, const std::string& number, const std::string& text);
    std::string contactName(const std::string& id, const std::string& number) const; // the number when unknown
    Signal<> messagesChanged;

    Signal<> changed;

private:
    KdeConnect() = default;
    void listen();
    void device(const std::string& id, std::function<void(Device)> done);
    void readConversations(const std::string& id);
    void addMessage(const dbus::Value& v);
    std::vector<Device> m_devices;
    std::vector<Message> m_conversations, m_messages;
    dbus::Subscription m_sms;
    std::string m_smsDevice;
    std::string m_name;
    bool m_running = false, m_listening = false, m_refreshing = false, m_again = false;
    dbus::Subscription m_signals, m_owner;
    EventLoop::TimerId m_soon = 0;
};

} // namespace m3
