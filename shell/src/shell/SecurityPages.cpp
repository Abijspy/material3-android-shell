// Settings → VPN (NetworkManager's VPN and WireGuard connections, Cloudflare
// WARP) and Settings → Firewall (ufw or firewalld: on or off, what may come
// in, and the rules)
#include "shell/SecurityPages.h"

#include "services/Firewall.h"
#include "services/Vpn.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"

#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

Label* errorLine(Node* parent, const std::string& text) {
    auto* l = parent->add<Label>(text);
    l->setSize(13)->setColor("error", false)->setMaxWidth(640)->setMaxLines(3);
    return l;
}

std::string vpnKind(const std::string& type) {
    if (type == "wireguard") return "WireGuard";
    if (type == "openvpn") return "OpenVPN";
    if (type == "openconnect") return "OpenConnect";
    if (type == "vpnc") return "Cisco (vpnc)";
    if (type == "l2tp") return "L2TP";
    if (type == "pptp") return "PPTP";
    if (type == "strongswan" || type == "libreswan") return "IPsec";
    return type.empty() ? "VPN" : type;
}

} // namespace

void vpnPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(0)->setAlignItems(Align::Stretch);
    auto lease = std::make_shared<Vpn::Lease>(Vpn::get().watch());
    live->sync = [live, lease] {
        (void)lease;
        live->clearChildren();
        auto& v = Vpn::get();

        // Your connections: one switch each
        auto* g = group(live, "VPN connections", "WireGuard, OpenVPN and other VPNs NetworkManager knows; your traffic goes through the one that's on");
        for (const auto& c : v.connections()) {
            const bool busy = v.busy() == c.uuid || c.activating;
            auto r = row(g, c.type == "wireguard" ? "vpn_lock" : "vpn_key", c.name,
                         busy ? (c.active ? "Disconnecting…" : "Connecting…") : vpnKind(c.type) + (c.active ? " · Connected" : ""));
            r.title->setMaxWidth(320);
            const std::string uuid = c.uuid;
            confirmButton(r.row, "vpn:" + uuid, "delete", [uuid] { Vpn::get().remove(uuid); });
            auto* sw = r.row->add<Switch>();
            sw->setChecked(c.active || (busy && !c.active), false);
            const bool active = c.active;
            sw->onToggled = [uuid, active](bool on) {
                if (on && !active) Vpn::get().up(uuid);
                else if (!on && active) Vpn::get().down(uuid);
            };
        }
        if (v.connections().empty()) {
            auto r = row(g, "vpn_key_off", "No VPNs yet", "Import your provider's WireGuard (.conf) or OpenVPN (.ovpn) file");
            r.title->setColor("on_surface_variant", false);
        }
        auto add = row(g, "upload_file", "Add a VPN", "From a WireGuard .conf or an OpenVPN .ovpn file");
        pillButton(add.row, "Import", true, [] {
            if (open) open("files vpn");
        }, "add");
        if (!v.lastError().empty()) errorLine(live, v.lastError());

        // Cloudflare WARP, when installed
        if (v.warp() != Vpn::Warp::Absent) {
            auto* w = group(live, "Cloudflare WARP", "Cloudflare's free VPN: private DNS and an encrypted connection");
            const bool busy = v.busy() == "warp";
            switch (v.warp()) {
            case Vpn::Warp::NoDaemon: {
                auto r = row(w, "cloud_off", "WARP's service isn't running", "Start it once; it then starts with the computer");
                pillButton(r.row, busy ? "Starting…" : "Start", true, [] { Vpn::get().warpStartDaemon(); }, "play_arrow");
                break;
            }
            case Vpn::Warp::Unregistered: {
                auto r = row(w, "cloud", "Not set up yet", "Registers this computer with Cloudflare (free)");
                pillButton(r.row, busy ? "Setting up…" : "Set up", true, [] { Vpn::get().warpRegister(); });
                break;
            }
            default: {
                const bool on = v.warp() == Vpn::Warp::Connected;
                auto r = row(w, on ? "cloud_done" : "cloud", "WARP",
                             busy || v.warp() == Vpn::Warp::Connecting ? "Connecting…" : on ? "Connected: your traffic is encrypted" : "Off");
                auto* sw = r.row->add<Switch>();
                sw->setChecked(on, false);
                sw->onToggled = [](bool x) { Vpn::get().warpConnect(x); };
                break;
            }
            }
        }
        caption(live, "OpenVPN, Cisco and other VPN types need NetworkManager's plugin for them (networkmanager-openvpn and so on). "
                      "WireGuard works as it is.")
            ->setMaxWidth(640)
            ->setMaxLines(3);
    };
    live->watch(Vpn::get().changed);
    live->sync();
}

void firewallPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(0)->setAlignItems(Align::Stretch);
    // The rule being written, kept across rebuilds
    struct Draft {
        std::string port, proto = "any", action = "allow";
    };
    auto draft = std::make_shared<Draft>();
    Firewall::get().refresh();
    live->sync = [live, draft] {
        live->clearChildren();
        auto& f = Firewall::get();
        if (!f.installed()) {
            auto* g = group(live, "Firewall", "Nothing guards this computer's network ports");
            auto r = row(g, "shield", "No firewall installed", "ufw: blocks connections in, except the ones you allow");
            pillButton(r.row, f.busy() ? "Installing…" : "Install ufw", true, [] { Firewall::get().install(); }, "download");
            if (!f.lastError().empty()) errorLine(live, f.lastError());
            return;
        }
        const std::string tool = f.tool() == Firewall::Tool::Ufw ? "ufw" : "firewalld";

        auto* g = group(live, "Firewall", "Decides which devices on the network may connect to this computer");
        auto on = row(g, f.enabled() ? "shield_lock" : "remove_moderator", "Firewall",
                      f.busy() ? "Changing…" : f.enabled() ? "On · " + tool : "Off: every port is open to the network");
        if (!f.enabled()) on.subtitle->setColor("error", false);
        auto* sw = on.row->add<Switch>();
        sw->setChecked(f.enabled(), false);
        sw->onToggled = [](bool x) { Firewall::get().setEnabled(x); };
        if (f.tool() == Firewall::Tool::Ufw) {
            auto in = row(g, "login", "Connections in", "Blocked unless a rule below allows them");
            connectedGroup<bool>(in.row, f.blocksIncoming(), {{true, "Block"}, {false, "Allow"}},
                                 [](bool block) { Firewall::get().setBlockIncoming(block); });
            if (!f.blocksIncoming()) in.subtitle->setText("Everything may connect: rules only matter for what they deny");
        }
        if (!f.lastError().empty()) errorLine(live, f.lastError());

        // Apps that need a way in, allowed in one tap
        auto* apps = group(live, "Let apps through", "For apps that devices nearby connect to");
        for (const auto& p : Firewall::presets()) {
            const bool allowed = std::ranges::all_of(p.ports, [&](const std::string& x) { return f.allows(x); });
            std::string ports;
            for (const auto& x : p.ports) ports += (ports.empty() ? "" : ", ") + x;
            auto r = row(apps, p.icon, p.name, allowed ? "Allowed · " + ports : ports);
            if (allowed) {
                r.row->add<Icon>("check_circle", 22)->setFill(true)->setColor("primary", false);
            } else {
                const auto ports_ = p.ports;
                const std::string name = p.name;
                pillButton(r.row, "Allow", false, [ports_, name] { Firewall::get().allow(ports_, name); });
            }
        }

        // The rules
        auto* rules = group(live, "Rules", f.rules().empty() ? "None yet" : "");
        for (const auto& r : f.rules()) {
            const bool allow = r.action == "allow" || r.action == "limit";
            auto x = row(rules, allow ? "check_circle" : "block", r.comment.empty() ? r.describe() : r.comment,
                         (r.action == "limit" ? std::string("Allowed, rate-limited") : allow ? "Allowed" : r.action == "reject" ? "Rejected" : "Blocked") +
                             (r.comment.empty() ? "" : " · " + r.describe()));
            const Firewall::Rule rule = r;
            confirmButton(x.row, "fw:" + r.action + r.port + r.proto + r.from + r.direction, "delete",
                          [rule] { Firewall::get().remove(rule); });
        }

        // A new rule: port, protocol, allow or block
        auto add = row(rules, "add_circle", "Add a rule", "A port (22) or a range (1714-1764)");
        auto* port = add.row->add<TextField>();
        port->fixedWidth = 130;
        port->setPlaceholder("Port");
        port->setText(draft->port);
        port->onChanged = [draft](const std::string& t) { draft->port = t; };
        auto* opts = rules->add<Flex>(Flex::Direction::Row);
        opts->setPadding({20, 4, 20, 14})->setSpacing(10)->setJustify(Align::End)->setAlignItems(Align::Center);
        connectedGroup<std::string>(opts, draft->proto, {{"any", "TCP & UDP"}, {"tcp", "TCP"}, {"udp", "UDP"}},
                                    [draft](std::string p) { draft->proto = p; });
        if (f.tool() == Firewall::Tool::Ufw)
            connectedGroup<std::string>(opts, draft->action, {{"allow", "Allow"}, {"deny", "Block"}},
                                        [draft](std::string a) { draft->action = a; });
        pillButton(opts, "Add", true, [draft] {
            Firewall::get().add(draft->action, draft->port, draft->proto, "");
            draft->port.clear();
        }, "add");
        caption(live, "Changes ask for your password. Outgoing connections aren't limited.")->setMaxWidth(640);
    };
    live->watch(Firewall::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
