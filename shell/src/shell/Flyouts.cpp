// Floating panels for the bar's chips (and their desktop widgets): what
// you'd open Settings for, at hand beside the chip, with the page a tap
// away at the bottom. Phone (KDE Connect), Updates, Weather, Digital
// Wellbeing, Printers, Firewall and VPN.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Firewall.h"
#include "services/Firmware.h"
#include "services/KdeConnect.h"
#include "services/Packages.h"
#include "services/Printers.h"
#include "services/Vpn.h"
#include "services/Weather.h"
#include "services/Wellbeing.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell::panels {

using namespace m3::ui;
using kit::Live;

namespace {

// The page in Settings, at the bottom: the panel closes as it opens
void settingsLink(Flex* card, Popup& popup, const std::string& label, const std::string& page) {
    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setJustify(Align::End);
    pillButton(foot, label, false, [&popup, page] {
        popup.close();
        if (open) open("settings " + page);
    }, "settings");
}

// A header with a switch at its end
Switch* headerSwitch(Flex* head) {
    auto* sw = head->add<Switch>();
    return sw;
}

// A line of quieter text
Label* note(Node* parent, const std::string& text) {
    auto* l = parent->add<Label>(text);
    l->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(330)->setMaxLines(3);
    return l;
}

// A tonal button with an icon over a word (the phone's actions)
void action(Flex* row, const char* glyph, const std::string& label, std::function<void()> fn) {
    auto* b = row->add<Button>(Flex::Direction::Column);
    b->setPadding({8, 10})->setSpacing(4)->setRadius(Radii(18))->setBackground("secondary_container", false);
    b->pressedRadius = 10;
    b->grow = true;
    b->setAlignItems(Align::Center);
    b->add<Icon>(glyph, 22)->setColor("on_secondary_container", false);
    b->add<Label>(label)->setSize(12)->setWeight(500)->setColor("on_secondary_container", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT) fn();
    };
}

std::string hm(int seconds) {
    const int m = seconds / 60;
    return m >= 60 ? std::format("{} h {} min", m / 60, m % 60) : std::format("{} min", m);
}

std::string ago(std::time_t t) {
    if (!t) return "not checked yet";
    const long s = static_cast<long>(std::time(nullptr) - t);
    if (s < 90) return "checked just now";
    if (s < 3600) return std::format("checked {} min ago", s / 60);
    if (s < 86400) return std::format("checked {} h ago", s / 3600);
    return std::format("checked {} days ago", s / 86400);
}

} // namespace

// ── Phone (KDE Connect) ──

