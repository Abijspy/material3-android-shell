#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>

namespace m3 {

// Gentle sounds for what happens (Settings → Sound → Sound effects, off
// until switched on): the charger plugged in or out, a notification
// arriving, the volume stepping, a screenshot. The freedesktop sound
// theme's, played through PipeWire as event sounds (not listed among the
// apps playing)
class Sounds {
public:
    enum class Event { Plugged, Unplugged, Notification, Volume, Screenshot };
    static Sounds& get();
    void start();
    void play(Event e);            // if switched on
    static void preview(Event e);  // whatever the switches say (Settings)

private:
    Sounds() = default;
    std::chrono::steady_clock::time_point m_lastVolume{};
    std::deque<uint32_t> m_told; // notifications sounded for (a replaced one isn't again)
};

} // namespace m3
