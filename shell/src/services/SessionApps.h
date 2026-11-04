#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <string>
#include <vector>

namespace m3 {

// Apps reopened after login (Settings → Apps → Reopen apps after login):
// which are open, and on which workspace, noted every two minutes and as
// the shell logs out, reboots or shuts down; at the next login (not a
// restart of the shell: a mark in the runtime folder, gone at logout) each
// is opened again and, where the compositor can, its window put back on
// its workspace. Kept in ~/.local/state/material3ui/session-apps.json
class SessionApps {
public:
    struct Entry {
        std::string app, cls; // its desktop id, its window class
        std::vector<int> workspaces;
    };
    static SessionApps& get();
    void start();
    void save();
    std::vector<Entry> saved() const;
    std::time_t savedAt() const;

private:
    SessionApps() = default;
    void restore();
    struct Pending {
        std::string cls;
        std::vector<int> workspaces;
    };
    std::vector<Pending> m_pending;
    std::vector<std::string> m_seen; // windows there before the apps opened, or placed already
    Connection m_windows;
};

} // namespace m3
