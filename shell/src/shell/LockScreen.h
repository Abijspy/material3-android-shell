#pragma once

#include "core/Signal.h"

#include <map>
#include <memory>

namespace m3::wl {
struct Output;
class LockSurface;
} // namespace m3::wl

namespace m3::shell {

class LockView;

// The session lock (ext-session-lock-v1): one lock surface per output, PAM
// to unlock. The compositor keeps everything hidden until we unlock, even if
// the shell crashes.
class LockScreen {
public:
    static LockScreen& get();

    void lock();
    bool locked() const { return m_lock != nullptr; }

    void addOutput(wl::Output* output);
    void removeOutput(wl::Output* output);

    // Protocol callbacks
    void lockedEvent();
    void finishedEvent();

    Signal<> lockedChanged;

private:
    LockScreen() = default;
    void submit(LockView* view, const std::string& password);
    void unlock();
    void teardown();

    void* m_lock = nullptr; // ext_session_lock_v1*
    bool m_confirmed = false;
    std::map<wl::Output*, std::unique_ptr<wl::LockSurface>> m_surfaces;
    std::map<wl::Output*, LockView*> m_views;
};

} // namespace m3::shell
