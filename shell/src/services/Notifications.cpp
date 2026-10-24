#include "services/Notifications.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Guard.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <systemd/sd-bus.h>

namespace m3 {

namespace {

const char* kName = "org.freedesktop.Notifications";
const char* kPath = "/org/freedesktop/Notifications";

int onMessage(sd_bus_message* m, void* ud, sd_bus_error*) {
    int r = -EIO; // a method that throws answers with an error instead
    guard("a notification", [&] { r = static_cast<Notifications*>(ud)->handle(m); });
    return r;
}

const char* kIntrospect = R"(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
 <interface name="org.freedesktop.Notifications">
  <method name="Notify">
   <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
   <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="as" direction="in"/>
   <arg type="a{sv}" direction="in"/><arg type="i" direction="in"/><arg type="u" direction="out"/>
  </method>
  <method name="CloseNotification"><arg type="u" direction="in"/></method>
  <method name="GetCapabilities"><arg type="as" direction="out"/></method>
  <method name="GetServerInformation">
   <arg type="s" direction="out"/><arg type="s" direction="out"/><arg type="s" direction="out"/><arg type="s" direction="out"/>
  </method>
  <signal name="NotificationClosed"><arg type="u"/><arg type="u"/></signal>
  <signal name="ActionInvoked"><arg type="u"/><arg type="s"/></signal>
 </interface>
</node>
)";

} // namespace

Notifications& Notifications::get() {
    static Notifications instance;
    return instance;
}

Notifications::Notifications() {
    loadHistory();
    auto& bus = dbus::Bus::session();
    if (!bus.connected()) return;
    sd_bus_add_object(bus.raw(), &m_object, kPath, onMessage, this);
    // Don't steal the name from a running daemon (the QML shell, mako…)
    sd_bus_request_name_async(bus.raw(), nullptr, kName, 0,
                              [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
                                  auto* self = static_cast<Notifications*>(ud);
                                  self->m_active = !sd_bus_message_is_method_error(m, nullptr);
                                  if (self->m_active)
                                      log::info("notifications: serving {}", kName);
                                  else
                                      log::warn("notifications: another daemon owns {}; toasts are off", kName);
                                  self->changed.emit();
                                  return 0;
                              },
                              this);
}

const Notifications::Notification* Notifications::find(uint32_t id) const {
    auto it = std::ranges::find(m_list, id, &Notification::id);
    return it != m_list.end() ? &*it : nullptr;
}

std::string Notifications::cleanBody(const std::string& markup) {
    // The spec allows a little markup (b, i, u, a, img); render it as text
    std::string out;
    for (size_t i = 0; i < markup.size(); ++i) {
        if (markup[i] == '<') {
            const auto end = markup.find('>', i);
            if (end == std::string::npos) break;
            if (markup.compare(i, 3, "<br") == 0) out += '\n';
            i = end;
            continue;
        }
        out += markup[i];
    }
    const std::pair<const char*, const char*> entities[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
                                                             {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}};
    for (const auto& [from, to] : entities) {
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, std::char_traits<char>::length(from), to);
            pos += std::char_traits<char>::length(to);
        }
    }
    return out;
}

int Notifications::handle(sd_bus_message* m) {
    if (!sd_bus_message_is_method_call(m, nullptr, nullptr)) return 0;
    const std::string iface = sd_bus_message_get_interface(m) ? sd_bus_message_get_interface(m) : "";
    const std::string member = sd_bus_message_get_member(m) ? sd_bus_message_get_member(m) : "";

    if (iface == "org.freedesktop.DBus.Introspectable") return sd_bus_reply_method_return(m, "s", kIntrospect);
    if (iface != kName) return 0;

    if (member == "GetServerInformation")
        return sd_bus_reply_method_return(m, "ssss", "Material3UI", "material3ui", "0.1.0", "1.2");

    if (member == "GetCapabilities") {
        sd_bus_message* reply = nullptr;
        sd_bus_message_new_method_return(m, &reply);
        sd_bus_message_append(reply, "as", 6, "body", "body-markup", "actions", "icon-static", "persistence",
                              "action-icons");
        const int r = sd_bus_send(nullptr, reply, nullptr);
        sd_bus_message_unref(reply);
        return r;
    }

    if (member == "CloseNotification") {
        uint32_t id = 0;
        sd_bus_message_read(m, "u", &id);
        close(id, 3);
        return sd_bus_reply_method_return(m, "");
    }

    if (member == "Notify") {
        const auto a = dbus::readAll(m);
        if (a.size() < 8) return sd_bus_reply_method_errorf(m, "org.freedesktop.DBus.Error.InvalidArgs", "bad Notify");
        Notification n;
        n.app = a[0].get<std::string>();
        const uint32_t replaces = a[1].get<uint32_t>();
        n.appIcon = a[2].get<std::string>();
        n.summary = a[3].get<std::string>();
        n.body = cleanBody(a[4].get<std::string>());
        for (size_t i = 0; i + 1 < a[5].size(); i += 2) n.actions.push_back({a[5][i], a[5][i + 1]});
        const auto& hints = a[6];
        if (auto u = hints.find("urgency"); u != hints.end() && u->is_number())
            n.urgency = static_cast<Urgency>(std::clamp(u->get<int>(), 0, 2));
        for (const char* key : {"image-data", "image_data", "icon_data"}) {
            auto img = hints.find(key);
            if (img == hints.end() || !img->is_array() || img->size() != 7 || !(*img)[6].is_binary()) continue;
            n.image = Image{(*img)[0].get<int>(), (*img)[1].get<int>(), (*img)[2].get<int>(), (*img)[3].get<bool>(),
                            (*img)[6].get_binary()};
            break;
        }
        for (const char* key : {"image-path", "image_path"})
            if (auto p = hints.find(key); p != hints.end() && p->is_string()) n.imagePath = *p;
        if (auto d = hints.find("desktop-entry"); d != hints.end() && d->is_string()) n.desktopEntry = *d;
        if (auto t = hints.find("transient"); t != hints.end()) n.transient = t->is_boolean() ? t->get<bool>() : t->get<int>() != 0;
        if (auto r = hints.find("resident"); r != hints.end()) n.resident = r->is_boolean() ? r->get<bool>() : r->get<int>() != 0;
        n.timeoutMs = a[7].get<int>();
        n.time = std::time(nullptr);

        uint32_t id = 0;
        auto existing = replaces ? std::ranges::find(m_list, replaces, &Notification::id) : m_list.end();
        if (existing != m_list.end()) {
            id = n.id = replaces;
            *existing = std::move(n);
        } else {
            id = n.id = m_nextId++;
            m_list.insert(m_list.begin(), std::move(n));
            if (m_list.size() > 100) m_list.pop_back();
        }
        posted.emit(id);
        changed.emit();
        return sd_bus_reply_method_return(m, "u", id);
    }
    return 0;
}

