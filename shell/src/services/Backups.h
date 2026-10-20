#pragma once

#include "core/Signal.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace m3 {

// Settings backups: the shell's settings and the Hyprland settings it
// writes, kept as timestamped archives in
// ~/.config/material3ui/backups/settings (made from Settings, and before
// each update, restore and reset). Only these: the installer's backups
// beside them undo an install (the first one's deletes the whole Hyprland
// config), so they are never offered here.
class Backups {
public:
    struct Entry {
        std::string path;   // the archive
        std::string reason; // "manual", "before-update", "before-restore"
        std::time_t time = 0;
        std::uintmax_t size = 0;
    };

    static Backups& get();

    // Newest first
    std::vector<Entry> list() const;
    // `done` gets whether it worked; automatic ones beyond the last ten go
    void create(const std::string& reason, std::function<void(bool)> done = {});
    // Backs up what's there now first, so a restore can be undone
    void restore(const Entry& entry, std::function<void(bool)> done = {});
    void remove(const Entry& entry);
    bool busy() const { return m_busy; }

    Signal<> changed;

private:
    Backups() = default;
    void prune();
    void restoreChecked(const std::string& path, std::function<void(bool)> done);
    bool m_busy = false;
};

} // namespace m3
