#pragma once

#include "core/Signal.h"
#include "dbus/Bus.h"

#include <string>

namespace m3 {

// logind: power actions, "lock before sleep" (with a delay inhibitor so the
// lock screen is up before the machine suspends), loginctl lock-session, and
// the idle hint.
class Session {
public:
    static Session& get();

    void suspend();
    void hibernate();
    void reboot();
    void powerOff();
    void logout();

    // The lock screen is up: let a pending suspend proceed
    void lockReady();

    Signal<> lockRequested;   // loginctl lock-session, or before sleep
    Signal<> unlockRequested; // loginctl unlock-session

private:
    Session();
    void takeSleepInhibitor();
    void manager(const char* method);
    void endSession(); // logout(), once its hooks have run

    std::string m_sessionPath;
    int m_inhibitFd = -1;
    dbus::Subscription m_sleep, m_lock, m_unlock;
};

} // namespace m3
