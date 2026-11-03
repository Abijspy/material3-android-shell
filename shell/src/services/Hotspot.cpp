#include "services/Hotspot.h"

#include "core/Config.h"
#include "core/Process.h"
#include "services/Network.h"

#include <random>
#include <sstream>
#include <unistd.h>

namespace m3 {

namespace {
constexpr const char* kConnection = "Material3UI Hotspot";
} // namespace

Hotspot& Hotspot::get() {
    static Hotspot instance;
    return instance;
}

Hotspot::Hotspot() {
    // NetworkManager's changes: whether ours is up
    Network::get().changed.connectForever([this] { refresh(); });
    refresh();
}

std::string Hotspot::name() const {
    std::string set = Config::get().value<std::string>("/hotspot/name", "");
    if (!set.empty()) return set;
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    return std::string(host[0] ? host : "Material3UI") + " hotspot";
}

std::string Hotspot::password() const {
    std::string p = Config::get().value<std::string>("/hotspot/password", "");
    if (p.size() >= 8) return p;
    const_cast<Hotspot*>(this)->newPassword();
    return Config::get().value<std::string>("/hotspot/password", "");
}

void Hotspot::newPassword() {
    // Ten letters and digits, the easily confused ones left out
    static const std::string chars = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::uniform_int_distribution<size_t> pick(0, chars.size() - 1);
    std::string p;
    for (int i = 0; i < 10; ++i) p += chars[pick(rd)];
    Config::get().set("/hotspot/password", p);
    changed.emit();
}

void Hotspot::refresh() {
    run({"nmcli", "-t", "-f", "NAME", "connection", "show", "--active"}, [this](int code, const std::string& out) {
        bool on = false;
        if (code == 0) {
            std::stringstream ss(out);
            for (std::string line; std::getline(ss, line);)
                if (line == kConnection) on = true;
        }
        if (on == m_on) return;
        m_on = on;
        changed.emit();
    });
}

void Hotspot::set(bool on) {
    if (m_busy || on == m_on) return;
    m_busy = true;
    m_error.clear();
    changed.emit();
    // Its errors too (nmcli writes them to stderr); the name and password
    // as arguments, never through the shell's parsing
    std::vector<std::string> cmd = {"sh", "-c", "exec nmcli \"$@\" 2>&1", "sh"};
    if (on) cmd.insert(cmd.end(), {"device", "wifi", "hotspot", "con-name", kConnection, "ssid", name(), "password", password()});
    else cmd.insert(cmd.end(), {"connection", "down", kConnection});
    run(cmd, [this, on](int code, const std::string& out) {
        m_busy = false;
        if (code != 0) {
            // nmcli's own words, without its "Error: " in front
            std::string why = out.substr(0, out.find('\n'));
            if (why.starts_with("Error: ")) why = why.substr(7);
            m_error = why.empty() ? (on ? "Couldn't start the hotspot" : "Couldn't stop the hotspot") : why;
        }
        refresh();
        changed.emit();
    });
}

} // namespace m3