void phone(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(12)->setAlignItems(Align::Stretch);
    live->sync = [live] {
        live->clearChildren();
        auto& k = KdeConnect::get();
        auto* head = kit::header(live, "smartphone", "Phone");
        if (!KdeConnect::installed()) {
            note(live, "KDE Connect shows your phone's notifications here, its battery, and sends files and the clipboard across.");
            pillButton(live, "Install KDE Connect", true, [] { Packages::get().installTools("KDE Connect", {"kdeconnect"}); }, "download");
            return;
        }
        auto* sw = headerSwitch(head);
        sw->setChecked(k.enabled(), false);
        sw->onToggled = [](bool on) { Config::get().set("/kdeconnect/enabled", on); };
        if (!k.enabled()) {
            note(live, "Off. Switched on, this computer shows up in the KDE Connect app on your phone.");
            return;
        }
        if (!k.running()) {
            note(live, "Starting KDE Connect…");
            return;
        }
        bool any = false;
        for (const auto& d : k.devices()) {
            if (!d.reachable && !d.paired) continue;
            any = true;
            auto* box = live->add<Flex>(Flex::Direction::Column);
            box->setPadding({16, 14})->setSpacing(12)->setRadius(Radii(24))->setBackground("surface_container_high", false);
            box->setAlignItems(Align::Stretch);
            auto* top = box->add<Flex>(Flex::Direction::Row);
            top->setSpacing(12);
            top->add<Icon>(d.glyph(), 28)->setColor("primary", false);
            auto* names = top->add<Flex>(Flex::Direction::Column);
            names->setAlignItems(Align::Start)->setSpacing(2);
            names->grow = true;
            names->add<Label>(d.name)->setSize(16)->setWeight(600)->setMaxWidth(220);
            names->add<Label>(d.pairRequested ? "Wants to pair" : !d.paired ? "Not paired" : d.reachable ? "Connected" : "Not connected")
                ->setSize(12)
                ->setColor("on_surface_variant", false);
            // Its charge, large
            if (d.battery >= 0) {
                auto* charge = top->add<Flex>(Flex::Direction::Row);
                charge->setSpacing(2);
                const int bars = std::clamp(d.battery * 7 / 100, 0, 6);
                charge->add<Icon>(d.charging ? "battery_charging_full" : d.battery >= 95 ? "battery_full" : std::format("battery_{}_bar", bars), 22)
                    ->setColor(d.battery <= 15 && !d.charging ? "error" : "on_surface", false);
                charge->add<Label>(std::format("{}%", d.battery))->setSize(15)->setWeight(600);
            }
            const std::string id = d.id;
            if (d.pairRequested) {
                auto* row = box->add<Flex>(Flex::Direction::Row);
                row->setSpacing(8)->setJustify(Align::End);
                pillButton(row, "Reject", false, [id] { KdeConnect::get().reject(id); });
                pillButton(row, "Accept", true, [id] { KdeConnect::get().accept(id); }, "check");
                continue;
            }
            if (!d.paired) {
                pillButton(box, "Pair", true, [id] { KdeConnect::get().pair(id); }, "link");
                continue;
            }
            if (!d.reachable) continue;
            auto* actions = box->add<Flex>(Flex::Direction::Row);
            actions->setSpacing(8)->setAlignItems(Align::Stretch);
            if (d.has("findmyphone")) action(actions, "ring_volume", "Ring", [id] { KdeConnect::get().ring(id); });
            if (d.has("ping")) action(actions, "notifications_active", "Ping", [id] { KdeConnect::get().ping(id); });
            if (d.has("clipboard")) action(actions, "content_paste_go", "Clipboard", [id] { KdeConnect::get().sendClipboard(id); });
            if (d.has("share")) action(actions, "upload_file", "Send files", [id] {
                if (open) open("files phone " + id);
            });
        }
        if (!any) note(live, "No phone nearby. Open KDE Connect on your phone, on the same network as this computer.");
    };
    live->watch(KdeConnect::get().changed);
    live->watch(Config::get().changed);
    live->sync();
    settingsLink(card, popup, "Connected devices", "bluetooth");
}

// ── Updates ──

