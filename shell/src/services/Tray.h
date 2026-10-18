#pragma once

#include "core/Signal.h"
#include "dbus/Bus.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;

namespace m3 {

// System tray (KDE/freedesktop StatusNotifierItem). Owns
// org.kde.StatusNotifierWatcher when nobody else does; otherwise registers as
// a host with the existing watcher. Items are kept fresh from their signals.
class Tray {
public:
    struct Item {
        std::string key;     // bus name + object path
        std::string service; // bus name
        std::string path;
        std::string id, title, status, iconName, iconThemePath;
        std::string menuPath; // DBusMenu object, if the item has one
        int pixmapW = 0, pixmapH = 0;
        std::vector<uint8_t> pixmap; // ARGB32, network order
        int revision = 0;            // bumps when the icon changes
    };

    static Tray& get();

    const std::vector<Item>& items() const { return m_items; }
    void activate(const std::string& key, int x, int y);
    void secondaryActivate(const std::string& key, int x, int y);
    void contextMenu(const std::string& key, int x, int y);
    void scroll(const std::string& key, int delta);

    // DBusMenu (com.canonical.dbusmenu)
    struct MenuItem {
        int id = 0;
        std::string label, iconName;
        bool enabled = true, visible = true, separator = false;
        std::string toggleType; // "checkmark", "radio" or empty
        bool toggled = false;
        std::vector<MenuItem> children;
    };
    void fetchMenu(const std::string& key, std::function<void(const MenuItem& root)> done);
    void menuClicked(const std::string& key, int id);

    Signal<> changed;

    // sd-bus object callback (watcher mode)
    int handleWatcher(sd_bus_message* m);

private:
    Tray();
    void becomeWatcher();
    void becomeHost();
    void addItem(const std::string& service, const std::string& path);
    void removeService(const std::string& service);
    void loadItem(const std::string& key);
    Item* find(const std::string& key);
    std::vector<std::string> registered() const;

    std::vector<Item> m_items;
    std::map<std::string, dbus::Subscription> m_itemSubs;
    std::vector<dbus::Subscription> m_subs;
    sd_bus_slot* m_object = nullptr;
    bool m_isWatcher = false;
};

} // namespace m3
