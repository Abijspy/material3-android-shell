#include "services/Mpris.h"

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

void Mpris::playPause(const std::string& bus) { command(bus, "PlayPause"); }
void Mpris::next(const std::string& bus) { command(bus, "Next"); }
void Mpris::previous(const std::string& bus) { command(bus, "Previous"); }

} // namespace m3