void updates(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(10)->setAlignItems(Align::Stretch);
    live->sync = [live] {
        live->clearChildren();
        auto& p = Packages::get();
        auto& fw = Firmware::get();
        auto* head = kit::header(live, "system_update_alt", "Updates");
        kit::iconButton(head, "refresh", [] {
            Packages::get().check();
            if (Firmware::get().installed() && Packages::includesFirmware()) Firmware::get().check(true);
        });
        const int n = p.pending();
        std::string status = p.busy() ? p.status()
                           : p.checking() ? std::string("Checking…")
                           : !p.error().empty() ? p.error()
                           : n ? std::format("{} update{} · {}", n, n == 1 ? "" : "s", ago(p.lastChecked()))
                               : "Up to date · " + ago(p.lastChecked());
        note(live, status)->setColor(p.error().empty() ? "on_surface_variant" : "error", false);
        if (p.restart().any()) {
            auto* r = live->add<Flex>(Flex::Direction::Row);
            r->setPadding({14, 10})->setSpacing(10)->setRadius(Radii(18))->setBackground("tertiary_container", false);
            r->add<Icon>("restart_alt", 20)->setColor("on_tertiary_container", false);
            r->add<Label>(p.restart().system ? "Restart your computer to finish" : p.restart().session ? "Log out and back in to finish"
                                                                                                      : "Restart the shell to finish")
                ->setSize(13)
                ->setColor("on_tertiary_container", false);
        }
        // What's waiting, by source
        const auto& list = p.updates();
        if (!list.empty()) {
            auto* box = live->add<ScrollView>();
            box->maxHeight = 260;
            auto* rows = box->add<Flex>(Flex::Direction::Column);
            rows->setAlignItems(Align::Stretch)->setSpacing(2);
            for (const auto& u : list) {
                auto* r = rows->add<Flex>(Flex::Direction::Row);
                r->setPadding({8, 6})->setSpacing(10);
                const char* glyph = u.source == Packages::Source::Shell ? "auto_awesome"
                                  : u.source == Packages::Source::Aur ? "package_2"
                                  : u.source == Packages::Source::Flatpak ? "apps" : "deployed_code";
                r->add<Icon>(glyph, 18)->setColor(u.source == Packages::Source::Shell ? "primary" : "on_surface_variant", false);
                auto* name = r->add<Label>(u.name);
                name->setSize(13)->setWeight(500)->setMaxWidth(170);
                name->grow = true;
                r->add<Label>(u.from.empty() ? u.to : u.from + " → " + u.to)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(150);
            }
        }
        if (fw.installed() && Packages::includesFirmware() && fw.updateCount() > 0)
            note(live, std::format("{} firmware update{} (installed from Settings → Firmware)", fw.updateCount(), fw.updateCount() == 1 ? "" : "s"));
        if (!list.empty() && !p.busy()) {
            auto* row = live->add<Flex>(Flex::Direction::Row);
            row->setJustify(Align::End);
            pillButton(row, "Update now", true, [] { Packages::get().upgrade(); }, "download");
        }
    };
    live->watch(Packages::get().changed);
    live->watch(Firmware::get().changed);
    live->sync();
    settingsLink(card, popup, "Software & updates", "packages");
}

// ── Weather ──

void weather(Flex* card, Popup& popup) {
    dashboardWidget(card, "weather");
    // Rain on the way (or here), from the 15-minute forecast
    auto* rain = card->add<Live>();
    rain->sync = [rain] {
        rain->clearChildren();
        const int in = Weather::get().rainIn();
        if (in < 0) return;
        auto* r = rain->add<Flex>(Flex::Direction::Row);
        r->setPadding({14, 10})->setSpacing(10)->setRadius(Radii(18))->setBackground("secondary_container", false);
        r->add<Icon>("umbrella", 20)->setColor("on_secondary_container", false);
        r->add<Label>(in == 0 ? std::string("Raining now") : std::format("Rain in about {} minutes", std::max(5, (in + 4) / 5 * 5)))
            ->setSize(14)
            ->setWeight(500)
            ->setColor("on_secondary_container", false);
    };
    rain->watch(Weather::get().changed);
    rain->sync();
    settingsLink(card, popup, "Weather settings", "weather");
}

// ── Digital Wellbeing ──

