#include "services/Sounds.h"

#include "core/Config.h"
#include "core/Process.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Screenshot.h"

#include <algorithm>
#include <filesystem>
#include <memory>

namespace m3 {

namespace {

const char* file(Sounds::Event e) {
    switch (e) {
    case Sounds::Event::Plugged: return "power-plug.oga";
    case Sounds::Event::Unplugged: return "power-unplug.oga";
    case Sounds::Event::Notification: return "message-new-instant.oga";
    case Sounds::Event::Volume: return "audio-volume-change.oga";
    case Sounds::Event::Screenshot: return "screen-capture.oga";
    }
    return "bell.oga";
}

const char* key(Sounds::Event e) {
    switch (e) {
    case Sounds::Event::Plugged:
    case Sounds::Event::Unplugged: return "/sounds/charger";
    case Sounds::Event::Notification: return "/sounds/notifications";
    case Sounds::Event::Volume: return "/sounds/volume";
    case Sounds::Event::Screenshot: return "/sounds/screenshots";
    }
    return "/sounds/enabled";
}

} // namespace

Sounds& Sounds::get() {
    static Sounds instance;
    return instance;
}

void Sounds::preview(Event e) {
    const std::string path = std::string("/usr/share/sounds/freedesktop/stereo/") + file(e);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;
    // An event sound: the sound panel leaves those out of its apps
    if (haveCommand("pw-play")) run({"pw-play", "--media-role=event", path}, nullptr);
    else run({"paplay", "--property=media.role=event", path}, nullptr);
}

void Sounds::play(Event e) {
    const auto& c = Config::get();
    if (!c.value<bool>("/sounds/enabled", false) || !c.value<bool>(key(e), true)) return;
    if (e == Event::Volume) {
        // Held keys step quickly: a tick at most every 90 ms
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastVolume < std::chrono::milliseconds(90)) return;
        m_lastVolume = now;
    }
    preview(e);
}

void Sounds::start() {
    // The charger: plugged in or out (not the first look)
    auto plugged = std::make_shared<int>(-1);
    Power::get().changed.connectForever([this, plugged] {
        const int now = Power::get().pluggedIn() ? 1 : 0;
        if (*plugged >= 0 && now != *plugged) play(now ? Event::Plugged : Event::Unplugged);
        *plugged = now;
    });
    // A notification arriving: a new one (an update to one isn't), not in
    // Do not disturb, nor from an app you've quietened
    Notifications::get().posted.connectForever([this](uint32_t id) {
        if (std::ranges::find(m_told, id) != m_told.end()) return;
        m_told.push_back(id);
        if (m_told.size() > 200) m_told.pop_front();
        auto& n = Notifications::get();
        const auto* note = n.find(id);
        if (!note || !n.getsThrough(*note) || n.muted(note->app)) return;
        play(Event::Notification);
    });
    Screenshot::get().taken.connectForever([this](const std::string&) { play(Event::Screenshot); });
}

} // namespace m3
