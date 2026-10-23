#include "quickshare/Mdns.h"

#include "core/Log.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace m3::quickshare {

namespace {

constexpr uint16_t kPort = 5353;
constexpr uint16_t kPtr = 12, kTxt = 16, kSrv = 33, kA = 1, kAny = 255;
const uint32_t kGroup = htonl(0xE00000FB); // 224.0.0.251

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void u16(std::string& out, uint16_t v) {
    out += static_cast<char>(v >> 8);
    out += static_cast<char>(v);
}

void u32(std::string& out, uint32_t v) {
    u16(out, static_cast<uint16_t>(v >> 16));
    u16(out, static_cast<uint16_t>(v));
}

// "a.b.local" → labels (no compression: the packets are small)
void name(std::string& out, const std::string& dotted) {
    size_t start = 0;
    while (start < dotted.size()) {
        size_t dot = dotted.find('.', start);
        if (dot == std::string::npos) dot = dotted.size();
        const std::string label = dotted.substr(start, dot - start);
        out += static_cast<char>(label.size());
        out += label;
        start = dot + 1;
    }
    out += '\0';
}

void record(std::string& out, const std::string& owner, uint16_t type, bool flush, uint32_t ttl, const std::string& data) {
    name(out, owner);
    u16(out, type);
    u16(out, static_cast<uint16_t>(flush ? 0x8001 : 0x0001)); // IN, cache-flush for unique records
    u32(out, ttl);
    u16(out, static_cast<uint16_t>(data.size()));
    out += data;
}

// A name in a packet, following compression pointers
bool readName(const std::string& p, size_t& i, std::string& out) {
    out.clear();
    size_t at = i;
    bool jumped = false;
    for (int hops = 0; hops < 32; ++hops) {
        if (at >= p.size()) return false;
        const auto len = static_cast<uint8_t>(p[at]);
        if (len == 0) {
            if (!jumped) i = at + 1;
            return true;
        }
        if ((len & 0xC0) == 0xC0) {
            if (at + 1 >= p.size()) return false;
            if (!jumped) i = at + 2;
            at = static_cast<size_t>((len & 0x3F) << 8 | static_cast<uint8_t>(p[at + 1]));
            jumped = true;
            continue;
        }
        if (at + 1 + len > p.size()) return false;
        if (!out.empty()) out += '.';
        out += p.substr(at + 1, len);
        at += 1 + len;
    }
    return false;
}

} // namespace

MdnsResponder::MdnsResponder(std::string instance, std::string type, uint16_t port, std::vector<std::string> txt)
    : m_instance(std::move(instance)), m_type(std::move(type)), m_port(port), m_txt(std::move(txt)) {
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    m_host = std::string(host) + ".local";
    m_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_fd < 0) return;
    int yes = 1;
    // Shared with systemd-resolved or Avahi on the same port
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        log::warn("quick share: mDNS: can't use port 5353: {}", std::strerror(errno));
        close(m_fd);
        m_fd = -1;
        return;
    }
    int ttl = 255;
    setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    unsigned char loop = 1;
    setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    joinAll();
    EventLoop::get().add(this);
    // Announced now and a second later, as mDNS asks; networks joined as
    // they come (Wi-Fi reconnecting)
    announce(4500);
    m_reannounce = EventLoop::get().addTimer(1000, [this] {
        m_reannounce = 0;
        announce(4500);
    });
    m_rejoin = EventLoop::get().addTimer(30000, [this] { joinAll(); }, true);
}

MdnsResponder::~MdnsResponder() {
    if (m_reannounce) EventLoop::get().cancel(m_reannounce);
    if (m_rejoin) EventLoop::get().cancel(m_rejoin);
    if (m_fd < 0) return;
    announce(0); // goodbye: caches drop it
    EventLoop::get().remove(this);
    close(m_fd);
}

std::vector<MdnsResponder::Iface> MdnsResponder::interfaces() const {
    std::vector<Iface> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (auto* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if ((a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_MULTICAST)) continue;
        const int index = static_cast<int>(if_nametoindex(a->ifa_name));
        if (index == 0) continue;
        out.push_back({index, reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr});
    }
    freeifaddrs(list);
    return out;
}

