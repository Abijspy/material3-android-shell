#include "services/Mpris.h"

#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <systemd/sd-bus.h>

namespace m3 {

namespace {
std::string text(const dbus::Value& obj, const char* key) {
    auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : "";
}

const char* kPrefix = "org.mpris.MediaPlayer2.";
const char* kPath = "/org/mpris/MediaPlayer2";
const char* kPlayer = "org.mpris.MediaPlayer2.Player";
} // namespace

Mpris& Mpris::get() {
    static Mpris instance;
    return instance;
}

Mpris::Mpris() {
    auto& bus = dbus::Bus::session();
    m_owner = bus.subscribe("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                            [this](const dbus::Value& a, auto*) {
                                if (a.size() < 3) return;
                                const auto name = a[0].get<std::string>();
                                if (!name.starts_with(kPrefix)) return;
                                if (a[2].get<std::string>().empty())
                                    remove(name);
                                else
                                    add(name);
                            });
    bus.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", {},
             [this](const dbus::Value& a, const std::string& err) {
                 if (!err.empty() || a.empty()) return;
                 for (const auto& n : a[0])
                     if (n.get<std::string>().starts_with(kPrefix)) add(n.get<std::string>());
             });
}

void Mpris::add(const std::string& name) {
    auto& p = m_players[name];
    p.bus = name;
    p.identity = name.substr(std::char_traits<char>::length(kPrefix));
    auto& bus = dbus::Bus::session();
    m_subs[name] = bus.onPropertiesChanged(name, kPath, [this, name](const dbus::Value& c, const std::string& iface) {
        auto it = m_players.find(name);
        if (it == m_players.end() || iface != kPlayer) return;
        apply(it->second, c);
    });
    bus.getAll(name, kPath, kPlayer, [this, name](const dbus::Value& props, bool ok) {
        auto it = m_players.find(name);
        if (ok && it != m_players.end()) apply(it->second, props);
    });
    bus.getAll(name, kPath, "org.mpris.MediaPlayer2", [this, name](const dbus::Value& props, bool ok) {
        auto it = m_players.find(name);
        if (!ok || it == m_players.end()) return;
        if (auto id = props.find("Identity"); id != props.end() && id->is_string()) it->second.identity = *id;
        changed.emit();
    });
}

void Mpris::remove(const std::string& name) {
    m_subs.erase(name);
    if (m_players.erase(name)) changed.emit();
}

void Mpris::apply(Player& p, const dbus::Value& props) {
    if (auto s = props.find("PlaybackStatus"); s != props.end() && s->is_string()) {
        if (*s == "Playing" && p.status != "Playing") p.lastPlayed = ++m_clock;
        p.status = *s;
    }
    if (auto m = props.find("Metadata"); m != props.end() && m->is_object()) {
        p.title = text(*m, "xesam:title");
        p.album = text(*m, "xesam:album");
        p.artUrl = text(*m, "mpris:artUrl");
        p.trackId = text(*m, "mpris:trackid");
        if (auto l = m->find("mpris:length"); l != m->end() && l->is_number()) p.length = l->get<long long>();
        else p.length = 0;
        p.artist.clear();
        if (auto a = m->find("xesam:artist"); a != m->end()) {
            if (a->is_array()) {
                for (const auto& x : *a)
                    if (x.is_string()) p.artist += (p.artist.empty() ? "" : ", ") + x.get<std::string>();
            } else if (a->is_string()) {
                p.artist = *a;
            }
        }
    }
    if (auto n = props.find("CanGoNext"); n != props.end() && n->is_boolean()) p.canNext = *n;
    if (auto n = props.find("CanGoPrevious"); n != props.end() && n->is_boolean()) p.canPrevious = *n;
    if (auto n = props.find("CanSeek"); n != props.end() && n->is_boolean()) p.canSeek = *n;
    if (auto n = props.find("Position"); n != props.end() && n->is_number()) {
        p.position = n->get<long long>();
        p.positionAt = std::chrono::steady_clock::now();
    }
    changed.emit();
}

