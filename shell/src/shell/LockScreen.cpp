#include "shell/LockScreen.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "services/Auth.h"
#include "services/Session.h"
#include "services/Wallpaper.h"
#include "shell/LockView.h"
#include "wayland/Display.h"
#include "wayland/LayerSurface.h"

#include "ext-session-lock-v1-client-protocol.h"

#include <wayland-client.h>

namespace m3::shell {

namespace {

const ext_session_lock_v1_listener kLock = {
    .locked = [](void*, ext_session_lock_v1*) { LockScreen::get().lockedEvent(); },
    .finished = [](void*, ext_session_lock_v1*) { LockScreen::get().finishedEvent(); },
};

} // namespace

LockScreen& LockScreen::get() {
    static LockScreen instance;
    return instance;
}

void LockScreen::lock() {
    if (m_lock) return;
    auto* manager = static_cast<ext_session_lock_manager_v1*>(wl::Display::get().sessionLockManager());
    if (!manager) {
        log::error("the compositor does not support ext-session-lock-v1; cannot lock");
        return;
    }
    auto* lock = ext_session_lock_manager_v1_lock(manager);
    ext_session_lock_v1_add_listener(lock, &kLock, nullptr);
    m_lock = lock;
    m_confirmed = false;
    for (auto& o : wl::Display::get().outputs())
        if (o->ready) addOutput(o.get());
    lockedChanged.emit();
}

void LockScreen::addOutput(wl::Output* output) {
    if (!m_lock || m_surfaces.contains(output)) return;
    auto surface = std::make_unique<wl::LockSurface>(output, m_lock);
    LockView::Options opts;
    opts.user = auth::currentUser();
    opts.wallpaper = Wallpaper::get().lockPath();
    auto view = std::make_unique<LockView>(opts);
    LockView* raw = view.get();
    raw->onSubmit = [this, raw](const std::string& pw) { submit(raw, pw); };
    surface->setRoot(std::move(view));
    surface->setFocus(raw->field());
    m_views[output] = raw;
    m_surfaces[output] = std::move(surface);
}

void LockScreen::removeOutput(wl::Output* output) {
    m_views.erase(output);
    m_surfaces.erase(output);
}

void LockScreen::lockedEvent() {
    m_confirmed = true;
    log::info("session locked");
    // Everything is hidden now: a pending suspend may go ahead
    Session::get().lockReady();
}

void LockScreen::finishedEvent() {
    // Refused (another locker) or ended by the compositor
    if (!m_confirmed) log::warn("the compositor refused the lock");
    if (m_lock) ext_session_lock_v1_destroy(static_cast<ext_session_lock_v1*>(m_lock));
    m_lock = nullptr;
    teardown();
}

void LockScreen::submit(LockView* view, const std::string& password) {
    if (password.empty()) return;
    for (auto& [_, v] : m_views) v->setBusy(true);
    auth::check(auth::currentUser(), password, [this](bool ok, const std::string& message) {
        if (!m_lock) return;
        if (ok) return unlock();
        for (auto& [_, v] : m_views) v->fail(message);
    });
    (void)view;
}

void LockScreen::unlock() {
    if (!m_lock) return;
    // Unlock first, then drop the surfaces: destroying them first would show
    // a solid colour for a moment
    if (m_confirmed)
        ext_session_lock_v1_unlock_and_destroy(static_cast<ext_session_lock_v1*>(m_lock));
    else
        ext_session_lock_v1_destroy(static_cast<ext_session_lock_v1*>(m_lock));
    m_lock = nullptr;
    log::info("session unlocked");
    teardown();
}

void LockScreen::teardown() {
    m_views.clear();
    // Surfaces own their views; drop them outside any of their callbacks
    auto surfaces = std::make_shared<decltype(m_surfaces)>(std::move(m_surfaces));
    m_surfaces.clear();
    EventLoop::get().defer([surfaces] { surfaces->clear(); });
    lockedChanged.emit();
}

} // namespace m3::shell
