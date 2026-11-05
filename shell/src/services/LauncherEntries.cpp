#include "services/LauncherEntries.h"

#include <algorithm>
#include <systemd/sd-bus.h>

namespace m3 {

LauncherEntries& LauncherEntries::get() {
    static LauncherEntries l;
    return l;
}

std::string LauncherEntries::key(std::string id) {
    if (id.starts_with("application://")) id = id.substr(14);
    if (id.ends_with(".desktop")) id.resize(id.size() - 8);
    std::ranges::transform(id, id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return id;
}

const LauncherEntries::Entry* LauncherEntries::find(const std::string& desktopId) const {
    const auto it = m_entries.find(key(desktopId));
    return it == m_entries.end() ? nullptr : &it->second;
}

void LauncherEntries::start() {
    if (m_started) return;
    m_started = true;
    auto& bus = dbus::Bus::session();
    if (!bus.connected()) return;
    // Update(app_uri, {count, count-visible, progress, progress-visible, urgent, …}):
    // only what's sent changes
    m_updates = bus.subscribe("", "", "com.canonical.Unity.LauncherEntry", "Update", [this](const dbus::Value& a, sd_bus_message* m) {
        if (a.size() < 2 || !a[0].is_string() || !a[1].is_object()) return;
        const std::string k = key(a[0].get<std::string>());
        if (k.empty()) return;
        Entry& e = m_entries[k];
        const char* sender = sd_bus_message_get_sender(m);
        e.sender = sender ? sender : "";
        const auto& p = a[1];
        auto num = [&](const char* name, auto& out) {
            if (p.contains(name) && p[name].is_number()) out = p[name].get<std::remove_reference_t<decltype(out)>>();
        };
        auto flag = [&](const char* name, bool& out) {
            if (p.contains(name) && p[name].is_boolean()) out = p[name].get<bool>();
        };
        num("count", e.count);
        num("progress", e.progress);
        e.progress = std::clamp(e.progress, 0.0, 1.0);
        flag("count-visible", e.countVisible);
        flag("progress-visible", e.progressVisible);
        flag("urgent", e.urgent);
        changed.emit();
    });
    // An app that quits takes its badge with it
    m_owner = bus.subscribe("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                            [this](const dbus::Value& a, sd_bus_message*) {
                                if (a.size() < 3 || !a[0].is_string() || !a[2].is_string() || !a[2].get<std::string>().empty()) return;
                                const std::string gone = a[0].get<std::string>();
                                if (std::erase_if(m_entries, [&](const auto& kv) { return kv.second.sender == gone; }) > 0) changed.emit();
                            });
}

} // namespace m3