const Mpris::Player* Mpris::active() const {
    const Player* best = nullptr;
    for (const auto& [_, p] : m_players) {
        if (p.title.empty() && p.status.empty()) continue;
        auto rank = [](const Player& x) { return x.status == "Playing" ? 2 : x.status == "Paused" ? 1 : 0; };
        if (!best || rank(p) > rank(*best) || (rank(p) == rank(*best) && p.lastPlayed > best->lastPlayed)) best = &p;
    }
    return best && best->status != "Stopped" ? best : nullptr;
}

void Mpris::command(const std::string& bus, const char* method) {
    std::string target = bus;
    if (target.empty()) {
        if (const Player* p = active()) target = p->bus;
    }
    if (!target.empty()) dbus::Bus::session().call(target, kPath, kPlayer, method);
}

long long Mpris::Player::positionNow() const {
    long long at = position;
    if (status == "Playing" && positionAt.time_since_epoch().count() != 0)
        at += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - positionAt).count();
    return length > 0 ? std::min(at, length) : at;
}

void Mpris::readPositions() {
    // Players don't announce Position as it moves: it has to be asked for
    for (auto& [name, p] : m_players) {
        if (p.status.empty()) continue;
        dbus::Bus::session().call(name, kPath, "org.freedesktop.DBus.Properties", "Get", dbus::args(kPlayer, "Position"),
                                  [this, name](const dbus::Value& a, const std::string& err) {
            auto it = m_players.find(name);
            if (!err.empty() || it == m_players.end() || a.empty() || !a[0].is_number()) return;
            it->second.position = a[0].get<long long>();
            it->second.positionAt = std::chrono::steady_clock::now();
            changed.emit();
        });
    }
}

Mpris::Lease Mpris::watchPosition() {
    if (m_positionLeases++ == 0) {
        readPositions();
        m_positionTimer = EventLoop::get().addTimer(1000, [this] { readPositions(); }, true);
    }
    return Lease(nullptr, [this](void*) {
        if (--m_positionLeases == 0 && m_positionTimer) {
            EventLoop::get().cancel(m_positionTimer);
            m_positionTimer = 0;
        }
    });
}

void Mpris::seek(double fraction, const std::string& bus) {
    auto it = bus.empty() ? m_players.end() : m_players.find(bus);
    const Player* p = it != m_players.end() ? &it->second : active();
    if (!p || p->length <= 0 || !p->canSeek) return;
    const long long target = static_cast<long long>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(p->length));
    const std::string track = p->trackId.empty() ? "/org/mpris/MediaPlayer2/TrackList/NoTrack" : p->trackId;
    const std::string name = p->bus;
    dbus::Bus::session().call(name, kPath, kPlayer, "SetPosition", [track, target](sd_bus_message* m) {
        sd_bus_message_append(m, "ox", track.c_str(), static_cast<int64_t>(target));
    });
    // Shown at once; the next reading confirms it
    auto& mut = m_players[name];
    mut.position = target;
    mut.positionAt = std::chrono::steady_clock::now();
    changed.emit();
}

std::string Mpris::artPath(const Player& p) {
    const std::string& url = p.artUrl;
    if (url.starts_with("file://")) return url.substr(7);
    if (!url.starts_with("http://") && !url.starts_with("https://")) return "";
    // Known: the file, "" while it downloads, "!" if it couldn't be had
    if (auto it = m_art.find(url); it != m_art.end()) return it->second == "!" ? "" : it->second;
    // Download once, named after the URL
    const std::string dir = std::filesystem::path(paths::home()) / ".cache/material3ui/art";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string file = dir + "/" + std::to_string(std::hash<std::string>{}(url));
    if (std::filesystem::exists(file, ec)) return m_art[url] = file;
    m_art[url] = "";
    run({"curl", "-fsSL", "--max-time", "15", "-o", file, url}, [this, url, file](int code, const std::string&) {
        std::error_code ec;
        if (code == 0 && std::filesystem::exists(file, ec)) {
            m_art[url] = file;
            changed.emit();
        } else {
            m_art[url] = "!"; // no retrying on every redraw
        }
    });
    return "";
}

void Mpris::playPause(const std::string& bus) { command(bus, "PlayPause"); }
void Mpris::next(const std::string& bus) { command(bus, "Next"); }
void Mpris::previous(const std::string& bus) { command(bus, "Previous"); }

} // namespace m3