void MdnsResponder::joinAll() {
    for (const auto& i : interfaces()) {
        if (std::ranges::find(m_joined, i.index) != m_joined.end()) continue;
        ip_mreqn req{};
        req.imr_multiaddr.s_addr = kGroup;
        req.imr_ifindex = i.index;
        if (setsockopt(m_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof req) == 0 || errno == EADDRINUSE) {
            m_joined.push_back(i.index);
            // A new network: tell it at once
            std::string packet = response(4500, i.addr);
            sendOn(i, packet, kGroup, kPort);
        }
    }
}

std::string MdnsResponder::response(uint32_t ttl, uint32_t addr) const {
    const std::string service = m_type + ".local", full = m_instance + "." + service;
    std::string p;
    u16(p, 0);      // id
    u16(p, 0x8400); // response, authoritative
    u16(p, 0);      // questions
    u16(p, 1);      // answers: the PTR
    u16(p, 0);      // authority
    u16(p, addr ? 3 : 2); // additional: SRV, TXT, A
    std::string ptr;
    name(ptr, full);
    record(p, service, kPtr, false, ttl, ptr);
    std::string srv;
    u16(srv, 0);
    u16(srv, 0);
    u16(srv, m_port);
    name(srv, m_host);
    record(p, full, kSrv, true, ttl, srv);
    std::string txt;
    for (const auto& t : m_txt) {
        txt += static_cast<char>(std::min<size_t>(t.size(), 255));
        txt += t.substr(0, 255);
    }
    record(p, full, kTxt, true, ttl, txt);
    if (addr) record(p, m_host, kA, true, ttl, std::string(reinterpret_cast<const char*>(&addr), 4));
    return p;
}

void MdnsResponder::sendOn(const Iface& i, const std::string& packet, uint32_t to, uint16_t port) {
    ip_mreqn via{};
    via.imr_ifindex = i.index;
    via.imr_address.s_addr = i.addr;
    setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_IF, &via, sizeof via);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = to;
    sendto(m_fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof dst);
}

void MdnsResponder::announce(uint32_t ttl) {
    for (const auto& i : interfaces()) sendOn(i, response(ttl, i.addr), kGroup, kPort);
}

void MdnsResponder::dispatch(short revents) {
    if (!(revents & POLLIN)) return;
    for (;;) {
        char buf[9000];
        sockaddr_in from{};
        socklen_t fromLen = sizeof from;
        // Which interface it came in on, for the address to answer with
        const ssize_t n = recvfrom(m_fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) return;
        const std::string p(buf, static_cast<size_t>(n));
        if (p.size() < 12) continue;
        const uint16_t flags = static_cast<uint16_t>(static_cast<uint8_t>(p[2]) << 8 | static_cast<uint8_t>(p[3]));
        if (flags & 0x8000) continue; // a response, not a query
        const int questions = static_cast<uint8_t>(p[4]) << 8 | static_cast<uint8_t>(p[5]);
        size_t i = 12;
        bool asked = false, unicast = false;
        const std::string service = lower(m_type + ".local"), full = lower(m_instance + "." + m_type + ".local");
        for (int q = 0; q < questions; ++q) {
            std::string qname;
            if (!readName(p, i, qname) || i + 4 > p.size()) break;
            const uint16_t qtype = static_cast<uint16_t>(static_cast<uint8_t>(p[i]) << 8 | static_cast<uint8_t>(p[i + 1]));
            const uint16_t qclass = static_cast<uint16_t>(static_cast<uint8_t>(p[i + 2]) << 8 | static_cast<uint8_t>(p[i + 3]));
            i += 4;
            const std::string l = lower(qname);
            if ((l == service && (qtype == kPtr || qtype == kAny)) || (l == full && (qtype == kSrv || qtype == kTxt || qtype == kAny))) {
                asked = true;
                if (qclass & 0x8000) unicast = true; // the QU bit
            }
        }
        if (!asked) continue;
        // The interface whose network the asker is on (or every one)
        const auto ifaces = interfaces();
        for (const auto& f : ifaces) {
            sendOn(f, response(4500, f.addr), kGroup, kPort);
            // Legacy and QU queries get a direct answer too
            if (unicast || ntohs(from.sin_port) != kPort) sendOn(f, response(10, f.addr), from.sin_addr.s_addr, ntohs(from.sin_port));
        }
    }
}

