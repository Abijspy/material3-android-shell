#include "services/Session.h"

#include "core/Config.h"
#include "core/Process.h"
#include "services/Hyprland.h"

#include <unistd.h>

namespace m3 {

namespace {
const char* kLogin = "org.freedesktop.login1";
const char* kManagerPath = "/org/freedesktop/login1";
const char* kManager = "org.freedesktop.login1.Manager";
} // namespace

Session& Session::get() {
    static Session instance;
    return instance;
}

Session::Session() {
    auto& bus = dbus::Bus::system();
    m_sleep = bus.subscribe(kLogin, kManagerPath, kManager, "PrepareForSleep", [this](const dbus::Value& a, auto*) {
        if (a.empty() || !a[0].is_boolean()) return;
        if (a[0].get<bool>()) {
            if (Config::get().value<bool>("/lock/lockBeforeSleep", true)) {
                lockRequested.emit();
            } else {
                lockReady();
            }
        } else {
            takeSleepInhibitor(); // resumed: be ready for the next suspend
        }
    });
    // Our own session's object, for lock-session / unlock-session
    bus.call(kLogin, kManagerPath, kManager, "GetSessionByPID", dbus::args(static_cast<uint32_t>(getpid())),
             [this](const dbus::Value& a, const std::string& err) {
                 if (!err.empty() || a.empty()) return;
                 m_sessionPath = a[0].get<std::string>();
                 auto& b = dbus::Bus::system();
                 m_lock = b.subscribe(kLogin, m_sessionPath, "org.freedesktop.login1.Session", "Lock",
                                      [this](const dbus::Value&, auto*) { lockRequested.emit(); });
                 m_unlock = b.subscribe(kLogin, m_sessionPath, "org.freedesktop.login1.Session", "Unlock",
                                        [this](const dbus::Value&, auto*) { unlockRequested.emit(); });
             });
    takeSleepInhibitor();
}

void Session::takeSleepInhibitor() {
    if (m_inhibitFd >= 0) return;
    dbus::Bus::system().call(kLogin, kManagerPath, kManager, "Inhibit",
                             dbus::args("sleep", "Material3UI", "Lock the screen before sleeping", "delay"),
                             [this](const dbus::Value& a, const std::string& err) {
                                 // The fd belongs to the reply message; keep our own copy
                                 if (err.empty() && !a.empty() && a[0].is_number()) m_inhibitFd = dup(a[0].get<int>());
                             });
}

void Session::lockReady() {
    if (m_inhibitFd >= 0) {
        close(m_inhibitFd);
        m_inhibitFd = -1;
    }
}

void Session::manager(const char* method) {
    // interactive=true lets polkit ask when the action needs authorisation
    dbus::Bus::system().call(kLogin, kManagerPath, kManager, method, dbus::args(true));
}

void Session::suspend() { manager("Suspend"); }
void Session::hibernate() { manager("Hibernate"); }
void Session::reboot() { manager("Reboot"); }
void Session::powerOff() { manager("PowerOff"); }

void Session::logout() {
    auto& h = Hyprland::get();
    if (h.available()) {
        h.request(h.usingLua() ? "dispatch hl.dsp.exit()" : "dispatch exit");
        return;
    }
    if (!m_sessionPath.empty())
        dbus::Bus::system().call(kLogin, m_sessionPath, "org.freedesktop.login1.Session", "Terminate");
}

} // namespace m3
