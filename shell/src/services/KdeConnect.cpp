#include "services/KdeConnect.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Process.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <systemd/sd-bus.h>

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
// ── Text messages ──

namespace {
// A ConversationMessage as kdeconnect marshals it: (event i, body s,
// addresses a(s), date x, type i, read i, threadID x, uID i, subID x,
// attachments a(xsss)); a variant's contents, read leniently
bool parseMessage(const dbus::Value& v, KdeConnect::Message& m) {
    const dbus::Value* s = &v;
    while (s->is_array() && s->size() == 1 && (*s)[0].is_array()) s = &(*s)[0]; // a variant's wrapping
    if (!s->is_array() || s->size() < 7) return false;
    const auto& f = *s;
    if (!f[1].is_string() || !f[3].is_number() || !f[4].is_number() || !f[6].is_number()) return false;
    m.body = f[1].get<std::string>();
    m.addresses.clear();
    if (f[2].is_array())
        for (const auto& a : f[2]) {
            if (a.is_array() && !a.empty() && a[0].is_string()) m.addresses.push_back(a[0].get<std::string>());
            else if (a.is_string()) m.addresses.push_back(a.get<std::string>());
        }
    m.date = f[3].get<int64_t>();
    m.sent = f[4].get<int>() == 2; // Android's: 1 received, 2 sent
    m.read = !f[5].is_number() || f[5].get<int>() != 0;
    m.thread = f[6].get<int64_t>();
    return true;
}
std::string digits(const std::string& number) {
    std::string d;
    for (char c : number)
        if (std::isdigit(static_cast<unsigned char>(c))) d += c;
    return d.size() > 10 ? d.substr(d.size() - 10) : d; // country codes aside
}
} // namespace

void KdeConnect::loadConversations(const std::string& id) {
    if (m_smsDevice != id) {
        m_smsDevice = id;
        m_conversations.clear();
        m_messages.clear();
        // New and updated messages, as the phone sends them
        m_sms = dbus::Bus::session().subscribe(kService, path(id), "org.kde.kdeconnect.device.conversations", "",
                                               [this, id](const dbus::Value& a, sd_bus_message*) {
            if (a.is_array() && !a.empty() && !a[0].is_number()) addMessage(a[0]);
            EventLoop::get().debounce(m_soon, 300, [this, id] { readConversations(id); });
        });
    }
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device.conversations", "requestAllConversationThreads");
    readConversations(id);
}

void KdeConnect::readConversations(const std::string& id) {
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device.conversations", "activeConversations", {},
                              [this](const dbus::Value& a, const std::string& err) {
        if (!err.empty() || !a.is_array() || a.empty() || !a[0].is_array()) return;
        std::vector<Message> out;
        for (const auto& v : a[0]) {
            Message m;
            if (parseMessage(v, m)) out.push_back(std::move(m));
        }
        std::ranges::sort(out, std::greater{}, &Message::date);
        m_conversations = std::move(out);
        messagesChanged.emit();
    });
}

void KdeConnect::addMessage(const dbus::Value& v) {
    Message m;
    if (!parseMessage(v, m)) return;
    // Once each (an update replaces the same date and thread)
    std::erase_if(m_messages, [&](const Message& x) { return x.thread == m.thread && x.date == m.date && x.sent == m.sent; });
    m_messages.push_back(std::move(m));
    messagesChanged.emit();
}

void KdeConnect::loadThread(const std::string& id, int64_t thread) {
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device.conversations", "requestConversation",
                              [thread](sd_bus_message* m) {
                                  sd_bus_message_append(m, "xii", thread, static_cast<int32_t>(0), static_cast<int32_t>(60));
                              });
}

std::vector<KdeConnect::Message> KdeConnect::thread(int64_t thread) const {
    std::vector<Message> out;
    for (const auto& m : m_messages)
        if (m.thread == thread) out.push_back(m);
    // The conversation's latest, if its messages haven't come yet
    if (out.empty())
        for (const auto& m : m_conversations)
            if (m.thread == thread) out.push_back(m);
    std::ranges::sort(out, {}, &Message::date);
    return out;
}

void KdeConnect::reply(const std::string& id, int64_t thread, const std::string& text) {
    if (text.empty()) return;
    dbus::Bus::session().call(kService, path(id), "org.kde.kdeconnect.device.conversations", "replyToConversation",
                              [thread, text](sd_bus_message* m) {
                                  sd_bus_message_append(m, "xs", thread, text.c_str());
                                  sd_bus_message_open_container(m, 'a', "v"); // no attachments
                                  sd_bus_message_close_container(m);
                              },
                              [](const dbus::Value&, const std::string& err) {
                                  if (!err.empty()) log::warn("KDE Connect reply: {}", err);
                              });
}

void KdeConnect::sendNew(const std::string& id, const std::string& number, const std::string& text) {
    if (number.empty() || text.empty()) return;
    run({"kdeconnect-cli", "-d", id, "--send-sms", text, "--destination", number}, nullptr);
}

std::string KdeConnect::contactName(const std::string& id, const std::string& number) const {
    // The contacts the phone shares, as vCards: read once per device
    static std::string loadedFor;
    static std::map<std::string, std::string> names;
    if (loadedFor != id) {
        loadedFor = id;
        names.clear();
        const char* xdg = std::getenv("XDG_DATA_HOME");
        const std::string dir = (xdg && *xdg ? std::string(xdg) : std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.local/share") +
                                "/kpeoplevcard/kdeconnect-" + id;
        std::error_code ec;
        for (const auto& f : std::filesystem::directory_iterator(dir, ec)) {
            std::ifstream in(f.path());
            std::string name;
            std::vector<std::string> tels;
            for (std::string line; std::getline(in, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.starts_with("FN:")) name = line.substr(3);
                else if (line.starts_with("TEL")) {
                    if (const auto c = line.find(':'); c != std::string::npos) tels.push_back(line.substr(c + 1));
                }
            }
            if (!name.empty())
                for (const auto& t : tels) names[digits(t)] = name;
        }
    }
    auto it = names.find(digits(number));
    return it != names.end() ? it->second : number;
}

void KdeConnect::share(const std::string& id, const std::vector<std::string>& files) {
    for (const auto& f : files) act(id, "share", "shareUrl", dbus::args(std::string("file://") + f));
}

} // namespace m3
