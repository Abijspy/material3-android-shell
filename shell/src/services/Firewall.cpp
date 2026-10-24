#include "services/Firewall.h"

#include "core/Process.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

bool exists(const char* path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

// 'quoted' for sh
std::string quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string unhex(const std::string& hex) {
    std::string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
    return out;
}

// Ports and protocols as the user types them: "22", "1714-1764" or "1714:1764"
bool validPort(const std::string& p) {
    if (p.empty() || p.size() > 11) return false;
    return std::ranges::all_of(p, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == ':' || c == '-'; });
}

} // namespace

const std::vector<Firewall::Preset>& Firewall::presets() {
    static const std::vector<Preset> list = {
        {"Quick Share", "quickshare", {"47893/tcp"}}, // its own symbol (SecurityPages)
        {"KDE Connect", "phonelink", {"1714:1764/tcp", "1714:1764/udp"}},
        {"LocalSend", "send_to_mobile", {"53317/tcp", "53317/udp"}},
        {"Syncthing", "sync", {"22000/tcp", "22000/udp", "21027/udp"}},
        {"SSH", "terminal", {"22/tcp"}},
    };
    return list;
}

std::string Firewall::Rule::describe() const {
    std::string what = service ? port : port == "any" ? "All ports" : proto == "any" ? port : port + "/" + proto;
    std::string out = what;
    if (from != "anywhere") out += " from " + from;
    if (direction == "out") out += " (outgoing)";
    return out;
}

Firewall& Firewall::get() {
    static Firewall instance;
    return instance;
}

Firewall::Firewall() { refresh(); }

void Firewall::refresh() {
    if (exists("/usr/bin/ufw") || exists("/usr/sbin/ufw")) {
        m_tool = Tool::Ufw;
        readUfw();
    } else if (exists("/usr/bin/firewall-cmd")) {
        m_tool = Tool::Firewalld;
        readFirewalld();
    } else {
        m_tool = Tool::None;
        m_rules.clear();
        changed.emit();
    }
}

void Firewall::readUfw() {
    // Enabled: the service runs and ufw.conf says so
    run({"systemctl", "is-active", "-q", "ufw"}, [this](int code, const std::string&) {
        bool conf = false;
        std::ifstream in("/etc/ufw/ufw.conf");
        for (std::string l; std::getline(in, l);)
            if (l.starts_with("ENABLED=")) conf = l.find("yes") != std::string::npos;
        m_enabled = code == 0 && conf;
        std::ifstream def("/etc/default/ufw");
        for (std::string l; std::getline(def, l);)
            if (l.starts_with("DEFAULT_INPUT_POLICY=")) m_blockIncoming = l.find("ACCEPT") == std::string::npos;
        // "### tuple ### allow tcp 22 0.0.0.0/0 any 0.0.0.0/0 in comment=…":
        // action, proto, port, to, from port, from, [apps,] direction
        std::vector<Rule> rules;
        for (const char* file : {"/etc/ufw/user.rules", "/etc/ufw/user6.rules"}) {
            std::ifstream rf(file);
            for (std::string l; std::getline(rf, l);) {
                if (!l.starts_with("### tuple ### ")) continue;
                std::stringstream ss(l.substr(14));
                std::vector<std::string> t;
                for (std::string w; ss >> w;) t.push_back(w);
                if (t.size() < 7) continue;
                Rule r;
                r.action = t[0];
                r.proto = t[1];
                r.port = t[2];
                const std::string src = t[5];
                r.from = src == "0.0.0.0/0" || src == "::/0" ? "anywhere" : src;
                r.direction = t.size() >= 9 && !t[8].starts_with("comment=") ? t[8] : t[6];
                for (const auto& w : t)
                    if (w.starts_with("comment=")) r.comment = unhex(w.substr(8));
                // IPv4 and IPv6 copies of one rule show once
                const bool dup = std::ranges::any_of(rules, [&](const Rule& o) {
                    return o.action == r.action && o.proto == r.proto && o.port == r.port && o.from == r.from && o.direction == r.direction;
                });
                if (!dup) rules.push_back(std::move(r));
            }
        }
        m_rules = std::move(rules);
        changed.emit();
    });
}

void Firewall::readFirewalld() {
    run({"sh", "-c", "firewall-cmd --state 2>&1; echo '--'; firewall-cmd --list-ports 2>/dev/null; echo '--'; firewall-cmd --list-services 2>/dev/null"},
        [this](int, const std::string& out) {
            std::vector<std::string> parts(1);
            std::stringstream ss(out);
            for (std::string l; std::getline(ss, l);) {
                if (l == "--") parts.emplace_back();
                else parts.back() += l + " ";
            }
            parts.resize(3);
            m_enabled = parts[0].starts_with("running");
            m_blockIncoming = true; // a zone's default
            std::vector<Rule> rules;
            std::stringstream ports(parts[1]), services(parts[2]);
            for (std::string p; ports >> p;) {
                const auto slash = p.find('/');
                rules.push_back({"allow", p.substr(0, slash), slash == std::string::npos ? "any" : p.substr(slash + 1), "anywhere", "in", "", false});
            }
            for (std::string s; services >> s;) rules.push_back({"allow", s, "any", "anywhere", "in", "", true});
            m_rules = std::move(rules);
            changed.emit();
        });
}

