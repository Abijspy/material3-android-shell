#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace m3::quickshare {

class BleBeacon;
class Connection;
class MdnsBrowser;
class MdnsResponder;

// Quick Share, the shell's own: Android phones (and Chromebooks) nearby can
// send this computer files, links and text. It speaks Google's Nearby
// Share protocol over the local network: an mDNS service (its own small
// responder) for the phones' share sheet to find, a TCP server, the UKEY2 key
// exchange and AES-256-CBC + HMAC-SHA256 secure messages, as described in
// NearDrop's PROTOCOL.md. Every transfer is asked first, with the PIN the
// phone shows; files land in ~/Downloads.
class QuickShare {
public:
    struct File {
        std::string name, mime;
        int64_t size = 0, payloadId = 0;
    };
    struct Text {
        std::string title; // a preview
        int type = 0;      // 1 text, 2 URL, 3 address, 4 phone number
        int64_t payloadId = 0, size = 0;
    };
    // Asking: waiting for an answer (ours, or theirs when sending);
    // Receiving: bytes moving, either way
    enum class State { Connecting, Asking, Receiving, Done, Declined, Failed };
    struct Transfer {
        int id = 0;
        bool outgoing = false;
        std::string sender, pin; // sender: the other device, either way
        State state = State::Connecting;
        std::vector<File> files;
        std::vector<Text> texts;
        int64_t received = 0, total = 0; // received: sent, when sending
        std::vector<std::string> saved; // the files, where they went
        std::vector<std::string> textsReceived;
        std::string error;
    };

    static QuickShare& get();

    // Visible to devices nearby (Settings → Connected devices)
    bool receiving() const { return m_listenFd >= 0 && m_mdns != nullptr; }
    void apply();                            // follows the settings
    std::string deviceName() const;
    const Transfer* transfer(int id) const;
    const std::map<int, Transfer>& transfers() const { return m_transfers; }
    void accept(int id);
    void decline(int id);
    void cancel(int id); // either way, while it runs

    // Sending. Devices nearby show up while a lease is held: a Bluetooth
    // nudge (phones then appear on the network) and an mDNS search.
    struct Device {
        std::string id, name; // id: its service label
        int type = 0;         // 1 phone, 2 tablet, 3 laptop
        uint32_t addr = 0;    // IPv4, network order
        uint16_t port = 0;
    };
    using Lease = std::shared_ptr<void>;
    [[nodiscard]] Lease discover();
    std::vector<Device> devices() const;
    std::string discoveryNote() const; // why phones may not appear
    int send(const Device& to, const std::vector<std::string>& paths); // the transfer's id, 0 if nothing to send
    std::string status() const { return m_status; } // why it isn't visible, if not

    Signal<int> asking; // a transfer waits for an answer: show it
    Signal<> changed;

    // Used by the connections
    Transfer& newTransfer();
    void finished(Connection* c); // closed: freed on the next turn
    void notifyChanged() { changed.emit(); }

private:
    QuickShare();
    void start();
    void stop();
    void advertise();
    void acceptClient();
    std::string instanceName() const;
    std::string endpointInfo() const;

    class Listener;
    std::unique_ptr<Listener> m_listener;
    int m_listenFd = -1, m_port = 0;
    std::unique_ptr<MdnsResponder> m_mdns;
    std::string m_endpointId, m_status;
    std::map<int, Transfer> m_transfers;
    std::vector<std::unique_ptr<Connection>> m_connections;
    int m_nextId = 1;
    int m_discoverers = 0;
    std::unique_ptr<MdnsBrowser> m_browser;
    std::unique_ptr<BleBeacon> m_beacon;
    std::string m_applied; // the settings last applied
};

} // namespace m3::quickshare
