#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <set>
#include <string>
#include <vector>

namespace m3 {

// The calendar: your own events (kept in ~/.local/share/material3ui/
// calendar.json) and calendars subscribed to by their ICS link (Google,
// Outlook, Nextcloud, Proton…: "secret address in iCal format"), fetched
// every half hour and cached. Repeating events (RRULE: daily, weekly on
// given days, monthly, yearly; INTERVAL, COUNT, UNTIL; EXDATE) are
// expanded. A notification comes before each event, as its reminder says.
class Calendar {
public:
    struct Event {
        std::string id;       // unique (subscribed: the source's id + its UID)
        std::string title, location, notes;
        std::time_t start = 0, end = 0;
        bool allDay = false;
        std::string repeat;   // "", "daily", "weekly", "monthly", "yearly"
        int interval = 1;
        int count = 0;        // occurrences in all (0: no limit)
        std::time_t until = 0;
        std::vector<int> byDay; // weekly: 0 Sunday … 6 Saturday
        std::set<std::time_t> except; // EXDATE starts
        int remind = 10;      // minutes before (-1: none)
        std::string source;   // "" for your own, else a subscription's id
    };
    // One time an event happens
    struct Occurrence {
        const Event* event;
        std::time_t start, end;
    };
    struct Source {
        std::string id, name, url, colour;
        bool enabled = true;
        std::string error; // the last fetch's, if it failed
    };

    static Calendar& get();

    // Every occurrence overlapping [from, to), by start
    std::vector<Occurrence> between(std::time_t from, std::time_t to) const;
    // Days of a month (1–31) with something on
    std::set<int> busyDays(int year, int month) const;
    const Event* find(const std::string& id) const;

    // Your own events
    void save(Event event); // new (empty id) or changed
    void remove(const std::string& id);

    // Subscriptions
    const std::vector<Source>& sources() const { return m_sources; }
    void subscribe(const std::string& name, const std::string& url);
    void unsubscribe(const std::string& id);
    void setEnabled(const std::string& id, bool on);
    void refresh(); // fetch every subscription again
    bool refreshing() const { return m_fetching > 0; }

    Signal<> changed;

private:
    Calendar();
    void load();
    void store() const;
    void loadSources();
    void storeSources() const;
    void fetch(const Source& s);
    void parseIcs(const std::string& sourceId, const std::string& text);
    void remindLoop();

    std::vector<Event> m_own;
    std::vector<Event> m_subscribed;
    std::vector<Source> m_sources;
    std::set<std::string> m_reminded; // event id + start, already notified
    int m_fetching = 0;
    EventLoop::TimerId m_refreshTimer = 0, m_remindTimer = 0;
};

} // namespace m3
