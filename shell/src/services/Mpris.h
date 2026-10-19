#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "dbus/Bus.h"

#include <chrono>
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
        std::string trackId;  // mpris:trackid, for SetPosition
        bool canNext = false, canPrevious = false, canSeek = false;
        long long lastPlayed = 0;
        long long length = 0;   // µs; 0 when the player doesn't say
        long long position = 0; // µs, as last read
        std::chrono::steady_clock::time_point positionAt; // when it was read
        // Where playback is now: the last reading, moved on while playing
        long long positionNow() const;
    };

    static Mpris& get();

    const Player* active() const;
    const std::map<std::string, Player>& players() const { return m_players; }

    void playPause(const std::string& bus = {});
    void next(const std::string& bus = {});
    void previous(const std::string& bus = {});
    // Jumps to `fraction` (0–1) of the track
    void seek(double fraction, const std::string& bus = {});
    // The cover as a local file: file:// as is, web art downloaded once to
    // ~/.cache/material3ui/art ("" until it's there)
    std::string artPath(const Player& p);
    // Position is read every second while a player is on screen
    using Lease = std::shared_ptr<void>;
    [[nodiscard]] Lease watchPosition();

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
    void readPositions();
    int m_positionLeases = 0;
    EventLoop::TimerId m_positionTimer = 0;
    std::map<std::string, std::string> m_art; // art URL → cached file ("" while downloading)
};

} // namespace m3
