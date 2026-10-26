#pragma once

#include "core/Signal.h"
#include "wayland/LayerSurface.h"

#include <functional>
#include <memory>

namespace m3::ui {
class Node;
}

namespace m3::shell {

// What's ringing (services/Alarms), a card each: an alarm's time and label
// with Snooze and Stop, a timer's "Time's up" with +1:00 and Stop. Hidden
// while nothing rings. The heads-up surface below and the lock screen show it
ui::Node* addRingingCards(ui::Node* parent, float width);

// The cards at the top of the screen, over everything (Android's heads-up
// alarm), from the moment something rings until it's stopped or snoozed.
// Click-through around them; never takes the keyboard.
class Ringer {
public:
    explicit Ringer(std::function<wl::Output*()> output);
    ~Ringer();

private:
    void sync();
    std::function<wl::Output*()> m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    Connection m_conn;
};

} // namespace m3::shell
