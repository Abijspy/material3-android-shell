#pragma once

#include "core/Signal.h"
#include "dbus/Bus.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace m3 {

// MPRIS media players on the session bus. Tracks every player and picks the
// active one: the most recent to start playing, else any paused one.
class Mpris {
public:
    struct Player {
        std::string bus;      // org.mpris.MediaPlayer2.spotify
        std::string identity; // app name
        std::string title, artist, album, artUrl;
        std::string status;   // Playing | Paused | Stopped
        bool canNext = false, canPrevious = false;
        long long lastPlayed = 0;
    };

    static Mpris& get();

    const Player* active() const;
    const std::map<std::string, Player>& players() const { return m_players; }

    void playPause(const std::string& bus = {});
    void next(const std::string& bus = {});
    void previous(const std::string& bus = {});

    Signal<> changed;

private:
    Mpris();
    void add(const std::string& bus);
    void remove(const std::string& bus);
    void apply(Player& p, const dbus::Value& props);
    void command(const std::string& bus, const char* method);

    std::map<std::string, Player> m_players;
    std::map<std::string, dbus::Subscription> m_subs;
    dbus::Subscription m_owner;
    long long m_clock = 0;
};

} // namespace m3
