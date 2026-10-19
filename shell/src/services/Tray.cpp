#include "services/Tray.h"

#include "core/Guard.h"
#include "core/Log.h"

#include <algorithm>
#include <cerrno>
#include <ctime>
#include <systemd/sd-bus.h>
#include <unistd.h>

namespace m3 {

namespace {

const char* kWatcher = "org.kde.StatusNotifierWatcher";
const char* kWatcherPath = "/StatusNotifierWatcher";
const char* kItemIface = "org.kde.StatusNotifierItem";

const char* kIntrospect = R"(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
 <interface name="org.kde.StatusNotifierWatcher">
  <method name="RegisterStatusNotifierItem"><arg name="service" type="s" direction="in"/></method>
  <method name="RegisterStatusNotifierHost"><arg name="service" type="s" direction="in"/></method>
  <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
  <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
  <property name="ProtocolVersion" type="i" access="read"/>
  <signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
  <signal name="StatusNotifierItemUnregistered"><arg type="s"/></signal>
  <signal name="StatusNotifierHostRegistered"/>
 </interface>
</node>
)";

int onWatcherMessage(sd_bus_message* m, void* userdata, sd_bus_error*) {
    int r = -EIO; // a method that throws answers with an error instead
    guard("a tray item", [&] { r = static_cast<Tray*>(userdata)->handleWatcher(m); });
    return r;
}

// "service" as items register it: a bus name, a bus name + path, or a path
void splitService(const std::string& s, const std::string& sender, std::string& service, std::string& path) {
    if (s.starts_with('/')) {
        service = sender;
        path = s;
    } else if (auto slash = s.find('/'); slash != std::string::npos) {
        service = s.substr(0, slash);
        path = s.substr(slash);
    } else {
        service = s;
        path = "/StatusNotifierItem";
    }
}

} // namespace

Tray& Tray::get() {
    static Tray instance;
    return instance;
}

Tray::Tray() {
    auto& bus = dbus::Bus::session();
    if (!bus.connected()) return;
    // Clients that vanish take their items with them
    m_subs.push_back(bus.subscribe("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                   "NameOwnerChanged", [this](const dbus::Value& a, auto*) {
                                       if (a.size() >= 3 && a[2].get<std::string>().empty())
                                           removeService(a[0].get<std::string>());
                                   }));
    sd_bus_request_name_async(bus.raw(), nullptr, kWatcher, 0,
                              [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
                                  auto* self = static_cast<Tray*>(ud);
                                  if (sd_bus_message_is_method_error(m, nullptr))
                                      self->becomeHost();
                                  else
                                      self->becomeWatcher();
                                  return 0;
                              },
                              this);
}

void Tray::becomeWatcher() {
    m_isWatcher = true;
    sd_bus_add_object(dbus::Bus::session().raw(), &m_object, kWatcherPath, onWatcherMessage, this);
    log::info("tray: serving {}", kWatcher);
}

void Tray::becomeHost() {
    // Someone else (another bar) is the watcher: register with it
    auto& bus = dbus::Bus::session();
    const std::string host = "org.kde.StatusNotifierHost-" + std::to_string(getpid());
    sd_bus_request_name_async(bus.raw(), nullptr, host.c_str(), 0, nullptr, nullptr);
    bus.call(kWatcher, kWatcherPath, kWatcher, "RegisterStatusNotifierHost", dbus::args(host));
    m_subs.push_back(bus.subscribe(kWatcher, kWatcherPath, kWatcher, "StatusNotifierItemRegistered",
                                   [this](const dbus::Value& a, auto*) {
                                       std::string service, path;
                                       if (!a.empty()) splitService(a[0].get<std::string>(), "", service, path);
                                       addItem(service, path);
                                   }));
    m_subs.push_back(bus.subscribe(kWatcher, kWatcherPath, kWatcher, "StatusNotifierItemUnregistered",
                                   [this](const dbus::Value& a, auto*) {
                                       std::string service, path;
                                       if (a.empty()) return;
                                       splitService(a[0].get<std::string>(), "", service, path);
                                       std::erase_if(m_items, [&](auto& i) { return i.service == service && i.path == path; });
                                       changed.emit();
                                   }));
    bus.call(kWatcher, kWatcherPath, "org.freedesktop.DBus.Properties", "Get",
             dbus::args(kWatcher, "RegisteredStatusNotifierItems"), [this](const dbus::Value& a, const std::string& err) {
                 if (!err.empty() || a.empty() || !a[0].is_array()) return;
                 for (const auto& s : a[0]) {
                     std::string service, path;
                     splitService(s.get<std::string>(), "", service, path);
                     addItem(service, path);
                 }
             });
}

std::vector<std::string> Tray::registered() const {
    std::vector<std::string> out;
    for (const auto& i : m_items) out.push_back(i.service + i.path);
    return out;
}