void wellbeing(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(10)->setAlignItems(Align::Stretch);
    live->sync = [live] {
        live->clearChildren();
        auto& w = Wellbeing::get();
        kit::header(live, "digital_wellbeing", "Digital Wellbeing");
        const auto& today = w.today();
        auto* total = live->add<Label>(hm(today.total()));
        total->setSize(32)->setWeight(500)->setColor("primary", false);
        note(live, std::format("Screen time today · {} unlock{}", today.unlocks, today.unlocks == 1 ? "" : "s"));
        // The apps used most, with a bar for each
        std::vector<std::pair<std::string, int>> apps(today.seconds.begin(), today.seconds.end());
        std::ranges::sort(apps, std::greater{}, &std::pair<std::string, int>::second);
        const int most = apps.empty() ? 1 : std::max(1, apps.front().second);
        for (size_t i = 0; i < apps.size() && i < 4; ++i) {
            const auto& [cls, secs] = apps[i];
            auto* r = live->add<Flex>(Flex::Direction::Row);
            r->setSpacing(10);
            const std::string icon = Wellbeing::iconOf(cls);
            if (!icon.empty() && !Images::get().findIcon(icon, 48).empty()) {
                r->add<ImageView>(22, [icon](float scale, int& iw, int& ih) -> GLuint {
                    auto* t = Images::get().icon(icon, static_cast<int>(std::lround(22 * scale)));
                    if (!t) return 0;
                    iw = t->width;
                    ih = t->height;
                    return t->id;
                });
            } else {
                r->add<Icon>("apps", 20)->setColor("on_surface_variant", false);
            }
            auto* col = r->add<Flex>(Flex::Direction::Column);
            col->setSpacing(3)->setAlignItems(Align::Stretch);
            col->grow = true;
            auto* line = col->add<Flex>(Flex::Direction::Row);
            auto* name = line->add<Label>(Wellbeing::nameOf(cls));
            name->setSize(13)->setWeight(500)->setMaxWidth(190);
            name->grow = true;
            line->add<Label>(hm(secs))->setSize(12)->setColor("on_surface_variant", false);
            auto* track = col->add<Flex>(Flex::Direction::Row);
            track->fixedHeight = 6;
            track->setRadius(Radii(3))->setBackground("surface_container_highest", false);
            auto* fill = track->add<Flex>(Flex::Direction::Row);
            fill->fixedHeight = 6;
            fill->fixedWidth = std::max(6.0f, 240.0f * static_cast<float>(secs) / static_cast<float>(most));
            fill->setRadius(Radii(3))->setBackground("primary", false);
        }
        // Focus mode and Bedtime, switched here
        auto toggle = [live](const char* glyph, const char* title, const char* sub, bool on, std::function<void(bool)> fn) {
            auto* r = live->add<Flex>(Flex::Direction::Row);
            r->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(18))->setBackground("surface_container_high", false);
            r->add<Icon>(glyph, 20)->setColor("on_surface_variant", false);
            auto* t = r->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start)->setSpacing(0);
            t->grow = true;
            t->add<Label>(title)->setSize(14)->setWeight(500);
            t->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(220);
            auto* sw = r->add<Switch>();
            sw->setChecked(on, false);
            sw->onToggled = std::move(fn);
        };
        toggle("self_improvement", "Focus mode", "Pauses distracting apps", w.focus(), [](bool on) { Wellbeing::get().setFocus(on); });
        toggle("bedtime", "Bedtime", "Greyscale and Do not disturb", w.bedtime(), [](bool on) { Wellbeing::get().setBedtimeNow(on); });
    };
    live->watch(Wellbeing::get().changed);
    live->sync();
    settingsLink(card, popup, "Digital Wellbeing", "wellbeing");
}

// ── Printers ──

void printers(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(10)->setAlignItems(Align::Stretch);
    live->sync = [live] {
        live->clearChildren();
        auto& p = Printers::get();
        kit::header(live, "print", "Printers");
        note(live, p.summary());
        if (!p.installed() || !p.running()) return;
        for (const auto& pr : p.printers()) {
            if (!pr.isDefault) continue;
            auto* r = live->add<Flex>(Flex::Direction::Row);
            r->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(18))->setBackground("surface_container_high", false);
            r->add<Icon>("print", 22)->setColor("primary", false);
            auto* t = r->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start)->setSpacing(0);
            t->grow = true;
            t->add<Label>(pr.description.empty() ? pr.name : pr.description)->setSize(14)->setWeight(500)->setMaxWidth(250);
            t->add<Label>(std::string("Default · ") + (pr.state == "printing" ? "Printing" : pr.state == "stopped" ? "Paused" : "Ready"))
                ->setSize(12)
                ->setColor("on_surface_variant", false);
        }
        const auto& jobs = p.jobs();
        if (jobs.empty()) return;
        kit::caption(live, std::format("Printing · {} waiting", jobs.size()));
        for (const auto& j : jobs) {
            auto* r = live->add<Flex>(Flex::Direction::Row);
            r->setPadding({8, 4})->setSpacing(10);
            r->add<Icon>("description", 18)->setColor("on_surface_variant", false);
            auto* t = r->add<Label>(j.title.empty() ? "Document" : j.title);
            t->setSize(13)->setMaxWidth(230);
            t->grow = true;
            const std::string id = j.id;
            kit::iconButton(r, "close", [id] { Printers::get().cancel(id); }, 18);
        }
        if (jobs.size() > 1) pillButton(live, "Cancel all", false, [] { Printers::get().cancelAll(); }, "cancel");
    };
    live->watch(Printers::get().changed);
    live->sync();
    settingsLink(card, popup, "Printer settings", "printers");
}

