#pragma once

#include "core/Signal.h"
#include "dbus/Bus.h"

#include <cstdint>
#include <map>
#include <string>

namespace m3 {

// What apps tell a dock about themselves (the Unity launcher API that
// Plank and Dash to Dock read too): an unread count, a progress (a
// download, a copy) and asking for attention. Apps send it as the
// com.canonical.Unity.LauncherEntry Update signal, keyed by their desktop
// file; Chromium and Electron apps, Nautilus, Telegram, Thunderbird and
// others do. An app's go when it quits.
class LauncherEntries {
public:
    struct Entry {
        int64_t count = 0;
        bool countVisible = false;
        double progress = 0; // 0 to 1
        bool progressVisible = false;
        bool urgent = false;
        std::string sender; // its bus name: gone with it
        bool showsCount() const { return countVisible && count > 0; }
        bool showsProgress() const { return progressVisible && progress > 0; }
    };

    static LauncherEntries& get();
    void start(); // once
    // By desktop id, with or without ".desktop" (any case); null if it said nothing
    const Entry* find(const std::string& desktopId) const;
    Signal<> changed;

private:
    LauncherEntries() = default;
    static std::string key(std::string id);
    std::map<std::string, Entry> m_entries; // key() → what it said
    dbus::Subscription m_updates, m_owner;
    bool m_started = false;
};

} // namespace m3