bool Firewall::allows(const std::string& port) const {
    const auto slash = port.find('/');
    const std::string p = port.substr(0, slash), proto = slash == std::string::npos ? "any" : port.substr(slash + 1);
    return std::ranges::any_of(m_rules, [&](const Rule& r) {
        if (r.action != "allow" || r.direction != "in" || r.service) return false;
        if (r.proto != "any" && r.proto != proto) return false;
        if (r.port == p) return true;
        // A range: "1714:1764" covers "1714:1764" and the ports inside
        const auto colon = r.port.find(':');
        if (colon == std::string::npos || p.find(':') != std::string::npos) return false;
        const int n = std::atoi(p.c_str());
        return n >= std::atoi(r.port.c_str()) && n <= std::atoi(r.port.c_str() + colon + 1);
    });
}

void Firewall::asRoot(const std::string& script) {
    m_busy = true;
    m_error.clear();
    changed.emit();
    run({"pkexec", "sh", "-c", script}, [this](int code, const std::string& out) {
        m_busy = false;
        // 126/127: the password prompt was dismissed
        if (code != 0 && code != 126 && code != 127) {
            std::string s = out;
            while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
            if (const auto nl = s.rfind('\n'); nl != std::string::npos) s = s.substr(nl + 1);
            m_error = s.empty() ? "The firewall didn't take the change" : s;
        }
        refresh();
    });
}

void Firewall::setEnabled(bool on) {
    if (m_tool == Tool::Ufw) asRoot(on ? "systemctl enable --now ufw && ufw --force enable" : "ufw disable");
    else if (m_tool == Tool::Firewalld) asRoot(on ? "systemctl enable --now firewalld" : "systemctl disable --now firewalld");
}

void Firewall::setBlockIncoming(bool block) {
    if (m_tool == Tool::Ufw) asRoot(block ? "ufw default deny incoming" : "ufw default allow incoming");
}

void Firewall::allow(const std::vector<std::string>& ports, const std::string& comment) {
    std::string script;
    for (const auto& p : ports) {
        if (!script.empty()) script += " && ";
        if (m_tool == Tool::Ufw) script += "ufw allow " + quote(p) + " comment " + quote(comment);
        else script += "firewall-cmd --permanent --add-port=" + quote(p) + " && firewall-cmd --add-port=" + quote(p);
    }
    if (!script.empty()) asRoot(script);
}

void Firewall::add(const std::string& action, const std::string& port, const std::string& proto, const std::string& comment) {
    std::string p = port;
    std::ranges::replace(p, '-', ':');
    if (!validPort(p)) {
        m_error = "A port is a number (22) or a range (1714-1764)";
        changed.emit();
        return;
    }
    if (m_tool == Tool::Ufw) {
        // ufw wants a protocol with a range
        if (proto == "any" && p.find(':') != std::string::npos)
            return asRoot(std::format("ufw {0} {1}/tcp comment {2} && ufw {0} {1}/udp comment {2}", action, p, quote(comment)));
        asRoot(std::format("ufw {} {} comment {}", action, proto == "any" ? p : p + "/" + proto, quote(comment)));
    } else if (m_tool == Tool::Firewalld) {
        if (action != "allow") {
            m_error = "firewalld blocks what isn't allowed: remove the port's rule instead";
            changed.emit();
            return;
        }
        std::ranges::replace(p, ':', '-');
        std::string script;
        for (const std::string pr : {std::string("tcp"), std::string("udp")}) {
            if (proto != "any" && proto != pr) continue;
            if (!script.empty()) script += " && ";
            script += std::format("firewall-cmd --permanent --add-port={0}/{1} && firewall-cmd --add-port={0}/{1}", p, pr);
        }
        asRoot(script);
    }
}

void Firewall::remove(const Rule& r) {
    if (m_tool == Tool::Ufw) {
        // ufw's full syntax names the rule exactly
        std::string spec = r.action + " " + r.direction + " from " + (r.from == "anywhere" ? "any" : quote(r.from)) + " to any";
        if (r.port != "any") spec += " port " + r.port;
        if (r.proto != "any") spec += " proto " + r.proto;
        asRoot("ufw delete " + spec);
    } else if (m_tool == Tool::Firewalld) {
        const std::string what = r.service ? "service=" + r.port : "port=" + (r.proto == "any" ? r.port : r.port + "/" + r.proto);
        asRoot(std::format("firewall-cmd --permanent --remove-{0} && firewall-cmd --remove-{0}", what));
    }
}

void Firewall::install() {
    asRoot("pacman -S --needed --noconfirm ufw && systemctl enable --now ufw && ufw default deny incoming && ufw --force enable");
}

} // namespace m3
