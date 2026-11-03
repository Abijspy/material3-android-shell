#pragma once

#include "core/Signal.h"

#include <string>

namespace m3 {

// A Wi-Fi hotspot: this computer's connection shared over its Wi-Fi card,
// as NetworkManager makes one (nmcli device wifi hotspot), named
// /hotspot/name with /hotspot/password (WPA2, made up the first time).
// Most cards can't join a network and offer one at once: their Wi-Fi
// connection pauses while it's on (Ethernet or a phone's USB stays)
class Hotspot {
public:
    static Hotspot& get();
    bool on() const { return m_on; }
    bool busy() const { return m_busy; }
    const std::string& error() const { return m_error; }
    std::string name() const;
    std::string password() const; // made up and kept the first time it's asked for
    void set(bool on);
    void newPassword();
    Signal<> changed;

private:
    Hotspot();
    void refresh();
    bool m_on = false, m_busy = false;
    std::string m_error;
};

} // namespace m3
