#pragma once

#include "core/EventLoop.h"

#include <cstdint>
#include <string>
#include <vector>

namespace m3::quickshare {

// A tiny mDNS responder for one DNS-SD service: it answers the phones'
// queries for `type` (PTR, then SRV, TXT and this computer's addresses) on
// every IPv4 network, and announces the service when it starts. It shares
// UDP 5353 with systemd-resolved or Avahi, so nothing has to be enabled.
class MdnsResponder : public EventSource {
public:
    // instance: the service's own label; type: "_x._tcp"; txt: "k=v" items
    MdnsResponder(std::string instance, std::string type, uint16_t port, std::vector<std::string> txt);
    ~MdnsResponder() override; // says goodbye (TTL 0)
    bool ok() const { return m_fd >= 0; }

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    struct Iface {
        int index;
        uint32_t addr; // network order
    };
    std::vector<Iface> interfaces() const;
    void joinAll();
    std::string response(uint32_t ttl, uint32_t addr) const;
    void announce(uint32_t ttl);
    void sendOn(const Iface& i, const std::string& packet, uint32_t to, uint16_t port);

    int m_fd = -1;
    std::string m_instance, m_type, m_host;
    uint16_t m_port;
    std::vector<std::string> m_txt;
    std::vector<int> m_joined;
    EventLoop::TimerId m_reannounce = 0, m_rejoin = 0;
};

} // namespace m3::quickshare