// ── Firewall ──

void firewall(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(10)->setAlignItems(Align::Stretch);
    live->sync = [live] {
        live->clearChildren();
        auto& f = Firewall::get();
        auto* head = kit::header(live, f.installed() && f.enabled() ? "shield_lock" : "remove_moderator", "Firewall");
        if (!f.installed()) {
            note(live, "No firewall is installed. Settings → Firewall sets one up.");
            return;
        }
        auto* sw = headerSwitch(head);
        sw->setChecked(f.enabled(), false);
        sw->onToggled = [](bool on) { Firewall::get().setEnabled(on); };
        note(live, !f.enabled() ? "Off: anything on the network can reach this computer's open ports"
                 : f.blocksIncoming() ? std::format("On: connections in are blocked, except {} rule{} you allowed", f.rules().size(),
                                                    f.rules().size() == 1 ? "" : "s")
                                      : "On, letting connections in (Settings → Firewall can block them)");
    };
    live->watch(Firewall::get().changed);
    live->sync();
    settingsLink(card, popup, "Firewall settings", "firewall");
}

// ── VPN ──

void vpn(Flex* card, Popup& popup) {
    auto* live = card->add<Live>();
    live->setSpacing(8)->setAlignItems(Align::Stretch);
    live->sync = [live] {
        live->clearChildren();
        auto& v = Vpn::get();
        kit::header(live, v.anyActive() ? "vpn_lock" : "vpn_key", "VPN");
        auto entry = [live](const std::string& name, const std::string& sub, bool on, std::function<void(bool)> fn) {
            auto* r = live->add<Flex>(Flex::Direction::Row);
            r->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(18))->setBackground(on ? "secondary_container" : "surface_container_high", false);
            auto* t = r->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start)->setSpacing(0);
            t->grow = true;
            t->add<Label>(name)->setSize(14)->setWeight(500)->setMaxWidth(230)->setColor(on ? "on_secondary_container" : "on_surface", false);
            t->add<Label>(sub)->setSize(12)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
            auto* sw = r->add<Switch>();
            sw->setChecked(on, false);
            sw->onToggled = std::move(fn);
        };
        for (const auto& c : v.connections()) {
            const std::string uuid = c.uuid;
            entry(c.name, c.activating ? "Connecting…" : c.active ? "Connected" : c.type == "wireguard" ? "WireGuard" : "OpenVPN",
                  c.active || c.activating, [uuid](bool on) { on ? Vpn::get().up(uuid) : Vpn::get().down(uuid); });
        }
        using W = Vpn::Warp;
        if (v.warp() == W::Connected || v.warp() == W::Disconnected || v.warp() == W::Connecting)
            entry("Cloudflare WARP", v.warp() == W::Connecting ? "Connecting…" : v.warp() == W::Connected ? "Connected" : "Off",
                  v.warp() != W::Disconnected, [](bool on) { Vpn::get().warpConnect(on); });
        if (v.connections().empty() && v.warp() == W::Absent) note(live, "No VPN set up yet: Settings → VPN imports one");
    };
    live->watch(Vpn::get().changed);
    live->sync();
    settingsLink(card, popup, "VPN settings", "vpn");
}

} // namespace m3::shell::panels