// ── Browsing ──

namespace {

uint16_t be16(const std::string& p, size_t i) {
    return static_cast<uint16_t>(static_cast<uint8_t>(p[i]) << 8 | static_cast<uint8_t>(p[i + 1]));
}

std::vector<std::pair<int, uint32_t>> ipv4Interfaces() {
    std::vector<std::pair<int, uint32_t>> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (auto* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if ((a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_MULTICAST)) continue;
        const int index = static_cast<int>(if_nametoindex(a->ifa_name));
        if (index) out.emplace_back(index, reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
    }
    freeifaddrs(list);
    return out;
}

void sendQuery(int fd, const std::string& packet) {
    for (const auto& [index, addr] : ipv4Interfaces()) {
        ip_mreqn via{};
        via.imr_ifindex = index;
        via.imr_address.s_addr = addr;
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &via, sizeof via);
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(kPort);
        dst.sin_addr.s_addr = kGroup;
        sendto(fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof dst);
    }
}

} // namespace

// A socket on a port of its own: legacy queries from it get direct answers
class MdnsBrowser::Direct : public EventSource {
public:
    explicit Direct(MdnsBrowser& b) : m_b(b) {
        m_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (m_fd >= 0) EventLoop::get().add(this);
    }
    ~Direct() override {
        if (m_fd < 0) return;
        EventLoop::get().remove(this);
        close(m_fd);
    }
    int fd() const override { return m_fd; }
    void dispatch(short revents) override {
        if (revents & POLLIN) m_b.read(m_fd);
    }

private:
    MdnsBrowser& m_b;
    int m_fd = -1;
};

MdnsBrowser::MdnsBrowser(std::string type) : m_type(std::move(type)) {
    m_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_fd >= 0) {
        int yes = 1;
        setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        setsockopt(m_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(kPort);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
            close(m_fd);
            m_fd = -1;
        } else {
            int ttl = 255;
            setsockopt(m_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
            for (const auto& [index, _] : ipv4Interfaces()) {
                ip_mreqn req{};
                req.imr_multiaddr.s_addr = kGroup;
                req.imr_ifindex = index;
                setsockopt(m_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &req, sizeof req);
            }
            EventLoop::get().add(this);
        }
    }
    m_direct = std::make_unique<Direct>(*this);
    query();
    m_timer = EventLoop::get().addTimer(3000, [this] {
        expire();
        query();
    }, true);
}

MdnsBrowser::~MdnsBrowser() {
    EventLoop::get().cancel(m_timer);
    m_direct.reset();
    if (m_fd < 0) return;
    EventLoop::get().remove(this);
    close(m_fd);
}

void MdnsBrowser::query() {
    // The PTR question, and SRV and TXT for any instance still missing them
    const std::string service = m_type + ".local";
    std::vector<std::pair<std::string, uint16_t>> questions{{service, kPtr}};
    for (const auto& [label, inst] : m_found)
        if (!inst.port) {
            questions.emplace_back(label + "." + service, kSrv);
            questions.emplace_back(label + "." + service, kTxt);
        }
    std::string p;
    u16(p, 0);
    u16(p, 0);
    u16(p, static_cast<uint16_t>(questions.size()));
    u16(p, 0);
    u16(p, 0);
    u16(p, 0);
    for (const auto& [qname, qtype] : questions) {
        name(p, qname);
        u16(p, qtype);
        u16(p, 1); // IN
    }
    if (m_fd >= 0) sendQuery(m_fd, p);
    if (m_direct && m_direct->fd() >= 0) sendQuery(m_direct->fd(), p);
}

