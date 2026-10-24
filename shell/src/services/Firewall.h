#pragma once

#include "core/Signal.h"

#include <string>
#include <vector>

namespace m3 {

// The firewall: ufw or firewalld, whichever runs. Reading needs no root
// (ufw's rule files are readable, firewalld answers queries); changes go
// through pkexec, one password prompt each.
class Firewall {
public:
    enum class Tool { None, Ufw, Firewalld };
    struct Rule {
        std::string action;    // allow, deny, reject, limit
        std::string port;      // "22", "1714:1764", "any"; firewalld: "ssh" for a service
        std::string proto;     // tcp, udp, any
        std::string from;      // "anywhere" or an address/network
        std::string direction; // in, out
        std::string comment;
        bool service = false;  // firewalld: a named service, not a port
        std::string describe() const; // "22/tcp from anywhere"
    };
    // Ports common apps need, to allow in one tap
    struct Preset {
        const char* name;
        const char* icon;
        std::vector<std::string> ports; // "47893/tcp", "1714:1764/udp"
    };
    static const std::vector<Preset>& presets();

    static Firewall& get();

    Tool tool() const { return m_tool; }
    bool installed() const { return m_tool != Tool::None; }
    bool enabled() const { return m_enabled; }
    bool blocksIncoming() const { return m_blockIncoming; } // the default for connections in
    const std::vector<Rule>& rules() const { return m_rules; }
    bool allows(const std::string& port) const; // "47893/tcp": an allow rule covers it

    void refresh();
    void setEnabled(bool on);
    void setBlockIncoming(bool block);
    void allow(const std::vector<std::string>& ports, const std::string& comment);
    void add(const std::string& action, const std::string& port, const std::string& proto, const std::string& comment);
    void remove(const Rule& r);
    void install(); // ufw, switched on, incoming blocked

    bool busy() const { return m_busy; }
    const std::string& lastError() const { return m_error; }
    Signal<> changed;

private:
    Firewall();
    void asRoot(const std::string& script);
    void readUfw();
    void readFirewalld();

    Tool m_tool = Tool::None;
    bool m_enabled = false, m_blockIncoming = true, m_busy = false;
    std::vector<Rule> m_rules;
    std::string m_error;
};

} // namespace m3
