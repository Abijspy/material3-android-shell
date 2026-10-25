#pragma once

#include "core/Signal.h"
#include "dbus/Bus.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct sd_bus_message;
struct sd_bus_slot;

namespace m3 {

// The notification daemon (org.freedesktop.Notifications 1.2). Toasts and
// the notification centre render the current ones; once dismissed (or
// cleared) they go to the history, kept on disk for 30 days (300 at most)
// unless Settings → Notifications turns it off. Transient ones never do.
class Notifications {
public:
    enum Urgency : uint8_t { Low = 0, Normal = 1, Critical = 2 };

    struct Action {
        std::string key, label;
    };
    struct Image {
        int width = 0, height = 0, rowstride = 0;
        bool alpha = false;
        std::vector<uint8_t> data;
    };
    struct Notification {
        uint32_t id = 0;
        std::string app, appIcon, desktopEntry, summary, body, imagePath;
        std::vector<Action> actions;
        Image image;
        Urgency urgency = Normal;
        int timeoutMs = -1; // -1: server default, 0: never
        bool transient = false, resident = false;
        std::time_t time = 0;
    };

    static Notifications& get();

    bool active() const { return m_active; } // we own the bus name
    const std::vector<Notification>& current() const { return m_list; } // newest first
    const Notification* find(uint32_t id) const;

    // Dismissed ones, newest first (no image data: the app icon and image
    // path stay)
    const std::vector<Notification>& history() const { return m_history; }
    bool keepsHistory() const;
    void forget(uint32_t id, std::time_t time); // one, from the history
    void clearHistory();

    // Muted apps' notifications are dropped (critical ones still come);
    // by the name apps give
    bool muted(const std::string& app) const;
    void setMuted(const std::string& app, bool muted);

    bool doNotDisturb() const { return m_dnd; }
    void setDoNotDisturb(bool on);

    void invoke(uint32_t id, const std::string& actionKey);
    void dismiss(uint32_t id);
    void clear();

    Signal<uint32_t> posted;  // new or replaced: show a toast
    Signal<uint32_t> closed;  // gone from the history
    Signal<> changed;         // anything (history, DND)

    int handle(sd_bus_message* m);

    ~Notifications(); // a save still pending is written

private:
    Notifications();
    void close(uint32_t id, uint32_t reason);
    void remember(const Notification& n);
    void loadHistory();
    void saveHistory(); // soon, coalesced
    void writeHistory();
    static std::string cleanBody(const std::string& markup);

    std::vector<Notification> m_list;
    std::vector<Notification> m_history;
    uint64_t m_saveTimer = 0;
    uint32_t m_nextId = 1;
    bool m_active = false, m_dnd = false;
    sd_bus_slot* m_object = nullptr;
};

} // namespace m3
