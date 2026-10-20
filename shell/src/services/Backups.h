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
// each update and restore). The installer's own backups (the configs an
// install replaced, beside them) are listed too.
class Backups {
public:
    enum class Kind { Settings, Install };
    struct Entry {
        Kind kind;
        std::string path;   // the archive, or the installer's backup folder
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
    bool m_busy = false;
};

} // namespace m3