int Tray::handleWatcher(sd_bus_message* m) {
    const char* iface = sd_bus_message_get_interface(m);
    const char* member = sd_bus_message_get_member(m);
    if (!iface || !member || !sd_bus_message_is_method_call(m, nullptr, nullptr)) return 0;
    const std::string i(iface), mem(member);
    auto* bus = dbus::Bus::session().raw();

    if (i == "org.freedesktop.DBus.Introspectable" && mem == "Introspect")
        return sd_bus_reply_method_return(m, "s", kIntrospect);

    if (i == kWatcher && mem == "RegisterStatusNotifierItem") {
        const char* arg = nullptr;
        sd_bus_message_read(m, "s", &arg);
        std::string service, path;
        splitService(arg ? arg : "", sd_bus_message_get_sender(m) ? sd_bus_message_get_sender(m) : "", service, path);
        addItem(service, path);
        sd_bus_emit_signal(bus, kWatcherPath, kWatcher, "StatusNotifierItemRegistered", "s", (service + path).c_str());
        return sd_bus_reply_method_return(m, "");
    }
    if (i == kWatcher && mem == "RegisterStatusNotifierHost") {
        sd_bus_emit_signal(bus, kWatcherPath, kWatcher, "StatusNotifierHostRegistered", "");
        return sd_bus_reply_method_return(m, "");
    }

    if (i == "org.freedesktop.DBus.Properties" && (mem == "Get" || mem == "GetAll")) {
        const char* ifaceArg = nullptr;
        const char* prop = nullptr;
        if (mem == "Get")
            sd_bus_message_read(m, "ss", &ifaceArg, &prop);
        else
            sd_bus_message_read(m, "s", &ifaceArg);
        sd_bus_message* reply = nullptr;
        sd_bus_message_new_method_return(m, &reply);
        auto appendItems = [&] {
            sd_bus_message_open_container(reply, 'v', "as");
            sd_bus_message_open_container(reply, 'a', "s");
            for (const auto& s : registered()) sd_bus_message_append(reply, "s", s.c_str());
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        };
        if (mem == "Get") {
            const std::string p = prop ? prop : "";
            if (p == "RegisteredStatusNotifierItems") appendItems();
            else if (p == "IsStatusNotifierHostRegistered") sd_bus_message_append(reply, "v", "b", 1);
            else if (p == "ProtocolVersion") sd_bus_message_append(reply, "v", "i", 0);
            else {
                sd_bus_message_unref(reply);
                return sd_bus_reply_method_errorf(m, "org.freedesktop.DBus.Error.UnknownProperty", "%s", p.c_str());
            }
        } else {
            sd_bus_message_open_container(reply, 'a', "{sv}");
            sd_bus_message_open_container(reply, 'e', "sv");
            sd_bus_message_append(reply, "s", "RegisteredStatusNotifierItems");
            appendItems();
            sd_bus_message_close_container(reply);
            sd_bus_message_append(reply, "{sv}", "IsStatusNotifierHostRegistered", "b", 1);
            sd_bus_message_append(reply, "{sv}", "ProtocolVersion", "i", 0);
            sd_bus_message_close_container(reply);
        }
        const int r = sd_bus_send(bus, reply, nullptr);
        sd_bus_message_unref(reply);
        return r;
    }
    return 0;
}

Tray::Item* Tray::find(const std::string& key) {
    auto it = std::ranges::find(m_items, key, &Item::key);
    return it != m_items.end() ? &*it : nullptr;
}

void Tray::addItem(const std::string& service, const std::string& path) {
    if (service.empty()) return;
    const std::string key = service + path;
    if (find(key)) return;
    Item item;
    item.key = key;
    item.service = service;
    item.path = path;
    m_items.push_back(item);
    // Any of the New* signals means "re-read the properties"
    m_itemSubs[key] = dbus::Bus::session().subscribe(service, path, kItemIface, "", [this, key](const dbus::Value&, auto*) {
        loadItem(key);
    });
    loadItem(key);
}

void Tray::removeService(const std::string& service) {
    const auto before = m_items.size();
    std::erase_if(m_items, [&](const Item& i) {
        if (i.service != service) return false;
        m_itemSubs.erase(i.key);
        if (m_isWatcher)
            sd_bus_emit_signal(dbus::Bus::session().raw(), kWatcherPath, kWatcher, "StatusNotifierItemUnregistered", "s",
                               i.key.c_str());
        return true;
    });
    if (m_items.size() != before) changed.emit();
}