void Notifications::close(uint32_t id, uint32_t reason) {
    const auto it = std::ranges::find(m_list, id, &Notification::id);
    if (it == m_list.end()) return;
    remember(*it);
    m_list.erase(it);
    if (m_active) sd_bus_emit_signal(dbus::Bus::session().raw(), kPath, kName, "NotificationClosed", "uu", id, reason);
    closed.emit(id);
    changed.emit();
}

void Notifications::invoke(uint32_t id, const std::string& actionKey) {
    const Notification* n = find(id);
    if (!n) return;
    sd_bus_emit_signal(dbus::Bus::session().raw(), kPath, kName, "ActionInvoked", "us", id, actionKey.c_str());
    if (!n->resident) close(id, 2);
}

void Notifications::dismiss(uint32_t id) {
    close(id, 2);
}

void Notifications::clear() {
    while (!m_list.empty()) close(m_list.front().id, 2);
}

// ── History ──

namespace {
constexpr size_t kHistoryMax = 300;
constexpr std::time_t kHistoryDays = 30;
std::string historyFile() {
    return paths::stateDir() + "/notification-history.json";
}
} // namespace

bool Notifications::keepsHistory() const {
    return Config::get().value<bool>("/notifications/history", true);
}

void Notifications::remember(const Notification& n) {
    if (n.transient || !keepsHistory() || (n.summary.empty() && n.body.empty())) return;
    Notification h = n;
    h.image = {};     // raw pixels stay out of the file
    h.actions.clear(); // the app that offered them may be gone
    m_history.insert(m_history.begin(), std::move(h));
    const std::time_t old = std::time(nullptr) - kHistoryDays * 86400;
    std::erase_if(m_history, [old](const Notification& x) { return x.time < old; });
    if (m_history.size() > kHistoryMax) m_history.resize(kHistoryMax);
    saveHistory();
}

void Notifications::forget(uint32_t id, std::time_t time) {
    // Ids start again each session: the time tells same-numbered ones apart
    std::erase_if(m_history, [&](const Notification& x) { return x.id == id && x.time == time; });
    saveHistory();
    changed.emit();
}

void Notifications::clearHistory() {
    m_history.clear();
    saveHistory();
    changed.emit();
}

void Notifications::loadHistory() {
    std::ifstream in(historyFile());
    if (!in) return;
    try {
        const auto doc = nlohmann::json::parse(in);
        const std::time_t old = std::time(nullptr) - kHistoryDays * 86400;
        for (const auto& j : doc) {
            Notification n;
            n.id = j.value("id", 0u);
            n.app = j.value("app", "");
            n.appIcon = j.value("appIcon", "");
            n.desktopEntry = j.value("desktopEntry", "");
            n.summary = j.value("summary", "");
            n.body = j.value("body", "");
            n.imagePath = j.value("imagePath", "");
            n.urgency = static_cast<Urgency>(std::clamp(j.value("urgency", 1), 0, 2));
            n.time = j.value("time", static_cast<std::time_t>(0));
            if (n.time >= old) m_history.push_back(std::move(n));
        }
    } catch (const std::exception& e) {
        log::warn("notification history: {}", e.what());
    }
}

void Notifications::saveHistory() {
    EventLoop::get().debounce(m_saveTimer, 1000, [this] { writeHistory(); });
}

Notifications::~Notifications() {
    // Quitting within a second of a dismissal: still kept
    if (m_saveTimer) writeHistory();
}

void Notifications::writeHistory() {
    nlohmann::json doc = nlohmann::json::array();
    for (const auto& n : m_history)
        doc.push_back({{"id", n.id}, {"app", n.app}, {"appIcon", n.appIcon}, {"desktopEntry", n.desktopEntry},
                       {"summary", n.summary}, {"body", n.body}, {"imagePath", n.imagePath},
                       {"urgency", static_cast<int>(n.urgency)}, {"time", n.time}});
    std::error_code ec;
    std::filesystem::create_directories(paths::stateDir(), ec);
    const std::string tmp = historyFile() + ".tmp";
    {
        std::ofstream out(tmp);
        out << doc.dump();
    }
    std::filesystem::rename(tmp, historyFile(), ec);
}

void Notifications::setDoNotDisturb(bool on) {
    m_dnd = on;
    changed.emit();
}

} // namespace m3