void MdnsBrowser::dispatch(short revents) {
    if (revents & POLLIN) read(m_fd);
}

void MdnsBrowser::read(int fd) {
    for (;;) {
        char buf[9000];
        sockaddr_in from{};
        socklen_t fromLen = sizeof from;
        const ssize_t n = recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) return;
        parse(std::string(buf, static_cast<size_t>(n)), from.sin_addr.s_addr);
    }
}

void MdnsBrowser::parse(const std::string& p, uint32_t from) {
    if (p.size() < 12 || !(be16(p, 2) & 0x8000)) return; // answers only
    const int questions = be16(p, 4);
    const int records = be16(p, 6) + be16(p, 8) + be16(p, 10);
    size_t i = 12;
    std::string owner;
    for (int q = 0; q < questions; ++q) {
        if (!readName(p, i, owner) || i + 4 > p.size()) return;
        i += 4;
    }
    const std::string service = lower(m_type + ".local");
    const auto labelOf = [&](const std::string& full) -> std::string {
        const std::string l = lower(full);
        if (l.size() <= service.size() + 1 || !l.ends_with("." + service)) return {};
        return full.substr(0, full.size() - service.size() - 1);
    };
    struct Srv {
        std::string label, host;
        uint16_t port;
    };
    std::vector<Srv> srvs;
    std::map<std::string, uint32_t> hosts;
    bool any = false;
    const auto now = std::chrono::steady_clock::now();
    for (int r = 0; r < records; ++r) {
        if (!readName(p, i, owner) || i + 10 > p.size()) return;
        const uint16_t type = be16(p, i);
        const uint32_t ttl = static_cast<uint32_t>(be16(p, i + 4)) << 16 | be16(p, i + 6);
        const size_t len = be16(p, i + 8);
        i += 10;
        if (i + len > p.size()) return;
        const size_t end = i + len;
        if (type == kPtr && lower(owner) == service) {
            std::string target;
            size_t at = i;
            if (readName(p, at, target)) {
                const std::string label = labelOf(target);
                if (!label.empty()) {
                    if (ttl == 0) {
                        any |= m_found.erase(label) > 0;
                    } else {
                        auto& inst = m_found[label];
                        inst.name = label;
                        inst.seen = now;
                        if (!inst.addr) inst.addr = from;
                        any = true;
                    }
                }
            }
        } else if (type == kSrv && len >= 7) {
            const std::string label = labelOf(owner);
            std::string host;
            size_t at = i + 6;
            if (!label.empty() && readName(p, at, host)) srvs.push_back({label, lower(host), be16(p, i + 4)});
        } else if (type == kTxt) {
            const std::string label = labelOf(owner);
            if (auto it = m_found.find(label); !label.empty() && it != m_found.end()) {
                it->second.txt.clear();
                for (size_t at = i; at < end;) {
                    const size_t n = static_cast<uint8_t>(p[at]);
                    if (at + 1 + n > end) break;
                    if (n) it->second.txt.push_back(p.substr(at + 1, n));
                    at += 1 + n;
                }
                any = true;
            }
        } else if (type == kA && len == 4) {
            uint32_t a;
            std::memcpy(&a, p.data() + i, 4);
            hosts[lower(owner)] = a;
        }
        i = end;
    }
    for (const auto& s : srvs) {
        auto it = m_found.find(s.label);
        if (it == m_found.end()) continue;
        it->second.port = s.port;
        if (auto h = hosts.find(s.host); h != hosts.end()) it->second.addr = h->second;
        any = true;
    }
    if (any && changed) changed();
}

void MdnsBrowser::expire() {
    // Not heard from for a while: gone (turned off, or out of range)
    const auto old = std::chrono::steady_clock::now() - std::chrono::seconds(15);
    const size_t before = m_found.size();
    std::erase_if(m_found, [&](const auto& kv) { return kv.second.seen < old; });
    if (m_found.size() != before && changed) changed();
}

} // namespace m3::quickshare