void Tray::loadItem(const std::string& key) {
    Item* item = find(key);
    if (!item) return;
    dbus::Bus::session().getAll(item->service, item->path, kItemIface, [this, key](const dbus::Value& p, bool ok) {
        Item* it = find(key);
        if (!it || !ok) return;
        auto text = [&](const char* k) {
            auto v = p.find(k);
            return v != p.end() && v->is_string() ? v->get<std::string>() : std::string();
        };
        it->id = text("Id");
        it->title = text("Title");
        it->status = text("Status");
        it->iconName = text("IconName");
        it->iconThemePath = text("IconThemePath");
        it->menuPath = text("Menu");
        it->pixmap.clear();
        it->pixmapW = it->pixmapH = 0;
        // a(iiay): pick the smallest pixmap that is at least 32px, else the largest
        if (auto px = p.find("IconPixmap"); px != p.end() && px->is_array()) {
            const dbus::Value* best = nullptr;
            for (const auto& e : *px) {
                if (!e.is_array() || e.size() != 3 || !e[2].is_binary()) continue;
                const int w = e[0].get<int>();
                if (!best) best = &e;
                const int bw = (*best)[0].get<int>();
                if ((w >= 32 && (bw < 32 || w < bw)) || (bw < 32 && w > bw)) best = &e;
            }
            if (best) {
                it->pixmapW = (*best)[0].get<int>();
                it->pixmapH = (*best)[1].get<int>();
                it->pixmap = (*best)[2].get_binary();
            }
        }
        ++it->revision;
        changed.emit();
    });
}

void Tray::activate(const std::string& key, int x, int y) {
    if (Item* i = find(key)) dbus::Bus::session().call(i->service, i->path, kItemIface, "Activate", dbus::args(x, y));
}

void Tray::secondaryActivate(const std::string& key, int x, int y) {
    if (Item* i = find(key))
        dbus::Bus::session().call(i->service, i->path, kItemIface, "SecondaryActivate", dbus::args(x, y));
}

void Tray::contextMenu(const std::string& key, int x, int y) {
    if (Item* i = find(key)) dbus::Bus::session().call(i->service, i->path, kItemIface, "ContextMenu", dbus::args(x, y));
}

namespace {

// (ia{sv}av) → MenuItem; children arrive as variants holding the same struct
Tray::MenuItem parseMenu(const dbus::Value& node) {
    Tray::MenuItem item;
    if (!node.is_array() || node.size() < 3) return item;
    item.id = node[0].get<int>();
    const auto& p = node[1];
    auto str = [&](const char* k) {
        auto v = p.find(k);
        return v != p.end() && v->is_string() ? v->get<std::string>() : std::string();
    };
    // Mnemonics: "_Open" → "Open", "__" → "_"
    const std::string l = str("label");
    for (size_t i = 0, n = l.size(); i < n; ++i) {
        if (l[i] == '_') {
            if (i + 1 < n && l[i + 1] == '_') item.label += '_', ++i;
            continue;
        }
        item.label += l[i];
    }
    item.iconName = str("icon-name");
    if (auto e = p.find("enabled"); e != p.end() && e->is_boolean()) item.enabled = *e;
    if (auto v = p.find("visible"); v != p.end() && v->is_boolean()) item.visible = *v;
    item.separator = str("type") == "separator";
    item.toggleType = str("toggle-type");
    if (auto t = p.find("toggle-state"); t != p.end() && t->is_number()) item.toggled = t->get<int>() == 1;
    for (const auto& child : node[2]) item.children.push_back(parseMenu(child));
    return item;
}

} // namespace

void Tray::fetchMenu(const std::string& key, std::function<void(const MenuItem&)> done) {
    Item* i = find(key);
    if (!i || i->menuPath.empty()) return;
    auto& bus = dbus::Bus::session();
    const std::string service = i->service, path = i->menuPath;
    // Some apps build the menu lazily on AboutToShow
    bus.call(service, path, "com.canonical.dbusmenu", "AboutToShow", dbus::args(int32_t{0}));
    bus.call(service, path, "com.canonical.dbusmenu", "GetLayout",
             [](sd_bus_message* m) { sd_bus_message_append(m, "iias", 0, -1, 0); },
             [done = std::move(done)](const dbus::Value& a, const std::string& err) {
                 if (!err.empty() || a.size() < 2) return;
                 done(parseMenu(a[1]));
             });
}

void Tray::menuClicked(const std::string& key, int id) {
    Item* i = find(key);
    if (!i || i->menuPath.empty()) return;
    dbus::Bus::session().call(i->service, i->menuPath, "com.canonical.dbusmenu", "Event", [id](sd_bus_message* m) {
        sd_bus_message_append(m, "isvu", id, "clicked", "i", 0, static_cast<uint32_t>(time(nullptr)));
    });
}

void Tray::scroll(const std::string& key, int delta) {
    if (Item* i = find(key))
        dbus::Bus::session().call(i->service, i->path, kItemIface, "Scroll", dbus::args(static_cast<int32_t>(delta), "vertical"));
}

} // namespace m3
