// The Settings app: sidebar navigation and pages. Settings are read from and
// written to config.json (shared with the QML shell) through Config.
#include "build_config.h"
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Audio.h"
#include "services/Backups.h"
#include "services/Calendar.h"
#include "quickshare/QuickShare.h"
#include "services/Clipboard.h"
#include "services/HyprOptions.h"
#include "services/Hyprland.h"
#include "services/Keybinds.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Packages.h"
#include "services/Search.h"
#include "services/Session.h"
#include "services/SystemStats.h"
#include "services/Wallpaper.h"
#include "services/Weather.h"
#include "shell/Dock.h"
#include "shell/HyprPages.h"
#include "shell/PrintersPage.h"
#include "shell/SecurityPages.h"
#include "shell/WellbeingPage.h"
#include "shell/KeyCapture.h"
#include "shell/LockLayout.h"
#include "shell/PanelKit.h"
#include "shell/SettingsKit.h"
#include "shell/Panels.h"
#include "shell/bar/Bar.h"
#include "shell/bar/Widgets.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cmath>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <sys/statvfs.h>
#include <pwd.h>
#include <grp.h>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// ── Building blocks ──
//
// M3 Expressive: lists are segmented (each row its own surface, a hairline
// apart, large outer and small inner corners), choices are connected button
// groups, and pressed controls spring into a tighter shape.

// Rebuilds a page by id (after a reset, so its controls show the defaults)
// One for each app: [0] Settings, [1] Hyprland Settings
std::function<void(const std::string&)> reshow[2];
// Types into the open Settings' search (`msg settings search:<words>`)
std::function<void(const std::string&)> searchFor[2];
// Rebuilds a page in whichever app shows it
void reshowPage(const std::string& id) {
    const bool hypr = id.starts_with("hypr-");
    if (reshow[hypr]) reshow[hypr](id);
}

// Config-bound controls

void switchRow(Node* g, const std::string& icon, const std::string& title, const std::string& sub,
               const std::string& pointer, bool fallback, std::function<void(bool)> after = {}) {
    auto r = row(g, icon, title, sub);
    auto* sw = r.row->add<Switch>();
    sw->setChecked(Config::get().value<bool>(pointer, fallback), false);
    sw->onToggled = [pointer, after](bool on) {
        Config::get().set(pointer, on);
        if (after) after(on);
    };
}

template <typename T>
void choiceRow(Node* g, const std::string& icon, const std::string& title, const std::string& sub, const std::string& pointer,
               T fallback, std::vector<std::pair<T, std::string>> options, std::function<void(T)> after = {}) {
    auto r = row(g, icon, title, sub);
    connectedGroup<T>(r.row, Config::get().value<T>(pointer, fallback), std::move(options), [pointer, after](T v) {
        Config::get().set(pointer, v);
        if (after) after(v);
    });
}

void sliderRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer, double fallback,
               double min, double max, double step, const std::function<std::string(double)>& format,
               std::function<void(double)> after = {}) {
    auto r = row(g, icon, title, format(Config::get().value<double>(pointer, fallback)));
    auto* sl = r.row->add<Slider>();
    sl->fixedWidth = 240;
    sl->setRange(static_cast<float>(min), static_cast<float>(max))->setStep(static_cast<float>(step / (max - min)));
    sl->setValue(static_cast<float>(Config::get().value<double>(pointer, fallback)), false);
    auto* sub = r.subtitle;
    sub->setVisible(true);
    sl->onChanged = [sub, format, step](float v) { sub->setText(format(std::round(v / step) * step)); };
    sl->onCommitted = [pointer, step, after](float v) {
        const double snapped = std::round(v / step) * step;
        Config::get().set(pointer, snapped);
        if (after) after(snapped);
    };
}

void textRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer, const std::string& fallback) {
    auto r = row(g, icon, title, "Enter to apply");
    auto* f = r.row->add<TextField>();
    f->fixedWidth = 280;
    f->setText(Config::get().value<std::string>(pointer, fallback));
    f->onSubmit = [pointer](const std::string& t) { Config::get().set(pointer, t); };
}

// A setting Hyprland owns: kept in Hyprland Settings only (no copy here to
// disagree with it), and this row takes you there
void hyprLink(Node* g, const std::string& icon, const std::string& title, const std::string& sub, const std::string& hyprPage) {
    auto r = row(g, icon, title, sub);
    pillButton(r.row, "Hyprland Settings", false, [hyprPage] {
        if (open) open("hyprsettings " + hyprPage);
    }, "open_in_new");
}

// Where one clock keeps its settings (the island, the bar's Clock widget,
// the desktop)
struct ClockKeys {
    std::string time, timeDefault, show;
    bool showDefault;
    std::string date, dateDefault;
};

// A clock's time and date as plain choices: 24- or 12-hour, seconds, the
// date and its format (shown only with the date on), each as it reads now.
// Changes go to every target (linked clocks); the first is read.
void timeDateRows(Node* g, const std::vector<ClockKeys>& targets) {
    if (targets.empty()) return;
    const auto& first = targets.front();
    auto& c = Config::get();
    const std::string fmt = c.value<std::string>(first.time, first.timeDefault);
    struct State {
        bool twelve, seconds;
    };
    auto st = std::make_shared<State>(State{fmt.find("AP") != std::string::npos || fmt.find("ap") != std::string::npos,
                                            fmt.find('s') != std::string::npos});
    auto writeTime = [st, targets] {
        const std::string f = std::string(st->twelve ? "h:mm" : "hh:mm") + (st->seconds ? ":ss" : "") + (st->twelve ? " AP" : "");
        for (const auto& t : targets) Config::get().set(t.time, f);
    };
    auto hours = row(g, "schedule", "Time");
    connectedGroup<bool>(hours.row, st->twelve, {{false, Clock::sample("hh:mm") + "  24-hour"}, {true, Clock::sample("h:mm AP") + "  12-hour"}},
                         [st, writeTime](bool v) {
                             st->twelve = v;
                             writeTime();
                         });
    auto secs = row(g, "timer", "Seconds");
    auto* sw = secs.row->add<Switch>();
    sw->setChecked(st->seconds, false);
    sw->onToggled = [st, writeTime](bool on) {
        st->seconds = on;
        writeTime();
    };

    const bool showing = c.value<bool>(first.show, first.showDefault);
    auto show = row(g, "calendar_today", "Show the date");
    auto* dsw = show.row->add<Switch>();
    dsw->setChecked(showing, false);
    std::vector<std::pair<std::string, std::string>> dates;
    for (const char* f : {"ddd d MMM", "d MMM", "dddd, d MMMM", "dd/MM/yyyy", "MM/dd/yyyy", "yyyy-MM-dd"})
        dates.push_back({f, Clock::sample(f)});
    auto date = row(g, "event", "Date format");
    date.row->setVisible(showing);
    dropdown(date.row, g, Clock::sample(c.value<std::string>(first.date, first.dateDefault)), dates, [targets](const std::string& v) {
        for (const auto& t : targets) Config::get().set(t.date, v);
    });
    dsw->onToggled = [targets, dateRow = date.row](bool on) {
        for (const auto& t : targets) Config::get().set(t.show, on);
        dateRow->setVisible(on);
    };
}

std::string percent(double v) {
    return std::format("{:.0f}%", v * 100);
}

std::string readFirstLine(const std::string& path, const std::string& prefix = {}) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
        if (prefix.empty() || line.starts_with(prefix)) return prefix.empty() ? line : line.substr(prefix.size());
    return {};
}

// Re-run Matugen (after scheme, mode or accent changes)
void recolour() {
    Wallpaper::get().recolour();
}

// ── Pages ──

// The current connection's speeds and details, inside its own entry (the
// connected Wi-Fi network, or the wired adapter in use). `fg` is the text
// colour on that entry's fill; `disconnect` adds a button for it.
void connectionDetails(Flex* parent, const char* fg, std::function<void()> disconnect) {
    auto* live = parent->add<Live>();
    live->setSpacing(12)->setPadding({0, 6, 0, 2});
    live->setAlignItems(Align::Stretch);

    // Live speeds on tonal tiles
    auto* tiles = live->add<Flex>(Flex::Direction::Row);
    tiles->setSpacing(8)->setAlignItems(Align::Stretch);
    auto stat = [tiles](const char* icon, const char* title, const char* bg, const char* tfg) {
        auto* t = tiles->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(2)->setPadding({16, 12})->setRadius(Radii(20))->setBackground(bg, false);
        t->grow = true;
        auto* top = t->add<Flex>(Flex::Direction::Row);
        top->setSpacing(6);
        top->add<Icon>(icon, 18)->setColor(tfg, false);
        top->add<Label>(title)->setSize(12)->setWeight(500)->setColor(tfg, false);
        auto* v = t->add<Label>("–");
        v->setSize(24)->setWeight(600)->setColor(tfg, false);
        return v;
    };
    auto* down = stat("arrow_downward", "Download", "primary_container", "on_primary_container");
    auto* up = stat("arrow_upward", "Upload", "tertiary_container", "on_tertiary_container");
    auto* linkTile = stat("speed", "Link speed", "surface_container_high", "on_surface");

    // Then a compact two-column list: what, and its value
    auto* list = live->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2)->setPadding({4, 0});
    struct Detail {
        Flex* row;
        Label* value;
    };
    auto detail = [list, fg](const char* icon, const char* title) {
        auto* r = list->add<Flex>(Flex::Direction::Row);
        r->setSpacing(14)->setPadding({0, 7});
        r->add<Icon>(icon, 18)->setColor(fg, false);
        auto* t = r->add<Label>(title);
        t->setSize(14)->setColor(fg, false);
        t->grow = true;
        auto* v = r->add<Label>();
        v->setSize(14)->setWeight(500)->setColor(fg, false)->setMaxWidth(320);
        return Detail{r, v};
    };
    auto band = detail("radio", "Band");
    auto security = detail("shield_lock", "Security");
    auto ipv4 = detail("lan", "IPv4 address");
    auto gateway = detail("router", "Gateway");
    auto dns = detail("dns", "DNS");
    auto ipv6 = detail("public", "IPv6 address");
    auto mac = detail("fingerprint", "Hardware address");
    auto iface = detail("settings_ethernet", "Interface");
    auto used = detail("data_usage", "Data used since it came up");

    if (disconnect) {
        auto* actions = live->add<Flex>(Flex::Direction::Row);
        actions->setJustify(Align::End);
        pillButton(actions, "Disconnect", false, disconnect, "link_off");
    }

    auto lease = Network::get().watchTraffic();
    live->sync = [=] {
        (void)lease; // sampling runs while the details are shown
        auto& n = Network::get();
        const auto& d = n.details();
        down->setText(formatRate(d.rxRate));
        up->setText(formatRate(d.txRate));
        linkTile->setText(d.linkRate > 0 ? std::format("{} Mb/s", d.linkRate) : "–");
        auto set = [](const Detail& x, const std::string& v) {
            x.value->setText(v);
            x.row->setVisible(!v.empty());
        };
        std::string rate = d.band();
        if (d.maxRate > 0) rate += (rate.empty() ? "" : " · ") + std::format("up to {} Mb/s", d.maxRate);
        set(band, n.kind() == Network::Kind::Wifi ? rate : "");
        set(security, d.security);
        set(ipv4, d.ipv4);
        set(gateway, d.gateway);
        set(dns, d.dns);
        set(ipv6, d.ipv6);
        set(mac, d.mac);
        set(iface, d.iface);
        set(used, d.iface.empty() ? "" : std::format("↓ {}  ·  ↑ {}", formatBytes(d.rxTotal), formatBytes(d.txTotal)));
    };
    live->watch(Network::get().detailsChanged);
    live->sync();
}

void networkPage(Flex* page, Popup&) {
    // Wi-Fi first, then wired adapters; the connection in use carries its
    // speeds and details inside its own entry
    wifiView(panelGroup(page, "Wi-Fi networks", "Tap one to connect; saved networks connect at once"), {}, false,
             [](Flex* entry) { connectionDetails(entry, "on_secondary_container", [] { Network::get().disconnect(); }); });

    // Wired adapters: their state, and a button to connect or disconnect
    auto* wiredLive = page->add<Live>();
    wiredLive->setSpacing(6);
    auto wiredOpen = std::make_shared<bool>(false); // the in-use adapter's dropdown
    wiredLive->sync = [wiredLive, wiredOpen] {
        wiredLive->clearChildren();
        auto* g = group(wiredLive, "Ethernet");
        const auto& all = Network::get().wired();
        if (all.empty()) {
            auto r = row(g, "settings_ethernet", "No Ethernet adapter", "Plug in a network cable, or a USB Ethernet adapter");
            r.title->setColor("on_surface_variant", false);
            return;
        }
        auto& n = Network::get();
        for (const auto& w : all) {
            std::string state = !w.cable() ? "Cable unplugged" : w.connecting() ? "Connecting…" : !w.connected() ? "Disconnected" : "Connected";
            if (w.connected() && w.speed > 0) state += std::format("  ·  {} Mb/s", w.speed);
            if (w.connected() && !w.ipv4.empty()) state += "  ·  " + w.ipv4;
            // The adapter in use holds the connection's details, as Wi-Fi does
            const bool inUse = w.connected() && n.kind() == Network::Kind::Ethernet && n.details().iface == w.iface;
            Flex* holder = g;
            if (inUse) {
                holder = g->add<Flex>(Flex::Direction::Column);
                holder->setAlignItems(Align::Stretch)->setPadding({0, 0, 0, 12})->setRadius(Radii(16))
                    ->setBackground("secondary_container", false);
            }
            auto r = row(holder, "lan", "Ethernet (" + w.iface + ")", state);
            const std::string path = w.path;
            if (w.connected() || w.connecting())
                pillButton(r.row, "Disconnect", false, [path] { Network::get().disconnectWired(path); });
            else if (w.cable())
                pillButton(r.row, "Connect", true, [path] { Network::get().connectWired(path); });
            if (inUse)
                iconButton(r.row, *wiredOpen ? "expand_less" : "expand_more", [wiredLive, wiredOpen] {
                    *wiredOpen = !*wiredOpen;
                    wiredLive->syncSoon();
                });
            if (inUse && *wiredOpen) {
                auto* details = holder->add<Flex>(Flex::Direction::Column);
                details->setAlignItems(Align::Stretch)->setPadding({16, 0});
                connectionDetails(details, "on_secondary_container", {});
            }
        }
    };
    wiredLive->watch(Network::get().changed);
    wiredLive->sync();
}

// Quick Share, the shell's own: phones nearby send files, links and text
// over the network, each transfer asked first with its PIN
void quickShareRows(Flex* page) {
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->sync = [live] {
        live->clearChildren();
        auto& qs = quickshare::QuickShare::get();
        auto* g = group(live, "Quick Share", "Share files with Android phones and Chromebooks on the same Wi-Fi, both ways");
        const bool on = Config::get().value<bool>("/quickshare/receive", true);
        auto vis = row(g, quickshare::QuickShare::symbol(), "Let devices nearby share with you",
                       !on ? "Off" : qs.receiving() ? "Visible as \"" + qs.deviceName() + "\": each transfer is asked first"
                           : qs.status().empty() ? "Starting…" : qs.status());
        auto* sw = vis.row->add<Switch>();
        sw->setChecked(on, false);
        sw->onToggled = [](bool v) { Config::get().set("/quickshare/receive", v); };
        // A firewall dropping phones' connections: they see this computer,
        // then fail to send
        if (on && qs.firewallBlocks()) {
            auto fw = row(g, "shield", "The firewall blocks phones sending to you",
                          std::format("{} drops connections to Quick Share's port ({}): allow it once", qs.firewall(), qs.port()));
            fw.title->setColor("error", false);
            fw.subtitle->setMaxWidth(400)->setMaxLines(2);
            pillButton(fw.row, "Allow", true, [] { quickshare::QuickShare::get().allowThroughFirewall(); }, "shield");
        }
        auto nameRow = row(g, "badge", "Name shown to others", "Enter to save");
        auto* name = nameRow.row->add<TextField>();
        name->fixedWidth = 240;
        name->setText(qs.deviceName());
        name->onSubmit = [](const std::string& v) { Config::get().set("/quickshare/name", v); };
        row(g, "folder", "Files go to Downloads", "Links and text go to the clipboard");
        auto send = row(g, "send", "Send files", "To a phone nearby with Quick Share open");
        pillButton(send.row, "Send", true, [] {
            if (open) open("quickshare-send ");
        }, "send");
        // The last few transfers
        if (!qs.transfers().empty()) {
            auto* recent = group(live, "Recently");
            static const char* states[] = {"Connecting", "Waiting for you", "Receiving", "Received", "Declined", "Stopped"};
            static const char* sending[] = {"Connecting", "Waiting for them", "Sending", "Sent", "Declined", "Not sent"};
            for (auto it = qs.transfers().rbegin(); it != qs.transfers().rend(); ++it) {
                const auto& t = it->second;
                std::string what = t.files.size() == 1 ? t.files.front().name
                                 : !t.files.empty()     ? std::format("{} files", t.files.size())
                                 : !t.texts.empty()     ? std::string("Text")
                                                        : std::string("");
                auto r = row(recent, t.outgoing ? "send" : t.state == quickshare::QuickShare::State::Done ? "download_done" : quickshare::QuickShare::symbol(),
                             t.sender.empty() ? "A device nearby" : t.sender,
                             std::string((t.outgoing ? sending : states)[static_cast<int>(t.state)]) + (what.empty() ? "" : " · " + what) + (t.error.empty() ? "" : " · " + t.error));
                const int id = t.id;
                pillButton(r.row, "Open", false, [id] {
                    if (open) open("quickshare " + std::to_string(id));
                });
            }
        }
    };
    live->watch(quickshare::QuickShare::get().changed);
    live->sync();
}

void bluetoothPage(Flex* page, Popup&) {
    quickShareRows(page);
    auto* live = page->add<Live>();
    auto* g = group(live, "Bluetooth");
    auto power = row(g, "bluetooth", "Bluetooth");
    auto* powerSw = power.row->add<Switch>();
    powerSw->onToggled = [](bool on) { Bluetooth::get().setPowered(on); };
    auto search = row(g, "bluetooth_searching", "Find devices", "Searches while this page is open");
    pillButton(search.row, "Search again", false, [] { Bluetooth::get().setDiscovering(true); }, "refresh");
    // The pairing question and errors, above the lists
    auto* prompt = live->add<Flex>(Flex::Direction::Column);
    prompt->setAlignItems(Align::Stretch);
    auto* error = live->add<Label>();
    error->setSize(13)->setColor("error", false)->setMaxWidth(640)->setMaxLines(2);
    auto* lists = live->add<Flex>(Flex::Direction::Column);
    lists->setAlignItems(Align::Stretch)->setSpacing(6);

    auto leases = std::make_shared<std::pair<Bluetooth::Lease, Bluetooth::Lease>>(Bluetooth::get().scan(), Bluetooth::get().showRequests());
    live->sync = [=] {
        (void)leases;
        auto& b = Bluetooth::get();
        powerSw->setChecked(b.powered());
        power.subtitle->setText(!b.available() ? "No Bluetooth adapter" : b.powered() ? "On" : "Off");
        power.subtitle->setVisible(true);
        search.subtitle->setText(b.discovering() ? "Searching… put the device in pairing mode" : "Searches while this page is open");
        prompt->clearChildren();
        bluetoothPrompt(prompt);
        error->setText(b.lastError());
        error->setVisible(!b.lastError().empty());
        lists->clearChildren();
        if (!b.powered()) return;

        auto add = [](Node* list, const Bluetooth::Device& d) {
            const std::string busy = Bluetooth::get().busy(d.path);
            auto r = row(list, bluetoothGlyph(d.icon), d.name,
                         !busy.empty() ? busy : d.connected ? "Connected" : d.paired ? "Paired" : "Available");
            const std::string path = d.path;
            if (!busy.empty()) {
                if (busy == "Pairing…") pillButton(r.row, "Cancel", false, [] { Bluetooth::get().cancelPairing(); });
                return;
            }
            if (d.connected) pillButton(r.row, "Disconnect", false, [path] { Bluetooth::get().disconnect(path); });
            else if (d.paired) pillButton(r.row, "Connect", true, [path] { Bluetooth::get().connect(path); });
            else pillButton(r.row, "Pair", true, [path] { Bluetooth::get().pair(path); }, "link");
            if (d.paired) confirmButton(r.row, "bt:" + path, "delete", [path] { Bluetooth::get().forget(path); });
        };
        Node* mine = nullptr;
        for (const auto& d : b.devices())
            if (d.paired) add(mine ? mine : (mine = group(lists, "My devices")), d);
        auto* nearby = group(lists, "Nearby devices", "Devices in pairing mode show up here; unnamed ones are hidden");
        bool any = false;
        for (const auto& d : b.devices())
            if (!d.paired && d.named) {
                any = true;
                add(nearby, d);
            }
        if (!any) {
            auto r = row(nearby, "", b.discovering() ? "Searching…" : "Nothing found yet");
            r.title->setColor("on_surface_variant", false);
        }
    };
    live->watch(Bluetooth::get().changed);
    live->sync();
}

void soundPage(Flex* page, Popup& popup) {
    audio(panelGroup(page, "Output and input", "Volume, balance, channels and devices"), popup);
}

void displayPage(Flex* page, Popup&) {
    auto* g = group(page, "Brightness");
    if (Brightness::get().available()) {
        auto r = row(g, "brightness_medium", "Screen brightness");
        auto* sl = r.row->add<Slider>();
        sl->fixedWidth = 240;
        sl->setValue(Brightness::get().value(), false);
        sl->onChanged = [](float v) { Brightness::get().set(v); };
    } else {
        row(g, "brightness_medium", "No adjustable backlight", "External monitors are controlled from their own menus");
    }

    auto* night = group(page, "Night light", "Warmer colours in the evening, via hyprsunset");
    auto applyNight = [] {
        if (Config::get().value<bool>("/nightLight/enabled", false))
            spawn(std::format("pkill -x hyprsunset; hyprsunset -t {}", Config::get().value<int>("/nightLight/temperature", 4500)));
        else
            spawn("pkill -x hyprsunset");
    };
    switchRow(night, "nightlight", "Night light", "", "/nightLight/enabled", false, [applyNight](bool) { applyNight(); });
    sliderRow(night, "thermostat", "Colour temperature", "/nightLight/temperature", 4500, 2500, 6500, 100,
              [](double v) { return std::format("{:.0f} K", v); }, [applyNight](double) { applyNight(); });

    auto* mons = group(page, "Monitors", "Arrange them and set their resolution, scale, rotation, VRR and HDR in Hyprland Settings");
    if (auto j = Hyprland::get().requestJson("monitors"); j && j->is_array()) {
        for (const auto& m : *j)
            row(mons, "monitor", m.value("description", "").empty() ? m.value("name", "") : std::format("{}  ·  {}", m.value("name", ""), m.value("description", "")),
                std::format("{}×{} at {:.0f} Hz · scale {:.2f}{}", m.value("width", 0), m.value("height", 0),
                            m.value("refreshRate", 0.0), m.value("scale", 1.0), m.value("focused", false) ? " · focused" : ""));
    } else {
        row(mons, "monitor", "Monitor details need Hyprland");
    }
    auto arrange = row(mons, "desktop_windows", "Arrangement and modes");
    pillButton(arrange.row, "Open", false, [] {
        if (open) open("hyprsettings hypr-monitors");
    }, "open_in_new");
}

// Installed font families from fontconfig, read once (monospace or all)
void fontFamilies(bool mono, std::function<void(const std::vector<std::string>&)> done) {
    static std::map<bool, std::vector<std::string>> cache;
    if (auto it = cache.find(mono); it != cache.end()) return done(it->second);
    m3::run({"fc-list", "--format", "%{family[0]}\\n", mono ? ":spacing=mono" : ":"}, [mono, done](int, const std::string& out) {
        std::set<std::string> names;
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line))
            if (!line.empty() && !line.starts_with("Material Symbols")) names.insert(line);
        auto& list = cache[mono];
        list.assign(names.begin(), names.end());
        done(list);
    });
}

// A dropdown with a searchable list under it (fonts, time zones, languages).
// `load` hands the options over (it may take a while); `pick` applies one.
// With `preview`, each option is drawn in its own font.
struct PickerOptions {
    std::string searchHint = "Search";
    bool preview = false;
};

void pickerRow(Node* g, const std::string& icon, const std::string& title, std::function<std::string()> current,
               std::function<void(std::function<void(const std::vector<std::string>&)>)> load,
               std::function<void(const std::string&)> pick, PickerOptions opts = {}) {
    auto r = row(g, icon, title, "");
    auto* drop = r.row->add<Button>(Flex::Direction::Row);
    pill(drop->setBackground(ColorRef("on_surface", 0.1f), false))->setPadding({18, 10})->setSpacing(8);
    drop->pressedRadius = 10;
    auto* name = drop->add<Label>(current());
    name->setSize(14)->setMaxWidth(220);
    if (opts.preview) name->setFamily(current());
    auto* chevron = drop->add<Icon>("expand_more", 20);
    chevron->setColor("on_surface_variant", false);

    // The list, hidden until the button opens it
    auto* panel = g->add<Flex>(Flex::Direction::Column);
    panel->setPadding({20, 8, 20, 14})->setSpacing(8)->setAlignItems(Align::Stretch);
    panel->setVisible(false);
    auto* search = panel->add<TextField>();
    search->setPlaceholder(opts.searchHint);
    auto* scroll = panel->add<ScrollView>();
    scroll->maxHeight = 260;
    auto* list = scroll->add<Flex>(Flex::Direction::Column);
    list->setAlignItems(Align::Stretch)->setSpacing(2);

    auto fill = std::make_shared<std::function<void()>>();
    *fill = [=] {
        load([=](const std::vector<std::string>& all) {
            list->clearChildren();
            const std::string now = current();
            auto lower = [](std::string t) {
                for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return t;
            };
            const std::string q = lower(search->text());
            size_t shown = 0;
            for (const auto& option : all) {
                if (!q.empty() && lower(option).find(q) == std::string::npos) continue;
                // Keep the list short (font previews load each font): search finds the rest
                if (++shown > 60) break;
                auto* item = list->add<Button>(Flex::Direction::Row);
                pill(item)->setPadding({14, 8})->setSpacing(10);
                if (option == now) item->setBackground("secondary_container", false);
                auto* l = item->add<Label>(option);
                l->setSize(15)->setMaxWidth(560);
                if (opts.preview) l->setFamily(option);
                l->grow = true;
                if (option == now) item->add<Icon>("check", 18)->setColor("on_secondary_container", false);
                item->onClicked = [=](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    pick(option);
                    name->setText(option);
                    if (opts.preview) name->setFamily(option);
                    panel->setVisible(false);
                    chevron->setText("expand_more");
                };
            }
            if (shown == 0) list->add<Label>("Nothing matches")->setSize(13)->setColor("on_surface_variant", false);
            scroll->scrollTo(0, false);
        });
    };
    search->onChanged = [fill](const std::string&) {
        EventLoop::get().defer([fill] { (*fill)(); });
    };
    drop->onClicked = [panel, chevron, fill, search](uint32_t button) {
        if (button != BTN_LEFT) return;
        const bool show = !panel->shown();
        panel->setVisible(show);
        chevron->setText(show ? "expand_less" : "expand_more");
        if (show) {
            (*fill)();
            search->focus();
        }
    };
}

// A dropdown of installed fonts, each previewed in its own face
void fontRow(Node* g, const std::string& icon, const std::string& title, const std::string& pointer,
             const std::string& fallback, bool mono) {
    pickerRow(
        g, icon, title, [pointer, fallback] { return Config::get().value<std::string>(pointer, fallback); },
        [mono](auto done) { fontFamilies(mono, done); },
        [pointer](const std::string& family) { Config::get().set(pointer, family); }, {"Search fonts", true});
}

// Matugen's templates, as apps: each themed with the wallpaper's colours
// unless switched off (it then keeps the colours it last had)
void appThemingRows(Flex* page) {
    struct App {
        std::vector<std::string> templates; // one switch for all of them
        const char* icon;
        const char* name;
        const char* about;
        std::vector<std::string> found; // a command or a path (~/…) that means it's here
    };
    const std::vector<App> known = {
        {{"hyprland"}, "space_dashboard", "Hyprland", "Window borders and group colours", {}},
        {{"hyprlock"}, "lock", "hyprlock", "The fallback lock screen", {"hyprlock"}},
        {{"gtk3", "gtk4"}, "widgets", "GTK apps", "GNOME and other GTK 3 and 4 apps", {}},
        {{"kde_colors"}, "apps", "Qt and KDE apps", "Through the system colour scheme", {}},
        {{"terminal"}, "terminal", "Terminals (live)", "Colours sent to open terminals", {}},
        {{"alacritty"}, "terminal", "Alacritty", "", {"alacritty"}},
        {{"kitty"}, "terminal", "kitty", "", {"kitty"}},
        {{"foot"}, "terminal", "foot", "", {"foot"}},
        {{"ghostty"}, "terminal", "Ghostty", "", {"ghostty"}},
        {{"wezterm"}, "terminal", "WezTerm", "", {"wezterm"}},
        {{"fuzzel"}, "search", "fuzzel", "", {"fuzzel"}},
        {{"firefox_chrome", "firefox"}, "language", "Firefox, LibreWolf, Floorp, Zen", "On their next start", {"firefox", "librewolf", "floorp", "zen-browser"}},
        {{"vscode"}, "code", "VS Code, VSCodium, Cursor", "Pick \"Material3UI\" as the colour theme once", {"code", "codium", "code-oss", "cursor", "windsurf"}},
        {{"zeditor"}, "code", "Zed", "", {"zeditor", "zed"}},
        {{"discord"}, "forum", "Vesktop (Discord)", "", {"vesktop", "~/.config/vesktop"}},
        {{"obsidian"}, "edit_note", "Obsidian", "", {"obsidian"}},
        {{"zathura"}, "picture_as_pdf", "zathura", "", {"zathura"}},
        {{"cava"}, "graphic_eq", "cava", "", {"cava"}},
        {{"btop"}, "monitoring", "btop", "", {"btop"}},
    };
    auto installed = [](const std::vector<std::string>& found) {
        if (found.empty()) return true;
        std::error_code ec;
        for (const auto& f : found) {
            if (f.starts_with("~/")) {
                if (std::filesystem::exists(paths::home() + f.substr(1), ec)) return true;
                continue;
            }
            for (const char* dir : {"/usr/bin/", "/usr/local/bin/", "/opt/bin/"})
                if (access((dir + f).c_str(), X_OK) == 0) return true;
            if (access((paths::home() + "/.local/bin/" + f).c_str(), X_OK) == 0) return true;
        }
        return false;
    };
    const auto present = Wallpaper::get().templates();
    auto has = [&](const std::string& t) { return std::ranges::find(present, t) != present.end(); };
    auto* g = group(page, "App theming", "Material You colours for your apps, from the wallpaper. Off: the app keeps the colours it has now");
    std::vector<std::string> covered;
    auto addRow = [&](const std::vector<std::string>& names, const std::string& icon, const std::string& title, std::string about) {
        auto r = row(g, icon, title, about);
        auto* sw = r.row->add<Switch>();
        sw->setChecked(Wallpaper::templateOn(names.front()), false);
        sw->onToggled = [names](bool on) {
            for (const auto& n : names) Wallpaper::setTemplateOn(n, on);
        };
    };
    for (const auto& app : known) {
        std::vector<std::string> names;
        for (const auto& t : app.templates)
            if (has(t)) names.push_back(t);
        covered.insert(covered.end(), app.templates.begin(), app.templates.end());
        if (names.empty()) continue;
        const bool here = installed(app.found);
        std::string about = app.about;
        if (!here) about = std::string("Not installed") + (about.empty() ? "" : "  ·  " + about);
        addRow(names, app.icon, app.name, about);
    }
    // Templates added by hand: by their own names
    for (const auto& t : present)
        if (!Wallpaper::templateRequired(t) && std::ranges::find(covered, t) == covered.end()) addRow({t}, "palette", t, "Your own template");
}

void appearancePage(Flex* page, Popup& popup) {
    auto* wp = group(page, "Wallpaper");
    const std::string path = Wallpaper::get().path();
    // The file name: a full path pushes the buttons off the row
    auto current = row(wp, "", "Current wallpaper", path.empty() ? "None" : std::filesystem::path(path).filename().string());
    // Thumbnail in front of the text
    auto* thumb = std::make_unique<ImageView>(96, 54, [path](float scale, int& w, int& h) -> GLuint {
        if (path.empty()) return 0;
        auto* t = Images::get().picture(path, static_cast<int>(96 * scale), static_cast<int>(54 * scale), true);
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    }).release();
    thumb->cover = true;
    current.row->adopt(std::unique_ptr<Node>(thumb), 0);
    pillButton(current.row, "Next", false, [] { Wallpaper::get().next(1); }, "skip_next");

    // Every wallpaper side by side; a click applies it
    const auto files = Wallpaper::get().list();
    if (!files.empty()) {
        auto* gallery = wp->add<Flex>(Flex::Direction::Column);
        gallery->setPadding({16, 14})->setSpacing(8)->setAlignItems(Align::Stretch);
        auto* grid = gallery->add<Flow>();
        grid->spacing = 10;
        constexpr float kW = 152, kH = 86;
        for (size_t i = 0; i < files.size() && i < 60; ++i) {
            const std::string file = files[i];
            auto* tile = grid->add<Button>();
            tile->setPadding(Insets(3))->setRadius(Radii(14));
            tile->pressedRadius = 8;
            if (file == path) tile->setBorder(3, "primary");
            auto* img = tile->add<ImageView>(kW, kH, [file](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().picture(file, static_cast<int>(kW * scale), static_cast<int>(kH * scale), true);
                if (!t) return 0;
                w = t->width;
                h = t->height;
                return t->id;
            });
            img->cover = true;
            tile->onClicked = [file](uint32_t button) {
                if (button != BTN_LEFT) return;
                Wallpaper::get().set(file);
                // The page shows the new current one (not inside this click)
                EventLoop::get().defer([] {
                    reshowPage("appearance");
                });
            };
        }
        auto* foot = gallery->add<Flex>(Flex::Direction::Row);
        foot->setSpacing(8);
        auto* where = foot->add<Label>(Wallpaper::get().directory());
        where->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420);
        where->grow = true;
        pillButton(foot, "Browse files…", false, [] {
            if (open) open("files wallpaper");
        }, "folder_open");
        pillButton(foot, "Browse all…", false, [&popup] {
            // The app stays open behind it; a popup makes way
            if (!popup.isWindow()) popup.close();
            if (open) open("wallpapers");
        }, "wallpaper");
    }
    choiceRow<std::string>(wp, "animation", "Transition", "", "/background/wallpaperTransition", "random",
                           {{"random", "Random"}, {"fade", "Fade"}, {"wipe", "Wipe"}, {"grow", "Grow"}});
    switchRow(wp, "schedule", "Desktop clock", "A large clock on the wallpaper", "/desktop/clock", true);
    auto edit = row(wp, "dashboard_customize", "Desktop widgets and shortcuts",
                    "Place the clock, calendar, weather, apps and files; or right-click the desktop");
    pillButton(edit.row, "Edit", true, [] {
        if (open) open("desktopedit");
    }, "edit");
    timeDateRows(group(page, "Desktop clock: time and date"),
                 {{"/desktop/timeFormat", "hh:mm", "/desktop/showDate", true, "/desktop/dateFormat", "dddd, d MMMM"}});

    auto* colour = group(page, "Colours", "Material You colours come from the wallpaper, or from an accent you pick");
    switchRow(colour, "dark_mode", "Dark theme", "", "/background/darkmode", true, [](bool) { recolour(); });
    // The wallpaper's colours in four styles, basic colours, and the style
    accentRows(colour);

    appThemingRows(page);

    auto* fonts = group(page, "Fonts");
    fontRow(fonts, "text_fields", "Interface", "/appearance/fonts/main", "Google Sans Flex", false);
    fontRow(fonts, "code", "Monospace", "/appearance/fonts/monospace", "JetBrains Mono NF", true);
}

void stylePage(Flex* page, Popup&) {
    auto* fx = group(page, "Blur & transparency", "Panels let the desktop show through, softened by the compositor's blur");
    switchRow(fx, "blur_on", "Blur", Hyprland::get().available() ? "Behind the bar, panels, notifications and OSD"
                                                                  : "Needs Hyprland, or a compositor with ext-background-effect",
              "/appearance/blur/enabled", true);
    switchRow(fx, "wallpaper", "Blur behind widgets", "On the desktop, the lock screen and the login screen", "/appearance/blur/widgets", true);
    sliderRow(fx, "blur_circular", "Widget blur strength", "/appearance/blur/widgetRadius", 6, 2, 24, 1,
              [](double v) { return std::format("Radius {:.0f}", v); });
    sliderRow(fx, "toolbar", "Bar opacity", "/appearance/transparency/bar", 0.7, 0.3, 1, 0.05, percent);
    sliderRow(fx, "select_window", "Panel opacity", "/appearance/transparency/panels", 0.72, 0.3, 1, 0.05, percent);
    // The blur itself (shared with windows) and windows' own look are Hyprland's
    hyprLink(fx, "blur_medium", "Blur strength and look", "Size, passes, noise, contrast, brightness and vibrancy", "hypr-decoration");
    hyprLink(fx, "select_window", "Window opacity, shadows and corners", "Active and inactive windows", "hypr-decoration");

    // Text size: the shell's (Accessibility's), and each part's on top of it
    auto* text = group(page, "Text & widget size", "The shell's text size applies everywhere; the others adjust one part on top of it. Widget size grows widgets whole, on the desktop, lock and login screens (each desktop widget also has its own, in the desktop editor)");
    auto scaleText = [](double v) { return std::format("{:.0f}%", v * 100); };
    sliderRow(text, "format_size", "Whole shell", "/accessibility/textScale", 1.0, 0.85, 1.5, 0.05, scaleText);
    sliderRow(text, "toolbar", "Status bar", "/appearance/textScale/bar", 1.0, 0.7, 1.4, 0.05, scaleText);
    sliderRow(text, "lock", "Lock & login screen", "/appearance/textScale/lock", 1.0, 0.7, 1.6, 0.05, scaleText);
    sliderRow(text, "widgets", "Widget size", "/appearance/textScale/widgets", 1.0, 0.6, 1.8, 0.05, scaleText);

    // Scrolling in both Settings apps and every panel
    auto* scrolling = group(page, "Scrolling", "In both Settings apps and every panel that scrolls");
    switchRow(scrolling, "swipe_vertical", "Smooth scrolling", "Mouse wheel steps glide instead of jumping", "/appearance/scroll/smooth", true);
    choiceRow<double>(scrolling, "speed", "Scroll speed", "How far a wheel step goes", "/appearance/scroll/step", 64,
                      {{40, "Slow"}, {64, "Normal"}, {96, "Fast"}, {140, "Faster"}});
    choiceRow<double>(scrolling, "touchpad_mouse", "Touchpad glide", "How far a flick carries on", "/appearance/scroll/glide", 1.0,
                      {{0.0, "Off"}, {0.6, "Short"}, {1.0, "Normal"}, {1.5, "Long"}});

    auto* shape = group(page, "Shape & motion");
    choiceRow<double>(shape, "rounded_corner", "Corners", "Every card, button and panel", "/appearance/roundness", 1.0,
                      {{0.35, "Sharp"}, {0.7, "Soft"}, {1.0, "Round"}, {1.3, "Extra"}});
    choiceRow<double>(shape, "animation", "Animation speed", "The shell's panels and pages", "/appearance/animationSpeed", 1.0,
                      {{0.0, "Off"}, {0.6, "Fast"}, {1.0, "Normal"}, {1.5, "Relaxed"}});
    hyprLink(shape, "animation", "Window animations", "Each animation's speed, curve and style", "hypr-animations");
    switchRow(shape, "palette", "Themed icons", "Settings' icons take on your wallpaper's colours, as Android's do", "/appearance/themedIcons", true);

    auto* reset = group(page, "Defaults");
    auto r = row(reset, "restart_alt", "Reset look & feel", "Blur, opacity, corners and motion");
    pillButton(r.row, "Reset", false, [] {
        for (const char* key : {"/appearance/blur/enabled", "/appearance/blur/widgets", "/appearance/blur/widgetRadius", "/appearance/textScale", "/appearance/scroll",
                                "/appearance/transparency/bar",
                                "/appearance/transparency/panels", "/appearance/roundness", "/appearance/animationSpeed"})
            Config::get().set(key, nullptr);
        // Not inside the click: the rebuild destroys this button
        EventLoop::get().defer([] {
            reshowPage("style");
        });
    });
}

void barPage(Flex* page, Popup&) {
    auto* layout = group(page, "Layout");
    choiceRow<std::string>(layout, "vertical_align_top", "Position", "", "/statusBar/position", "top",
                           {{"top", "Top"}, {"bottom", "Bottom"}});
    const int style = Config::get().value<int>("/statusBar/backgroundStyle", 0);
    // The floating bar's own rows come and go with it
    choiceRow<int>(layout, "toolbar", "Style", "",
                   "/statusBar/backgroundStyle", 0, {{0, "Pills"}, {1, "Full width"}, {2, "Floating bar"}}, [style](int v) {
                       if ((v == 2) != (style == 2)) EventLoop::get().defer([] { reshowPage("bar"); });
                   });
    sliderRow(layout, "height", "Height", "/statusBar/height", 40, 32, 56, 2, [](double v) { return std::format("{:.0f} px", v); });
    if (style == 2) {
        sliderRow(layout, "width", "Width", "/statusBar/width", 1.0, 0.4, 1.0, 0.02, percent);
        sliderRow(layout, "padding", "Gap from the edge", "/statusBar/gap", 6, 0, 24, 1, [](double v) { return std::format("{:.0f} px", v); });
        choiceRow<double>(layout, "rounded_corner", "Ends", "", "/statusBar/endRadius", 100,
                          {{100, "Round"}, {16, "Curved"}, {6, "Square"}});
    }

    auto* look = group(page, "Pills & blur", "Auto fills the widgets' pills in the Pills style, and leaves them on the bar in the others");
    choiceRow<std::string>(look, "highlight", "Pill highlight", "", "/statusBar/pillColor", "auto",
                           {{"auto", "Auto"}, {"none", "None"}, {"surface", "Surface"}, {"tonal", "Tonal"}, {"accent", "Accent"},
                            {"outline", "Outline"}});
    sliderRow(look, "rounded_corner", "Pill corners", "/statusBar/pillRadius", 100, 0, 100, 2,
              [](double v) { return v >= 100 ? std::string("Fully round") : std::format("{:.0f} px", v); });
    sliderRow(look, "border_style", "Pill border", "/statusBar/pillBorder",
              Config::get().value<std::string>("/statusBar/pillColor", "auto") == "outline" ? 1.5 : 0, 0, 4, 0.5,
              [](double v) { return v <= 0 ? std::string("None") : std::format("{:.1f} px", v); });
    switchRow(look, "blur_on", "Blur behind the bar", Theme::get().blur() ? "" : "Blur is off in Look & feel", "/statusBar/blur", true);
    sliderRow(look, "opacity", "Bar opacity", "/appearance/transparency/bar", 0.7, 0.3, 1, 0.05, percent);
    auto more = row(look, "blur_medium", "Blur strength and look", "Shared by the shell and windows");
    pillButton(more.row, "Look & feel", false, [] { settingsPage("style"); }, "open_in_new");

    // Widgets, as Android 17's editors work: each zone shows its widgets as
    // chips in a row, the way the bar does (groups share one outlined
    // capsule). Tap a chip to select it; a toolbar moves it (into the next
    // zone at either end), groups it with the next widget or splits its
    // group, and removes it. The widgets not on the bar sit below as icons:
    // a tap adds one to the chosen zone.
    auto* live = page->add<Live>();
    live->setSpacing(10);
    auto selected = std::make_shared<std::string>(); // widget id
    auto target = std::make_shared<int>(2);          // the zone "Add" puts widgets in
    live->sync = [live, selected, target] {
        live->clearChildren();
        using shell::Bar;
        auto* g = group(live, "Widgets", "Tap a widget to move, group or remove it; tap one below to add it");
        auto onBar = row(g, "edit", "Edit on the bar", "Move and remove widgets in place; right-clicking the bar does this too");
        pillButton(onBar.row, "Edit bar", false, [] {
            if (editBar) editBar(true);
        }, "edit");

        auto lay = Bar::layout(); // each handler edits its own copy
        auto meta = [](const std::string& id) {
            for (const auto& m : Bar::modules())
                if (id == m.id) return m;
            return Bar::Module{"", "extension", "Unknown"};
        };
        if (!selected->empty() && !Bar::placed(lay, *selected)) selected->clear();
        auto save = [live](const Bar::Layout& l) {
            Bar::setLayout(l);
            live->syncSoon();
        };
        auto select = [selected, live](const std::string& id) {
            *selected = *selected == id ? "" : id;
            live->syncSoon();
        };

        auto* zones = g->add<Flex>(Flex::Direction::Column);
        zones->setPadding({16, 12})->setSpacing(10)->setAlignItems(Align::Stretch);
        const char* names[] = {"Left", "Middle", "Right"};
        for (size_t z = 0; z < 3; ++z) {
            auto* zone = zones->add<Flex>(Flex::Direction::Column);
            zone->setPadding({14, 10})->setSpacing(8)->setRadius(Radii(24))->setAlignItems(Align::Stretch);
            zone->setBackground(ColorRef("on_surface", 0.06f), false);
            zone->add<Label>(names[z])->setSize(13)->setWeight(600)->setColor("primary", false);
            auto* chips = zone->add<Flow>();
            chips->spacing = 6;
            // One chip per widget: its icon and name, outlined when selected
            auto chip = [&](Node* parent, const std::string& id) {
                const auto m = meta(id);
                const bool on = *selected == id;
                auto* c = parent->add<Button>(Flex::Direction::Row);
                pill(c->setBackground(on ? ColorRef("primary") : ColorRef("on_surface", 0.1f), false))->setPadding({12, 8})->setSpacing(6);
                c->pressedRadius = 10;
                c->add<Icon>(m.icon, 18)->setFill(on)->setColor(on ? "on_primary" : "on_surface", false);
                c->add<Label>(m.name)->setSize(13)->setWeight(500)->setColor(on ? "on_primary" : "on_surface", false);
                c->onClicked = [select, id](uint32_t button) {
                    if (button == BTN_LEFT) select(id);
                };
            };
            if (lay[z].empty()) caption(chips, "Empty");
            for (const auto& gr : lay[z]) {
                if (gr.size() == 1) {
                    chip(chips, gr.front());
                    continue;
                }
                // A group: one outlined capsule, like on the bar
                auto* box = chips->add<Flex>(Flex::Direction::Row);
                pill(box)->setPadding({4, 4})->setSpacing(4);
                box->setBorder(1.5f, ColorRef("primary", 0.7f));
                for (const auto& id : gr) chip(box, id);
            }
        }

        // The selected widget's toolbar
        if (!selected->empty()) {
            const std::string id = *selected;
            size_t zone = 0;
            bool grouped = false, last = false;
            for (size_t z = 0; z < 3; ++z)
                for (size_t gi = 0; gi < lay[z].size(); ++gi)
                    if (std::ranges::find(lay[z][gi], id) != lay[z][gi].end()) {
                        zone = z;
                        grouped = lay[z][gi].size() > 1;
                        last = gi + 1 == lay[z].size();
                    }
            auto* bar = zones->add<Flex>(Flex::Direction::Row);
            bar->setPadding({18, 8, 8, 8})->setSpacing(4)->setRadius(Radii(28))->setBackground(ColorRef("on_surface", 0.1f), false);
            bar->add<Icon>(meta(id).icon, 20)->setColor("primary", false);
            auto* name = bar->add<Label>(std::string(meta(id).name) + "  ·  " + names[zone]);
            name->setSize(14)->setWeight(600)->setMaxWidth(260);
            name->grow = true;
            iconButton(bar, "arrow_back", [lay, id, save]() mutable {
                Bar::moveWidget(lay, id, -1);
                save(lay);
            }, 20);
            iconButton(bar, "arrow_forward", [lay, id, save]() mutable {
                Bar::moveWidget(lay, id, 1);
                save(lay);
            }, 20);
            // Group with the next widget in the zone, or split the group
            if (grouped || !last)
                iconButton(bar, grouped ? "link_off" : "link", [lay, id, zone, grouped, save]() mutable {
                    if (grouped) {
                        Bar::ungroup(lay, id);
                    } else {
                        std::vector<std::string> ids = {id};
                        bool next = false;
                        for (const auto& gr : lay[zone])
                            for (const auto& other : gr) {
                                if (next && ids.size() == 1) ids.push_back(other);
                                if (other == id) next = true;
                            }
                        Bar::groupWidgets(lay, zone, ids);
                    }
                    save(lay);
                }, 20);
            iconButton(bar, "delete", [lay, id, selected, save]() mutable {
                Bar::removeWidget(lay, id);
                selected->clear();
                save(lay);
            }, 20);
        }

        // The rest: tap to add to the chosen zone
        auto* add = group(live, "Add widgets");
        auto* addBox = add->add<Flex>(Flex::Direction::Column);
        addBox->setPadding({16, 12})->setSpacing(10)->setAlignItems(Align::Start);
        auto* to = addBox->add<Flex>(Flex::Direction::Row);
        to->setSpacing(10);
        to->add<Label>("Add to")->setSize(14)->setColor("on_surface_variant", false);
        connectedGroup<int>(to, *target, {{0, "Left"}, {1, "Middle"}, {2, "Right"}}, [target](int z) { *target = z; });
        auto* avail = addBox->add<Flow>();
        avail->spacing = 8;
        size_t free = 0;
        for (const auto& m : Bar::modules()) {
            if (Bar::placed(lay, m.id)) continue;
            ++free;
            auto* item = avail->add<Button>(Flex::Direction::Column);
            item->setPadding({4, 8})->setSpacing(6)->setRadius(Radii(20));
            item->pressedRadius = 12;
            item->fixedWidth = 104;
            auto* icon = item->add<Flex>(Flex::Direction::Row);
            pill(icon->setBackground(ColorRef("on_surface", 0.1f), false))->setJustify(Align::Center);
            icon->fixedWidth = 64;
            icon->fixedHeight = 52;
            icon->alignSelf = Align::Center;
            icon->add<Icon>(m.icon, 22)->setColor("on_surface", false);
            auto* label = item->add<Label>(m.name);
            label->setSize(12)->setMaxWidth(96)->setMaxLines(2)->setCentered(true)->setColor("on_surface_variant", false);
            label->alignSelf = Align::Center;
            const std::string id = m.id;
            item->onClicked = [lay, id, target, selected, save](uint32_t button) mutable {
                if (button != BTN_LEFT) return;
                Bar::add(lay, static_cast<size_t>(*target), id);
                *selected = id;
                save(lay);
            };
        }
        if (free == 0) caption(addBox, "Every widget is on the bar");

        auto* reset = live->add<Flex>(Flex::Direction::Row);
        reset->setJustify(Align::End);
        pillButton(reset, "Default layout", false, [live, selected] {
            selected->clear();
            Config::get().set("/statusBar/layout", nullptr);
            live->syncSoon();
        }, "restart_alt");
    };
    live->sync();

    // The time and the date on the bar: the island's clock and the Clock
    // widget, set together unless they're unlinked, then one at a time
    auto keys = [](const std::string& place) {
        const std::string base = "/bar/clock/" + place;
        return ClockKeys{base + "/timeFormat", Config::get().value<std::string>("/time/format", "hh:mm"), base + "/showDate", false,
                         base + "/dateFormat", "ddd d MMM"};
    };
    auto* clocks = group(page, "Time & date", "The clock in the island and the Clock widget");
    auto preview = row(clocks, "visibility", "Preview");
    for (const auto& [place, name] : {std::pair{"island", "Island"}, {"bar", "Clock widget"}}) {
        auto* chip = preview.row->add<Flex>(Flex::Direction::Column);
        pill(chip)->setPadding({14, 4})->setBackground("surface_container_highest", false);
        chip->setAlignItems(Align::Center);
        chip->add<Label>(name)->setSize(11)->setColor("on_surface_variant", false);
        chip->add<Clock>(place)->setSize(14);
    }
    auto sameAs = [keys] {
        auto& c = Config::get();
        const auto a = keys("island"), b = keys("bar");
        return c.value<std::string>(a.time, a.timeDefault) == c.value<std::string>(b.time, b.timeDefault) &&
               c.value<bool>(a.show, a.showDefault) == c.value<bool>(b.show, b.showDefault) &&
               c.value<std::string>(a.date, a.dateDefault) == c.value<std::string>(b.date, b.dateDefault);
    };
    struct ClockUi {
        bool linked;
        int place = 0; // unlinked: 0 the island, 1 the Clock widget
        Flex* body = nullptr;
        std::function<void()> fill;
    };
    auto ui = std::make_shared<ClockUi>(ClockUi{Config::get().value<bool>("/bar/clock/linked", sameAs())});
    auto link = row(clocks, "link", "Same for both", "Off: set the island and the Clock widget apart");
    auto* lsw = link.row->add<Switch>();
    lsw->setChecked(ui->linked, false);
    ui->body = page->add<Flex>(Flex::Direction::Column);
    ui->body->setAlignItems(Align::Stretch);
    ui->fill = [raw = ui.get(), keys] {
        raw->body->clearChildren();
        auto* g = raw->body->add<Segments>();
        if (!raw->linked) {
            auto which = row(g, "tune", "Setting up");
            auto* self = raw;
            connectedGroup<int>(which.row, raw->place, {{0, "Island"}, {1, "Clock widget"}}, [self](int v) {
                self->place = v;
                EventLoop::get().defer([self] { self->fill(); });
            });
        }
        if (raw->linked) timeDateRows(g, {keys("island"), keys("bar")});
        else timeDateRows(g, {keys(raw->place == 0 ? "island" : "bar")});
        raw->body->requestLayout();
    };
    lsw->onToggled = [ui, keys](bool on) {
        ui->linked = on;
        Config::get().set("/bar/clock/linked", on);
        if (on) {
            // Linking: the Clock widget takes the island's settings
            auto& c = Config::get();
            const auto a = keys("island"), b = keys("bar");
            c.set(b.time, c.value<std::string>(a.time, a.timeDefault));
            c.set(b.show, c.value<bool>(a.show, a.showDefault));
            c.set(b.date, c.value<std::string>(a.date, a.dateDefault));
        }
        EventLoop::get().defer([ui] { ui->fill(); });
    };
    ui->fill();

    auto* island = group(page, "Island");
    switchRow(island, "view_carousel", "Show workspace switches in the island", "", "/statusBar/islandWorkspace", true);

    // Text longer than its space: how each place shows the rest
    auto* scrolling = group(page, "Scrolling text", "For titles longer than their space");
    const std::vector<std::pair<std::string, std::string>> styles = {{"loop", "Loop"}, {"bounce", "Back and forth"}, {"off", "Off"}};
    choiceRow<std::string>(scrolling, "title", "Window title", "", "/bar/scrolling/windowTitle", "loop", styles);
    choiceRow<std::string>(scrolling, "music_note", "Music in the island", "", "/bar/scrolling/islandMedia", "loop", styles);
    choiceRow<std::string>(scrolling, "notifications", "Notifications in the island", "", "/bar/scrolling/islandNotice", "loop", styles);
    choiceRow<int>(scrolling, "speed", "Speed", "", "/bar/scrolling/speed", 30, {{18, "Slow"}, {30, "Normal"}, {50, "Fast"}});
}

void dockPage(Flex* page, Popup&) {
    auto* g = group(page, "Dock", "Pinned and running apps on the bottom edge");
    choiceRow<std::string>(g, "dock_to_bottom", "Show", "", "/dock/mode", "autohide",
                           {{"off", "Off"}, {"autohide", "Auto-hide"}, {"always", "Always"}});
    sliderRow(g, "photo_size_select_small", "Icon size", "/dock/iconSize", 44, 32, 64, 2,
              [](double v) { return std::format("{:.0f} px", v); });
    switchRow(g, "apps", "Running apps", "Apps that are open but not pinned, after a divider", "/dock/showRunning", true);
    auto* dlook = group(page, "Look", "How the dock sits on what's behind it");
    switchRow(dlook, "blur_on", "Blur behind the dock", Theme::get().blur() ? "" : "Blur is off in Look & feel", "/dock/blur", true);
    sliderRow(dlook, "opacity", "Opacity", "/dock/opacity", Theme::get().panelOpacity(), 0.2, 1, 0.05, percent);
    row(g, "mouse", "Click to focus or open", "Middle click opens a new window · right click pins or unpins");

    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto query = std::make_shared<std::string>();
    live->sync = [live, query] {
        live->clearChildren();
        auto* pins = group(live, "Pinned");
        auto ids = Dock::pinned();
        if (ids.empty()) row(pins, "push_pin", "Nothing pinned", "Search below, or right-click an app in the dock");
        for (size_t i = 0; i < ids.size(); ++i) {
            const Apps::App* app = nullptr;
            for (const auto& a : Apps::get().all())
                if (a.id == ids[i] || a.id == ids[i] + ".desktop") app = &a;
            auto r = row(pins, "", app ? app->name : ids[i], app ? app->comment : "Not installed");
            const std::string icon = app ? app->icon : ids[i];
            r.row->adopt(std::make_unique<ImageView>(32, [icon](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().icon(icon, static_cast<int>(std::lround(32 * scale)));
                if (!t) return 0;
                w = t->width;
                h = t->height;
                return t->id;
            }), 0);
            auto move = [ids, i](int d) mutable {
                const size_t j = static_cast<size_t>(static_cast<long>(i) + d);
                if (j >= ids.size()) return;
                std::swap(ids[i], ids[j]);
                Dock::setPinned(ids);
            };
            if (i > 0) iconButton(r.row, "arrow_upward", [move]() mutable { move(-1); });
            if (i + 1 < ids.size()) iconButton(r.row, "arrow_downward", [move]() mutable { move(1); });
            iconButton(r.row, "close", [ids, i]() mutable {
                ids.erase(ids.begin() + static_cast<long>(i));
                Dock::setPinned(ids);
            });
        }

        auto* add = group(live, "Pin an app");
        auto* fieldRow = add->add<Flex>(Flex::Direction::Row);
        fieldRow->setPadding({16, 12});
        auto* field = fieldRow->add<TextField>();
        field->grow = true;
        field->setPlaceholder("Search apps");
        field->setText(*query);
        field->onChanged = [query, live](const std::string& t) {
            *query = t;
            live->syncSoon();
        };
        if (!query->empty()) field->focus();
        std::vector<std::pair<double, const Apps::App*>> hits;
        for (const auto& a : Apps::get().all()) {
            const double sc = query->empty() ? 0 : fuzzyScore(*query, a.name);
            if (sc > 0) hits.push_back({sc, &a});
        }
        std::ranges::sort(hits, std::greater{}, &std::pair<double, const Apps::App*>::first);
        for (size_t k = 0; k < hits.size() && k < 6; ++k) {
            const auto* a = hits[k].second;
            std::string id = a->id;
            if (id.ends_with(".desktop")) id.resize(id.size() - 8);
            const bool pinnedAlready = std::ranges::find(ids, id) != ids.end();
            auto r = row(add, "apps", a->name, a->comment);
            if (pinnedAlready) r.row->add<Label>("Pinned")->setSize(13)->setColor("on_surface_variant", false);
            else pillButton(r.row, "Pin", true, [id, query] {
                auto now = Dock::pinned();
                now.push_back(id);
                query->clear();
                Dock::setPinned(now);
            }, "push_pin");
        }
    };
    // Config writes come back through the watcher: rebuild then
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();
}

// ── Battery ──

void batteryPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->watch(Power::get().changed);
    live->sync = [live] {
        live->clearChildren();
        auto& p = Power::get();
        if (p.hasBattery()) {
            // The charge, large, with a bar that fills to it
            auto* hero = live->add<Flex>(Flex::Direction::Column);
            hero->setPadding({24, 20})->setSpacing(10)->setRadius(Radii(kOuter))->setBackground("surface_container_high", false);
            hero->setAlignItems(Align::Stretch);
            auto* top = hero->add<Flex>(Flex::Direction::Row);
            top->setSpacing(14);
            const bool low = p.percent() <= 15 && !p.charging();
            top->add<Icon>(p.icon(), 40)->setFill(true)->setColor(low ? "error" : "primary", false);
            auto* texts = top->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start);
            texts->add<Label>(std::to_string(p.percent()) + "%")->setSize(36)->setWeight(600);
            texts->add<Label>(p.status())->setSize(14)->setColor("on_surface_variant", false);
            hero->add<Meter>(static_cast<float>(p.percent()) / 100.0f,
                             ColorRef(low ? "error" : p.charging() ? "tertiary" : "primary"));

            auto* g = group(live, "Details");
            row(g, "bolt", p.charging() ? "Charging at" : "Using",
                p.watts() > 0 ? std::format("{:.1f} W", p.watts()) : p.full() ? "Plugged in, not charging" : "Not reported");
            row(g, "battery_profile", "Health",
                p.health() > 0 ? std::format("{}% of its design capacity", p.health()) : "Not reported");
        }

        auto* modes = group(live, "Power mode", "How hard the CPU and GPU may work: battery life against speed");
        if (p.profiles().empty()) {
            row(modes, "power_off", "Power modes are unavailable", "Install and enable power-profiles-daemon");
            return;
        }
        const std::pair<const char*, const char*> labels[] = {
            {"power-saver", "Power saver"}, {"balanced", "Balanced"}, {"performance", "Performance"}};
        std::vector<std::pair<std::string, std::string>> options;
        for (const auto& [id, label] : labels)
            if (std::ranges::find(p.profiles(), id) != p.profiles().end()) options.push_back({id, label});
        auto r = row(modes, "speed", "Mode", p.profile() == "power-saver"   ? "Longest battery life, slower"
                                            : p.profile() == "performance" ? "Fastest, drains the battery sooner"
                                                                           : "Speed when needed, saving otherwise");
        connectedGroup<std::string>(r.row, p.profile(), options, [](std::string v) { Power::get().setProfile(v); });
    };
    live->sync();
}

// ── Storage ──

// Async work outliving a page must not touch it: callbacks check this flag,
// cleared when the page (and this node with it) goes away
class AliveGuard : public Node {
public:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    ~AliveGuard() override { *alive = false; }

protected:
    SizeF doMeasure(SizeF) override { return {}; }
};

void storagePage(Flex* page, Popup&) {
    auto alive = page->add<AliveGuard>()->alive;
    namespace fs = std::filesystem;

    // Each real filesystem once (btrfs mounts one device many times)
    auto* disks = group(page, "Disks");
    std::ifstream mounts("/proc/mounts");
    std::string dev, mnt, type, rest;
    std::set<unsigned long> seen;
    while (mounts >> dev >> mnt >> type && std::getline(mounts, rest)) {
        if (!dev.starts_with("/dev/") || type == "squashfs" || mnt.starts_with("/snap") || mnt.starts_with("/var/lib/docker")) continue;
        struct statvfs st {};
        if (statvfs(mnt.c_str(), &st) != 0 || st.f_blocks == 0 || !seen.insert(st.f_fsid).second) continue;
        const uint64_t total = static_cast<uint64_t>(st.f_blocks) * st.f_frsize;
        const uint64_t free = static_cast<uint64_t>(st.f_bavail) * st.f_frsize;
        const uint64_t used = total - static_cast<uint64_t>(st.f_bfree) * st.f_frsize;
        auto r = row(disks, mnt == "/" ? "hard_drive" : "storage", mnt == "/" ? "System (/)" : mnt,
                     std::format("{} used of {}  ·  {} free", formatBytes(used), formatBytes(total), formatBytes(free)));
        const float share = total ? static_cast<float>(used) / static_cast<float>(total) : 0;
        auto* meter = r.row->add<Meter>(share, ColorRef(share > 0.9f ? "error" : "primary"), 10);
        meter->fixedWidth = 200;
    }

    // Where the space goes at home, counted in the background
    auto* home = group(page, "In your home folder", "Counted when this page opens");
    auto measure = [alive](const std::string& path, Label* into) {
        m3::run({"du", "-sb", path}, [alive, into](int, const std::string& out) {
            if (!*alive) return;
            into->setText(out.empty() ? "Empty" : formatBytes(std::strtoull(out.c_str(), nullptr, 10)));
        });
    };
    const std::pair<const char*, const char*> folders[] = {
        {"download", "Downloads"}, {"description", "Documents"}, {"image", "Pictures"}, {"movie", "Videos"}, {"music_note", "Music"}};
    for (const auto& [icon, name] : folders) {
        const std::string path = paths::home() + "/" + name;
        std::error_code ec;
        if (!fs::is_directory(path, ec)) continue;
        auto r = row(home, icon, name, "Counting…");
        measure(path, r.subtitle);
        pillButton(r.row, "Open", false, [path] { spawn("xdg-open '" + path + "'"); });
    }

    // Space that can go
    auto* clean = group(page, "Free up space");
    auto pkg = row(clean, "inventory_2", "Package cache", "Counting…");
    measure("/var/cache/pacman/pkg", pkg.subtitle);
    pillButton(pkg.row, "Clean", false, [alive, sub = pkg.subtitle] {
        // Root through polkit: one password for the whole clean-up
        sub->setText("Cleaning…");
        Packages::get().runAsRoot("Cleaning the package cache…",
                                  "if command -v paccache >/dev/null; then paccache -rk1 && paccache -ruk0; "
                                  "else pacman -Sc --noconfirm; fi",
                                  "Package cache cleaned");
        // The new size once it has finished
        auto conn = std::make_shared<Connection>();
        *conn = Packages::get().changed.connect([alive, sub, conn] {
            if (Packages::get().busy() || !*alive) return;
            sub->setText(Packages::get().status());
            conn->disconnect();
        });
    }, "cleaning_services");
    auto removeAll = [alive](const std::string& dir, Label* sizeLabel) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) fs::remove_all(e.path(), ec);
        if (*alive) sizeLabel->setText("Empty");
    };
    const std::string thumbs = paths::home() + "/.cache/thumbnails";
    auto th = row(clean, "photo_library", "Thumbnails", "Counting…");
    measure(thumbs, th.subtitle);
    pillButton(th.row, "Clear", false, [removeAll, thumbs, sub = th.subtitle] { removeAll(thumbs, sub); });
    const std::string trash = paths::home() + "/.local/share/Trash";
    auto tr = row(clean, "delete", "Trash", "Counting…");
    measure(trash, tr.subtitle);
    pillButton(tr.row, "Empty", false, [removeAll, trash, sub = tr.subtitle] {
        removeAll(trash + "/files", sub);
        removeAll(trash + "/info", sub);
    });
}

// ── System monitor ──

namespace {

// Samples the system while the page holds it
struct StatsLease : Node {
    SystemStats::Lease lease = SystemStats::get().lease();
};

std::string duration(double seconds) {
    const long t = static_cast<long>(seconds);
    if (t >= 86400) return std::format("{}d {}h", t / 86400, (t % 86400) / 3600);
    return std::format("{}h {}m", t / 3600, (t % 3600) / 60);
}

// Signals a process: directly when it's the user's, through polkit otherwise
void signalProcess(const SystemStats::Process& p, int sig) {
    if (p.mine) {
        ::kill(p.pid, sig);
        return;
    }
    m3::run({"pkexec", "kill", std::format("-{}", sig), std::to_string(p.pid)}, nullptr);
}

// Ends a process and starts its command again, in the same folder
void restartProcess(int pid) {
    namespace fs = std::filesystem;
    std::ifstream in(std::format("/proc/{}/cmdline", pid), std::ios::binary);
    std::vector<std::string> argv;
    for (std::string arg; std::getline(in, arg, '\0');) argv.push_back(arg);
    if (argv.empty()) return; // a kernel thread, or gone
    std::error_code ec;
    const std::string cwd = fs::read_symlink(std::format("/proc/{}/cwd", pid), ec).string();
    ::kill(pid, SIGTERM);
    // Once it has gone (or after 3 s, forced), the same command again
    auto tries = std::make_shared<int>(0);
    auto timer = std::make_shared<EventLoop::TimerId>(0);
    *timer = EventLoop::get().addTimer(100, [pid, argv, cwd, tries, timer] {
        const bool alive = ::kill(pid, 0) == 0;
        if (alive && ++*tries < 30) return;
        if (alive) ::kill(pid, SIGKILL);
        EventLoop::get().cancel(*timer);
        std::string cmd = cwd.empty() ? "" : "cd '" + cwd + "' && ";
        cmd += "exec";
        for (const auto& a : argv) {
            std::string q = "'";
            for (char c : a) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
            cmd += " " + q + "'";
        }
        spawn(cmd);
    }, true);
}

} // namespace

void monitorPage(Flex* page, Popup&) {
    struct State {
        std::string query;
        int sort = 0;  // 0 CPU, 1 memory, 2 name
        int open = 0;  // the pid whose actions show
        int armed = 0; // the pid whose Force kill was tapped once
    };
    auto state = std::make_shared<State>();
    page->add<StatsLease>();

    // Overview: live while the page is open
    auto* overview = page->add<Live>();
    overview->setSpacing(6);
    overview->sync = [overview] {
        overview->clearChildren();
        auto& s = SystemStats::get();
        auto* g = group(overview, "Overview");
        auto meterRow = [&](const char* icon, const std::string& title, const std::string& value, float share) {
            auto r = row(g, icon, title, value);
            auto* m = r.row->add<Meter>(share, ColorRef(share > 0.9f ? "error" : "primary"), 10);
            m->fixedWidth = 220;
        };
        meterRow("memory", "CPU", std::format("{:.0f} %  ·  {} cores{}", s.cpu(), s.cores(),
                                              s.temperature() > 0 ? std::format("  ·  {:.0f} °C", s.temperature()) : ""),
                 static_cast<float>(s.cpu() / 100));
        meterRow("memory_alt", "Memory", std::format("{} of {}", formatBytes(s.memUsed()), formatBytes(s.memTotal())),
                 s.memTotal() ? static_cast<float>(s.memUsed()) / static_cast<float>(s.memTotal()) : 0);
        if (s.swapTotal() > 0)
            meterRow("swap_horiz", "Swap", std::format("{} of {}", formatBytes(s.swapUsed()), formatBytes(s.swapTotal())),
                     static_cast<float>(s.swapUsed()) / static_cast<float>(s.swapTotal()));
        row(g, "schedule", "Up for", duration(s.uptime()));
    };
    overview->watch(SystemStats::get().changed);
    overview->sync();

    // Processes: search, sort, and actions on the one opened
    auto* head = group(page, "Processes", "Tap one to stop, end, force or restart it");
    auto* tools = head->add<Flex>(Flex::Direction::Row);
    tools->setPadding({16, 10})->setSpacing(10);
    auto* field = tools->add<TextField>();
    field->setPlaceholder("Search by name or PID");
    field->grow = true;
    auto* list = page->add<Live>();
    list->setSpacing(6);
    connectedGroup<int>(tools, 0, {{0, "CPU"}, {1, "Memory"}, {2, "Name"}}, [state, list](int s) {
        state->sort = s;
        list->syncSoon();
    });
    field->onChanged = [state, list](const std::string& t) {
        state->query = t;
        list->syncSoon();
    };
    list->sync = [list, state] {
        list->clearChildren();
        auto procs = SystemStats::get().allProcesses();
        if (procs.empty()) {
            caption(list, "Reading processes…");
            return;
        }
        if (state->sort == 1) std::ranges::sort(procs, std::greater{}, &SystemStats::Process::rss);
        if (state->sort == 2) std::ranges::sort(procs, {}, &SystemStats::Process::name);
        auto lower = [](std::string t) {
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        const std::string q = lower(state->query);
        auto* g = list->add<Segments>();
        size_t shown = 0;
        for (const auto& p : procs) {
            if (!q.empty() && lower(p.name).find(q) == std::string::npos && std::to_string(p.pid).find(q) == std::string::npos)
                continue;
            if (++shown > 60) break; // search finds the rest
            const bool stopped = p.state == 'T' || p.state == 't';
            const bool open = state->open == p.pid;
            auto* b = g->add<Button>(Flex::Direction::Row);
            b->setPadding({20, 10})->setSpacing(14);
            b->pressedRadius = 10;
            b->add<Icon>(stopped ? "pause_circle" : p.mine ? "terminal" : "admin_panel_settings", 20)
                ->setColor(stopped ? "tertiary" : "on_surface_variant", false);
            auto* texts = b->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(p.name)->setSize(15)->setWeight(500)->setMaxWidth(300);
            texts->add<Label>(std::format("PID {}{}{}", p.pid, p.mine ? "" : "  ·  system", stopped ? "  ·  stopped" : ""))
                ->setSize(12)->setColor("on_surface_variant", false);
            auto* cpu = b->add<Label>(std::format("{:.1f} %", p.cpu));
            cpu->setSize(13)->setColor("on_surface_variant", false);
            cpu->fixedWidth = 64;
            auto* mem = b->add<Label>(formatBytes(p.rss));
            mem->setSize(13)->setColor("on_surface_variant", false);
            mem->fixedWidth = 84;
            const int pid = p.pid;
            b->onClicked = [state, list, pid](uint32_t button) {
                if (button != BTN_LEFT) return;
                state->open = state->open == pid ? 0 : pid;
                state->armed = 0;
                list->syncSoon();
            };
            if (!open) continue;
            // Its actions
            auto* actions = g->add<Flex>(Flex::Direction::Row);
            actions->setPadding({54, 4, 20, 12})->setSpacing(8);
            const SystemStats::Process copy = p;
            pillButton(actions, stopped ? "Resume" : "Stop", false, [copy, stopped] { signalProcess(copy, stopped ? SIGCONT : SIGSTOP); },
                       stopped ? "play_arrow" : "pause");
            pillButton(actions, "End", false, [copy] { signalProcess(copy, SIGTERM); }, "close");
            if (copy.mine) pillButton(actions, "Restart", false, [pid] { restartProcess(pid); }, "restart_alt");
            const bool armed = state->armed == pid;
            auto* kill = pillButton(actions, armed ? "Tap to force" : "Force kill", false, {}, "dangerous");
            kill->setBackground(armed ? ColorRef("error") : ColorRef("error_container"), false);
            for (auto& c : kill->children())
                if (auto* l = dynamic_cast<Label*>(c.get())) l->setColor(armed ? "on_error" : "on_error_container", false);
            kill->onClicked = [state, list, copy, armed](uint32_t button) {
                if (button != BTN_LEFT) return;
                if (!armed) {
                    state->armed = copy.pid;
                    list->syncSoon();
                    return;
                }
                state->armed = 0;
                signalProcess(copy, SIGKILL);
            };
        }
        if (shown == 0) row(g, "search_off", "No process matches");
    };
    list->watch(SystemStats::get().changed);
    list->sync();
}

// ── Users ──

namespace {

struct UserInfo {
    std::string name, realName, home;
    bool admin = false;
};

// People's accounts: uid 1000 and up, with a login shell
std::vector<UserInfo> localUsers() {
    std::vector<UserInfo> out;
    std::set<std::string> wheel;
    if (const struct group* g = getgrnam("wheel")) // struct: Settings has its own group()
        for (char** m = g->gr_mem; m && *m; ++m) wheel.insert(*m);
    setpwent();
    while (const struct passwd* p = getpwent()) {
        if (p->pw_uid < 1000 || p->pw_uid >= 60000) continue;
        const std::string shell = p->pw_shell ? p->pw_shell : "";
        if (shell.ends_with("nologin") || shell.ends_with("false")) continue;
        std::string real = p->pw_gecos ? p->pw_gecos : "";
        real = real.substr(0, real.find(','));
        out.push_back({p->pw_name, real.empty() ? p->pw_name : real, p->pw_dir ? p->pw_dir : "", wheel.contains(p->pw_name)});
    }
    endpwent();
    return out;
}

// Runs a root script through the polkit dialog; `input` goes to its stdin
// (passwords never appear in a command line). `done` gets how it ended.
void asRoot(const std::string& script, const std::string& input, std::function<void(const std::string&)> done) {
    m3::run({"pkexec", "/usr/bin/sh", "-c", script + " 2>&1"},
            [done = std::move(done)](int code, const std::string& out) {
        std::string last;
        std::stringstream ss(out);
        for (std::string l; std::getline(ss, l);)
            if (!l.empty()) last = l;
        done(code == 0 ? "" : code == 126 || code == 127 ? "Cancelled: no password given" : (last.empty() ? "Didn't work" : last));
    },
            input);
}

std::string shellQuote(const std::string& s) {
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}

bool validUsername(const std::string& u) {
    if (u.empty() || u.size() > 32 || !(std::islower(static_cast<unsigned char>(u[0])) || u[0] == '_')) return false;
    return std::ranges::all_of(u, [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
}

} // namespace

void usersPage(Flex* page, Popup&) {
    auto alive = page->add<AliveGuard>()->alive;
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto status = std::make_shared<std::string>();
    // What a change did, shown under the page's groups
    auto report = [alive, status, live](const std::string& doneText) {
        return [alive, status, live, doneText](const std::string& err) {
            if (!*alive) return;
            *status = err.empty() ? doneText : err;
            live->syncSoon();
        };
    };
    live->sync = [live, status, report] {
        live->clearChildren();
        const std::string me = std::getenv("USER") ? std::getenv("USER") : "";
        const auto users = localUsers();
        const auto self = std::ranges::find(users, me, &UserInfo::name);
        const size_t admins = static_cast<size_t>(std::ranges::count_if(users, &UserInfo::admin));

        // You: picture, name and password
        auto* you = group(live, "You");
        auto head = row(you, "", self != users.end() ? self->realName : me,
                        me + (self != users.end() && self->admin ? "  ·  Administrator" : ""));
        head.row->adopt(std::make_unique<AvatarView>(paths::home() + "/.face", self != users.end() ? self->realName : me, 56), 0);
        // The shell's picker: the default pictures, or one of yours
        pillButton(head.row, "Change picture", false, [] {
            if (open) open("files avatar");
        }, "add_a_photo");

        auto nameRow = row(you, "badge", "Name", "Enter to save");
        auto* name = nameRow.row->add<TextField>();
        name->fixedWidth = 260;
        name->setText(self != users.end() ? self->realName : "");
        name->onSubmit = [me, report](const std::string& t) {
            if (t.empty() || t.find_first_of(":,\n") != std::string::npos) return;
            asRoot("usermod -c " + shellQuote(t) + " " + shellQuote(me), "", report("Name saved"));
        };

        auto pwRow = row(you, "password", "Password", "Type it twice");
        auto* pw1 = pwRow.row->add<TextField>();
        pw1->setSecret(true)->setPlaceholder("New password");
        pw1->fixedWidth = 170;
        auto* pw2 = pwRow.row->add<TextField>();
        pw2->setSecret(true)->setPlaceholder("Again");
        pw2->fixedWidth = 150;
        auto changePw = [me, pw1, pw2, status, live, report] {
            if (pw1->text().empty() || pw1->text() != pw2->text()) {
                *status = pw1->text().empty() ? "Type a new password" : "The two passwords don't match";
                live->syncSoon();
                return;
            }
            asRoot("chpasswd", me + ":" + pw1->text() + "\n", report("Password changed"));
        };
        pw2->onSubmit = [changePw](const std::string&) { changePw(); };
        pillButton(pwRow.row, "Change", true, changePw);

        // Every profile, and who may administer the system
        auto* all = group(live, "Profiles on this device", "Administrators can install software and change system settings");
        for (const auto& u : users) {
            auto r = row(all, "", u.realName, u.name + (u.name == me ? "  ·  You" : ""));
            r.row->adopt(std::make_unique<AvatarView>(u.home + "/.face", u.realName, 40), 0);
            r.row->add<Label>("Administrator")->setSize(13)->setColor("on_surface_variant", false);
            auto* sw = r.row->add<Switch>();
            sw->setChecked(u.admin, false);
            const std::string who = u.name;
            const bool lastAdmin = u.admin && admins <= 1;
            sw->onToggled = [who, lastAdmin, status, live, report](bool on) {
                // Nobody left to run sudo would lock the system's settings away
                if (!on && lastAdmin) {
                    *status = "There must be at least one administrator";
                    live->syncSoon();
                    return;
                }
                asRoot(std::string("gpasswd ") + (on ? "-a " : "-d ") + shellQuote(who) + " wheel", "",
                       report(on ? who + " is an administrator" : who + " is no longer an administrator"));
            };
        }

        // A new profile
        auto* add = group(live, "Add a profile");
        auto fields = add->add<Flex>(Flex::Direction::Column);
        fields->setPadding({20, 14})->setSpacing(10)->setAlignItems(Align::Stretch);
        auto* full = fields->add<TextField>();
        full->setPlaceholder("Full name");
        auto* user = fields->add<TextField>();
        user->setPlaceholder("Username (lower case: letters, digits, - and _)");
        auto* np1 = fields->add<TextField>();
        np1->setSecret(true)->setPlaceholder("Password");
        auto* np2 = fields->add<TextField>();
        np2->setSecret(true)->setPlaceholder("Password again");
        auto* adminRow = fields->add<Flex>(Flex::Direction::Row);
        adminRow->setSpacing(10);
        auto* adminLabel = adminRow->add<Label>("Administrator");
        adminLabel->setSize(14);
        adminLabel->grow = true;
        auto* adminSw = adminRow->add<Switch>();
        auto* actions = fields->add<Flex>(Flex::Direction::Row);
        actions->setJustify(Align::End);
        pillButton(actions, "Add profile", true, [full, user, np1, np2, adminSw, status, live, report] {
            const std::string u = user->text();
            std::string problem = !validUsername(u) ? "Usernames start with a letter and use lower-case letters, digits, - and _"
                                : getpwnam(u.c_str()) ? "That username is taken"
                                : np1->text().empty() ? "Give the profile a password"
                                : np1->text() != np2->text() ? "The two passwords don't match"
                                : full->text().find_first_of(":,\n") != std::string::npos ? "The name can't contain : or ,"
                                                                                       : "";
            if (!problem.empty()) {
                *status = problem;
                live->syncSoon();
                return;
            }
            // One prompt: the account with its home, then its password from stdin
            const std::string script = "useradd -m -c " + shellQuote(full->text().empty() ? u : full->text()) +
                                       (adminSw->checked() ? " -G wheel " : " ") + shellQuote(u) + " && chpasswd";
            asRoot(script, u + ":" + np1->text() + "\n", report("Profile " + u + " added"));
        }, "person_add");

        if (!status->empty()) caption(live, *status);
    };
    live->sync();
}

// ── Accessibility ──

void accessibilityPage(Flex* page, Popup&) {
    // Text and display: larger or bolder text, less motion and transparency
    auto* see = group(page, "Text & display", "Easier to read and to follow");
    choiceRow<double>(see, "format_size", "Text size", "All of the shell's text", "/accessibility/textScale", 1.0,
                      {{1.0, "Default"}, {1.15, "Large"}, {1.3, "Larger"}});
    switchRow(see, "format_bold", "Bold text", "Heavier letters, easier to make out", "/accessibility/boldText", false);
    auto motion = row(see, "animation", "Remove animations", "Panels and pages appear without motion");
    auto* motionSw = motion.row->add<Switch>();
    motionSw->setChecked(Config::get().value<double>("/appearance/animationSpeed", 1.0) == 0, false);
    motionSw->onToggled = [](bool on) {
        Config::get().set("/appearance/animationSpeed", on ? nlohmann::json(0.0) : nlohmann::json(nullptr));
    };
    auto solid = row(see, "opacity", "Reduce transparency", "Solid panels and bar, with no blur behind them");
    auto* solidSw = solid.row->add<Switch>();
    solidSw->setChecked(Config::get().value<double>("/appearance/transparency/panels", 0.72) >= 1 &&
                            !Config::get().value<bool>("/appearance/blur/enabled", true),
                        false);
    solidSw->onToggled = [](bool on) {
        for (const char* key : {"/appearance/transparency/panels", "/appearance/transparency/bar"})
            Config::get().set(key, on ? nlohmann::json(1.0) : nlohmann::json(nullptr));
        Config::get().set("/appearance/blur/enabled", on ? nlohmann::json(false) : nlohmann::json(nullptr));
    };

    // Colour correction: one filter over the whole screen (Hyprland shaders)
    auto* colour = page->add<Live>();
    colour->setSpacing(6);
    colour->sync = [colour] {
        colour->clearChildren();
        auto* g = group(colour, "Colour correction", "A filter over the whole screen, apps included");
        const std::string now = Config::get().value<std::string>("/accessibility/colorFilter", "none");
        const std::tuple<const char*, const char*, const char*, const char*> filters[] = {
            {"none", "invert_colors_off", "Off", "Colours as they are"},
            {"grayscale", "filter_b_and_w", "Grayscale", "Shades of grey only"},
            {"invert", "invert_colors", "Invert colours", "Light becomes dark and dark light"},
            {"protanopia", "palette", "Red-weak (protanopia)", "Shifts reds into colours you can tell apart"},
            {"deuteranopia", "palette", "Green-weak (deuteranopia)", "The most common colour blindness"},
            {"tritanopia", "palette", "Blue-weak (tritanopia)", "Shifts blues and yellows apart"},
            {"dim", "brightness_4", "Extra dim", "Darker than the lowest brightness, for dark rooms"},
        };
        for (const auto& [id, icon, title, sub] : filters) {
            const bool on = now == id;
            auto* b = g->add<Button>(Flex::Direction::Row);
            b->setPadding({20, 12})->setSpacing(16);
            b->pressedRadius = 10;
            b->add<Icon>(icon, 22)->setColor(on ? "primary" : "on_surface_variant", false);
            auto* texts = b->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(title)->setSize(15)->setWeight(on ? 600 : 500);
            texts->add<Label>(sub)->setSize(13)->setColor("on_surface_variant", false);
            if (on) b->add<Icon>("check", 22)->setColor("primary", false);
            const std::string value = id;
            b->onClicked = [value](uint32_t button) {
                if (button != BTN_LEFT) return;
                Config::get().set("/accessibility/colorFilter", value == "none" ? nlohmann::json(nullptr) : nlohmann::json(value));
            };
        }
    };
    colour->conns.push_back(Config::get().changed.connect([colour] { colour->syncSoon(); }));
    colour->sync();

    // The magnifier: Hyprland's zoom around the pointer
    auto* zoom = group(page, "Magnifier", "Zooms in around the pointer. Super+= zooms in, Super+- out, Super+Backspace resets");
    hyprLink(zoom, "zoom_in", "Zoom level and how it follows", "Hyprland's cursor zoom", "hypr-cursor");

    auto* pointer = group(page, "Pointer");
    choiceRow<int>(pointer, "mouse", "Cursor size", "", "/accessibility/cursorSize", 24,
                   {{24, "Default"}, {32, "Large"}, {48, "Larger"}, {64, "Largest"}});
    hyprLink(pointer, "arrow_selector_tool", "More cursor options", "Hiding, warping and hardware cursors", "hypr-cursor");
}

// ── Mouse & touchpad ──

// Touchpad and pointer options are Hyprland's: kept in Hyprland Settings
// only, linked from here
void mousePage(Flex* page, Popup&) {
    auto* g = group(page, "Mouse & touchpad", "Tap to click, natural scrolling, pointer speed and gestures live in Hyprland Settings");
    hyprLink(g, "touch_app", "Touchpad", "Tap to click, natural scrolling, ignore while typing, click with fingers", "hypr-input");
    hyprLink(g, "speed", "Pointer", "Speed and acceleration, for the touchpad and mice", "hypr-input");
    hyprLink(g, "swipe", "Gestures", "Workspace swipes", "hypr-gestures");
    auto r = row(g, "swipe_vertical", "Scrolling in the shell", "Smooth scrolling, speed and touchpad glide");
    pillButton(r.row, "Look & feel", false, [] { settingsPage("style"); }, "open_in_new");
}

// ── System ──

// Asks before something that can't be taken back: a strip under the row,
// saying what will happen, with Cancel and the action in its own colour
void confirmStrip(Node* parent, const std::string& text, const std::string& action, bool destructive, std::function<void()> yes,
                  std::function<void()> no) {
    auto* strip = parent->add<Flex>(Flex::Direction::Row);
    strip->setPadding({20, 12})->setSpacing(10)->setAlignItems(Align::Center);
    strip->setBackground(destructive ? "error_container" : "secondary_container", false);
    strip->add<Icon>(destructive ? "warning" : "help", 22)->setColor(destructive ? "on_error_container" : "on_secondary_container", false);
    auto* label = strip->add<Label>(text);
    label->setSize(14)->setMaxWidth(440)->setMaxLines(3)->setColor(destructive ? "on_error_container" : "on_secondary_container", false);
    label->grow = true;
    pillButton(strip, "Cancel", false, std::move(no));
    auto* go = pillButton(strip, action, true, std::move(yes));
    if (destructive) {
        go->setBackground("error", false);
        for (auto& c : go->children())
            if (auto* l = dynamic_cast<Label*>(c.get())) l->setColor("on_error", false);
    }
}

// Settings backups, newest first, each with its date and time: back up now,
// restore or delete one (each asked first, under its row; a restore saves
// what's there now before it)
void backupRows(Flex* page) {
    auto* head = group(page, "Backups", "Your shell and Hyprland settings, also saved before each update, restore and reset");
    auto now = row(head, "backup", "Back up now", "Saved with today's date and time");
    auto* status = now.subtitle;
    pillButton(now.row, "Back up", true, [status] {
        status->setText("Backing up…");
        Backups::get().create("manual", [status](bool ok) { status->setText(ok ? "Backed up" : "Nothing to back up"); });
    }, "save");
    // The shell's settings as one JSON file, to keep or bring to another computer
    auto ex = row(head, "file_export", "Export settings", "As a JSON file in Downloads");
    auto* exStatus = ex.subtitle;
    pillButton(ex.row, "Export", false, [exStatus] {
        std::error_code ec;
        const std::string dir = paths::home() + "/Downloads";
        std::filesystem::create_directories(dir, ec);
        const std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M", std::localtime(&now));
        const std::string file = dir + "/material3ui-settings-" + stamp + ".json";
        const std::string error = Config::get().exportTo(file);
        exStatus->setText(error.empty() ? "Saved as Downloads/" + std::filesystem::path(file).filename().string() : error);
    }, "download");
    auto im = row(head, "file_open", "Import settings", "From an exported file: the ones now are backed up first");
    pillButton(im.row, "Import", false, [] {
        if (open) open("files settings");
    }, "upload");

    // Which backup is asking, and what for ("restore" or "delete"), and how
    // the last restore went
    struct Ask {
        std::string path, action, result;
    };
    auto ask = std::make_shared<Ask>();
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->sync = [live, ask] {
        live->clearChildren();
        const auto all = Backups::get().list();
        if (!ask->result.empty()) caption(live, ask->result);
        if (all.empty()) {
            caption(live, "No backups yet");
            return;
        }
        auto* g = live->add<Segments>();
        size_t shown = 0;
        for (const auto& e : all) {
            if (++shown > 15) break;
            std::tm tm{};
            localtime_r(&e.time, &tm);
            char when[64];
            std::strftime(when, sizeof when, "%a %-d %b %Y, %H:%M", &tm);
            const std::string why = e.reason == "before-update"    ? "Before an update"
                                  : e.reason == "before-restore" ? "Before a restore"
                                  : e.reason == "before-reset"   ? "Before a reset"
                                  : e.reason == "before-import"  ? "Before an import"
                                                                 : "Made by you";
            const std::string size = std::format("{} KB", std::max<std::uintmax_t>(1, e.size / 1024));
            auto r = row(g, e.reason == "manual" ? "bookmark" : "history", when, why + "  ·  " + size);
            const bool asking = ask->path == e.path;
            auto setAsk = [live, ask, path = e.path](const std::string& action) {
                ask->path = action.empty() ? "" : path;
                ask->action = action;
                ask->result.clear();
                EventLoop::get().defer([live] { live->syncSoon(); });
            };
            pillButton(r.row, "Restore", false, [setAsk] { setAsk("restore"); }, "settings_backup_restore");
            auto* del = r.row->add<Button>();
            pill(del)->setJustify(Align::Center);
            del->fixedWidth = del->fixedHeight = 40;
            del->add<Icon>("delete", 20)->setColor("on_surface_variant", false);
            del->tooltip = "Delete this backup";
            del->onClicked = [setAsk](uint32_t b) {
                if (b == BTN_LEFT) setAsk("delete");
            };
            if (!asking) continue;
            if (ask->action == "restore")
                confirmStrip(g, std::format("Restore your settings from {}? What you have now is saved as a backup first.", when),
                             "Restore", false,
                             [live, ask, e] {
                                 ask->path.clear();
                                 ask->result = "Restoring…";
                                 live->syncSoon();
                                 Backups::get().restore(e, [live, ask](bool ok) {
                                     ask->result = ok ? "Restored. Your settings from before are in the list too."
                                                      : "Not restored: that backup isn't a settings backup, or is damaged. Nothing changed.";
                                     live->syncSoon();
                                 });
                             },
                             [setAsk] { setAsk(""); });
            else
                confirmStrip(g, std::format("Delete the backup from {}? It can't be brought back.", when), "Delete", true,
                             [live, ask, e] {
                                 ask->path.clear();
                                 EventLoop::get().defer([e] { Backups::get().remove(e); });
                             },
                             [setAsk] { setAsk(""); });
        }
    };
    live->watch(Backups::get().changed);
    live->sync();
}

void systemPage(Flex* page, Popup&) {
    auto alive = page->add<AliveGuard>()->alive;
    namespace fs = std::filesystem;
    auto* dt = group(page, "Date & time");
    auto ntp = row(dt, "schedule", "Set time automatically", "From the network");
    auto* ntpSw = ntp.row->add<Switch>();
    m3::run({"timedatectl", "show", "-p", "NTP", "--value"}, [alive, ntpSw](int, const std::string& out) {
        if (*alive) ntpSw->setChecked(out.starts_with("yes"), false);
    });
    ntpSw->onToggled = [](bool on) { m3::run({"timedatectl", "set-ntp", on ? "true" : "false"}, nullptr); };
    // The zone /etc/localtime points into (no wait for timedatectl)
    auto zone = std::make_shared<std::string>("UTC");
    {
        std::error_code ec;
        const std::string target = fs::read_symlink("/etc/localtime", ec).string();
        if (const auto at = target.find("zoneinfo/"); at != std::string::npos) *zone = target.substr(at + 9);
    }
    pickerRow(
        dt, "public", "Time zone", [zone] { return *zone; },
        [](auto done) {
            static std::vector<std::string> zones;
            if (!zones.empty()) return done(zones);
            m3::run({"timedatectl", "list-timezones"}, [done](int, const std::string& out) {
                std::stringstream ss(out);
                for (std::string l; std::getline(ss, l);)
                    if (!l.empty()) zones.push_back(l);
                done(zones);
            });
        },
        [zone](const std::string& z) {
            *zone = z;
            m3::run({"timedatectl", "set-timezone", z}, nullptr);
        },
        {"Search time zones"});

    auto* lang = group(page, "Language & region", "A new language applies after you sign in again");
    auto locale = std::make_shared<std::string>("en_US.UTF-8");
    {
        std::ifstream in("/etc/locale.conf");
        for (std::string l; std::getline(in, l);)
            if (l.starts_with("LANG=")) *locale = l.substr(5);
    }
    pickerRow(
        lang, "language", "Language", [locale] { return *locale; },
        [](auto done) {
            m3::run({"localectl", "list-locales"}, [done](int, const std::string& out) {
                std::vector<std::string> list;
                std::stringstream ss(out);
                for (std::string l; std::getline(ss, l);)
                    if (!l.empty()) list.push_back(l);
                done(list);
            });
        },
        [locale](const std::string& l) {
            *locale = l;
            m3::run({"localectl", "set-locale", "LANG=" + l}, nullptr);
        },
        {"Search languages"});

    auto* more = group(page, "More");
    auto kb = row(more, "keyboard", "Keyboard", "Layouts, typing and shortcuts");
    // The Keyboard page here (re-showing Hyprland Settings' page did
    // nothing unless that app was already open)
    pillButton(kb.row, "Open", false, [] { EventLoop::get().defer([] { settingsPage("keybinds"); }); });
    auto up = row(more, "system_update", "Software & updates", "Updates from the repos, the AUR and Flathub");
    pillButton(up.row, "Open", false, [] {
        EventLoop::get().defer([] {
            reshowPage("packages");
        });
    });

    backupRows(page);

    // Reset: asked first, and the settings are kept as a backup before it
    auto* reset = group(page, "Reset options");
    auto rr = row(reset, "restart_alt", "Reset shell settings", "Everything back to the defaults; a backup of them is made first");
    pillButton(rr.row, "Reset…", false, [reset] {
        if (reset->children().size() > 1) return; // already asking
        EventLoop::get().defer([reset] {
            confirmStrip(reset, "Reset all of the shell's settings to their defaults? They're saved as a backup first (Backups, above).",
                         "Reset", true,
                         [reset] {
                             Backups::get().create("before-reset", [](bool ok) {
                                 // Only once they're safely kept
                                 if (ok) std::ofstream(paths::configFile()) << "{}\n";
                             });
                             EventLoop::get().defer([reset] { reset->remove(reset->children().back().get()); });
                         },
                         [reset] { EventLoop::get().defer([reset] { reset->remove(reset->children().back().get()); }); });
        });
    });
}

// ── Default apps ──
//
// File types go through xdg-mime (~/.config/mimeapps.list). The browser,
// file manager and terminal are also the apps.lua entries Hyprland's binds
// and $TERMINAL / $BROWSER use, so picking one rewrites that file too.

struct DefaultKind {
    const char* icon;
    const char* title;
    std::vector<std::string> mimes; // empty: the terminal, by category
    const char* luaKey;             // apps.lua entry, or null
};

const std::vector<DefaultKind>& defaultKinds() {
    static const std::vector<DefaultKind> kinds = {
        {"language", "Web browser", {"x-scheme-handler/https", "x-scheme-handler/http", "text/html"}, "browser"},
        {"folder", "Files", {"inode/directory"}, "file_manager"},
        {"terminal", "Terminal", {}, "terminal"},
        {"mail", "Email", {"x-scheme-handler/mailto"}, nullptr},
        {"edit_note", "Text editor", {"text/plain"}, nullptr},
        {"image", "Images", {"image/png", "image/jpeg", "image/gif", "image/webp", "image/svg+xml", "image/bmp"}, nullptr},
        {"movie", "Video", {"video/mp4", "video/x-matroska", "video/webm", "video/quicktime", "video/x-msvideo"}, nullptr},
        {"music_note", "Music", {"audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav", "audio/mp4", "audio/aac"}, nullptr},
        {"picture_as_pdf", "PDF documents", {"application/pdf"}, nullptr},
        {"folder_zip", "Archives", {"application/zip", "application/x-tar", "application/x-7z-compressed", "application/vnd.rar"}, nullptr},
    };
    return kinds;
}

bool hasToken(const std::string& list, const std::string& token) {
    return (";" + list + ";").find(";" + token + ";") != std::string::npos;
}

std::vector<const Apps::App*> candidates(const DefaultKind& kind) {
    std::vector<const Apps::App*> out;
    for (const auto& a : Apps::get().all()) {
        const bool fits = kind.mimes.empty() ? hasToken(a.categories, "TerminalEmulator")
                                             : std::ranges::any_of(kind.mimes, [&](const auto& m) { return hasToken(a.mimeTypes, m); });
        if (fits) out.push_back(&a);
    }
    return out;
}

std::string appsLuaFile() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.config") + "/hypr/material3ui/apps.lua";
}

std::string appsLuaValue(const std::string& key) {
    std::ifstream in(appsLuaFile());
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    std::smatch m;
    if (!std::regex_search(text, m, std::regex("\\b" + key + "\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)*)\""))) return "";
    // Unescape \\ and \"
    std::string out;
    const std::string raw = m[1].str();
    for (size_t i = 0; i < raw.size(); ++i) out += raw[i] == '\\' && i + 1 < raw.size() ? raw[++i] : raw[i];
    return out;
}

// Rewrites (or adds) one entry; false when apps.lua is not in use
bool setAppsLuaValue(const std::string& key, const std::string& command) {
    std::ifstream in(appsLuaFile());
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)), {});
    in.close();
    std::string quoted;
    for (char c : command) {
        if (c == '\\' || c == '"') quoted += '\\';
        quoted += c;
    }
    const std::regex entry("(\\b" + key + "\\s*=\\s*)\"(?:[^\"\\\\]|\\\\.)*\"");
    if (std::regex_search(text, entry)) text = std::regex_replace(text, entry, "$1\"" + std::regex_replace(quoted, std::regex("\\$"), "$$$$") + "\"");
    else if (const auto end = text.rfind('}'); end != std::string::npos) text.insert(end, "\t" + key + " = \"" + quoted + "\",\n");
    else return false;
    std::ofstream(appsLuaFile()) << text;
    return true;
}

// The app an apps.lua command launches: by its Exec, or its id
const Apps::App* appForCommand(const std::vector<const Apps::App*>& apps, const std::string& command) {
    if (command.empty()) return nullptr;
    const std::string bin = command.substr(0, command.find(' '));
    const std::string base = bin.substr(bin.rfind('/') + 1);
    for (const auto* a : apps) {
        const std::string exec = a->exec.substr(0, a->exec.find(' '));
        if (a->exec == command || exec == bin || exec.substr(exec.rfind('/') + 1) == base) return a;
    }
    for (const auto* a : apps)
        if (a->id == base + ".desktop") return a;
    return nullptr;
}

// Installed apps: each with its icon, what kind of package it came from,
// Open and Uninstall. Ownership comes from pacman (repo or AUR, by its
// foreign list) and from where Flatpak exports its apps.
void installedApps(Flex* page) {
    struct State {
        std::map<std::string, std::string> owner; // .desktop path → pacman package
        std::set<std::string> foreign;             // AUR and other non-repo packages
        bool loaded = false;
        std::string query, armed;                  // armed: the app whose Uninstall was tapped once
        std::string open;                          // the app whose details show
        struct Info {
            std::string version, size, date, description, data;
            bool loaded = false;
        };
        std::map<std::string, Info> info;          // by .desktop path, read once
        Signal<> changed;
    };
    auto state = std::make_shared<State>();
    auto load = [state] {
        std::string script = "pacman -Qo";
        for (const auto& a : Apps::get().all())
            if (a.path.find("/flatpak/exports/") == std::string::npos) script += " '" + a.path + "'";
        script += " 2>/dev/null; echo '#FOREIGN'; pacman -Qqm";
        m3::run({"sh", "-c", script}, [state](int, const std::string& out) {
            state->owner.clear();
            state->foreign.clear();
            bool foreign = false;
            std::stringstream ss(out);
            for (std::string l; std::getline(ss, l);) {
                if (l == "#FOREIGN") {
                    foreign = true;
                    continue;
                }
                if (foreign) {
                    if (!l.empty()) state->foreign.insert(l);
                    continue;
                }
                // "/usr/share/applications/x.desktop is owned by pkg 1.0-1"
                const auto at = l.find(" is owned by ");
                if (at == std::string::npos) continue;
                std::stringstream rest(l.substr(at + 13));
                std::string pkg;
                rest >> pkg;
                state->owner[l.substr(0, at)] = pkg;
            }
            state->loaded = true;
            state->changed.emit();
        });
    };
    load();

    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->watch(state->changed);
    live->watch(Packages::get().changed); // an uninstall finishing
    live->conns.push_back(Apps::get().changed.connect([load] { load(); })); // apps added or removed
    auto* head = group(live, "Installed apps");
    auto* searchRow = head->add<Flex>(Flex::Direction::Row);
    searchRow->setPadding({16, 10})->setSpacing(10);
    auto* field = searchRow->add<TextField>();
    field->setPlaceholder("Search apps");
    field->grow = true;
    auto* list = live->add<Live>(); // rebuilt on its own, so the search field keeps focus
    list->setSpacing(6);
    field->onChanged = [state, list](const std::string& t) {
        state->query = t;
        list->syncSoon();
    };
    list->watch(state->changed);
    list->watch(Packages::get().changed);
    list->conns.push_back(Apps::get().changed.connect([list] { list->syncSoon(); }));
    list->sync = [list, state] {
        list->clearChildren();
        auto* g = list->add<Segments>();
        if (!state->loaded) {
            row(g, "hourglass_empty", "Reading the package database…");
            return;
        }
        auto lower = [](std::string t) {
            for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return t;
        };
        const std::string q = lower(state->query);
        const std::string home = paths::home();
        size_t shown = 0;
        for (const auto& app : Apps::get().all()) {
            // What it is, and how it would go
            enum class Kind { Repo, Aur, Flatpak, Local, Other } kind;
            std::string pkg;
            std::error_code ec;
            const std::string real = std::filesystem::canonical(app.path, ec).string();
            if (app.path.find("/flatpak/") != std::string::npos || real.find("/flatpak/") != std::string::npos ||
                app.exec.find("flatpak run") != std::string::npos) {
                kind = Kind::Flatpak;
                pkg = app.id.ends_with(".desktop") ? app.id.substr(0, app.id.size() - 8) : app.id;
            } else if (auto it = state->owner.find(app.path); it != state->owner.end()) {
                pkg = it->second;
                kind = state->foreign.contains(pkg) ? Kind::Aur : Kind::Repo;
            } else {
                kind = app.path.starts_with(home) ? Kind::Local : Kind::Other;
            }
            const char* type = kind == Kind::Repo ? "Official" : kind == Kind::Aur ? "AUR" : kind == Kind::Flatpak ? "Flatpak"
                             : kind == Kind::Local ? "Local shortcut" : "Installed manually";
            if (!q.empty() && lower(app.name).find(q) == std::string::npos && lower(pkg).find(q) == std::string::npos &&
                lower(type).find(q) == std::string::npos)
                continue;
            ++shown;
            // The row itself opens the app's details (its buttons act alone)
            const bool expanded = state->open == app.path;
            auto* rowButton = g->add<Button>(Flex::Direction::Row);
            rowButton->setPadding({20, 12})->setSpacing(16);
            rowButton->minHeight = 60;
            rowButton->pressedRadius = 10;
            const std::string icon = app.icon;
            rowButton->add<ImageView>(32, [icon](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().icon(icon, static_cast<int>(std::lround(32 * scale)));
                if (!t) return 0;
                w = t->width;
                h = t->height;
                return t->id;
            });
            auto* texts = rowButton->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->add<Label>(app.name)->setSize(15)->setWeight(500)->setMaxWidth(420);
            texts->add<Label>(pkg.empty() ? type : std::string(type) + "  ·  " + pkg)
                ->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(440);
            Row r{rowButton, nullptr, nullptr};
            const std::string appPath = app.path;
            rowButton->onClicked = [state, list, appPath](uint32_t button) {
                if (button != BTN_LEFT) return;
                state->open = state->open == appPath ? "" : appPath;
                list->syncSoon();
            };
            pillButton(r.row, "Open", false, [app] { Apps::get().launch(app); }, "open_in_new");
            // The chevron ends the row, after Uninstall
            auto chevron = [&] { r.row->add<Icon>(expanded ? "expand_less" : "expand_more", 20)->setColor("on_surface_variant", false); };

            // App info: version, installed size, its data in your home, and more
            if (expanded) {
                auto& inf = state->info[app.path];
                if (!inf.loaded) {
                    inf.loaded = true;
                    inf.size = "Counting…";
                    // Its data and cache in the home folder, by the names it is likely to use
                    std::set<std::string> names = {lower(pkg), lower(app.wmClass)};
                    std::string id = app.id.ends_with(".desktop") ? app.id.substr(0, app.id.size() - 8) : app.id;
                    names.insert(id);
                    names.insert(lower(id.substr(id.rfind('.') + 1)));
                    std::string dirs;
                    for (const auto& n : names)
                        if (!n.empty())
                            for (const char* base : {"/.config/", "/.local/share/", "/.cache/"}) dirs += " '" + home + base + n + "'";
                    if (kind == Kind::Flatpak) dirs += " '" + home + "/.var/app/" + pkg + "'";
                    const std::string path = app.path;
                    std::string script = "echo '#DATA'; du -scb" + dirs + " 2>/dev/null | tail -1; echo '#INFO'; ";
                    if (kind == Kind::Flatpak) script += "flatpak info '" + pkg + "' 2>/dev/null";
                    else if (!pkg.empty()) script += "LC_ALL=C pacman -Qi '" + pkg + "' 2>/dev/null";
                    m3::run({"sh", "-c", script}, [state, path, flatpak = kind == Kind::Flatpak](int, const std::string& out) {
                        auto& i = state->info[path];
                        i.size.clear();
                        bool infoPart = false;
                        std::stringstream ss(out);
                        for (std::string l; std::getline(ss, l);) {
                            if (l == "#INFO") {
                                infoPart = true;
                                continue;
                            }
                            if (l == "#DATA") continue;
                            if (!infoPart) {
                                const uint64_t bytes = std::strtoull(l.c_str(), nullptr, 10);
                                i.data = bytes > 0 ? formatBytes(bytes) : "None";
                                continue;
                            }
                            // "Key : value" (pacman) or "Key: value" (flatpak)
                            const auto colon = l.find(':');
                            if (colon == std::string::npos) continue;
                            auto trim = [](std::string t) {
                                t.erase(0, t.find_first_not_of(' '));
                                while (!t.empty() && t.back() == ' ') t.pop_back();
                                return t;
                            };
                            const std::string key = trim(l.substr(0, colon)), value = trim(l.substr(colon + 1));
                            if (key == "Version") i.version = value;
                            else if (key == "Installed Size" || (flatpak && key == "Installed")) i.size = value;
                            else if (key == "Install Date" || (flatpak && key == "Date")) i.date = value;
                            else if (key == "Description" || (flatpak && key == "Subject")) i.description = value;
                        }
                        state->changed.emit();
                    });
                }
                auto* card = g->add<Flex>(Flex::Direction::Column);
                card->setPadding({68, 6, 20, 14})->setSpacing(6)->setAlignItems(Align::Stretch);
                auto fact = [card](const char* icon, const std::string& key, const std::string& value) {
                    if (value.empty()) return;
                    auto* f = card->add<Flex>(Flex::Direction::Row);
                    f->setSpacing(10);
                    f->add<Icon>(icon, 18)->setColor("on_surface_variant", false);
                    auto* k = f->add<Label>(key);
                    k->setSize(13)->setColor("on_surface_variant", false);
                    k->fixedWidth = 120;
                    auto* v = f->add<Label>(value);
                    v->setSize(13)->setMaxWidth(420)->setMaxLines(3);
                    v->grow = true;
                };
                fact("sell", "Version", inf.version);
                fact("hard_drive", "Installed size", inf.size);
                fact("folder_data", "App data", inf.data);
                fact("calendar_month", "Installed on", inf.date);
                fact("description", "About", inf.description);
                fact("draft", "Entry", app.path);
            }
            if (kind == Kind::Other) {
                chevron();
                continue;
            }
            // Two taps: the first arms it
            const bool armed = state->armed == app.path;
            const std::string label = armed ? "Tap to confirm" : kind == Kind::Local ? "Remove" : "Uninstall";
            auto* b = pillButton(r.row, label, false, {}, "delete");
            b->setBackground(armed ? ColorRef("error") : ColorRef("error_container"), false);
            for (auto& c : b->children()) {
                if (auto* l = dynamic_cast<Label*>(c.get())) l->setColor(armed ? "on_error" : "on_error_container", false);
            }
            const std::string path = app.path;
            b->onClicked = [state, list, armed, kind, pkg, path](uint32_t button) {
                if (button != BTN_LEFT) return;
                if (!armed) {
                    state->armed = path;
                    list->syncSoon();
                    return;
                }
                state->armed.clear();
                if (kind == Kind::Local) {
                    std::error_code ec;
                    std::filesystem::remove(path, ec); // just the shortcut; Apps reloads
                } else {
                    Packages::get().uninstall(kind == Kind::Flatpak ? Packages::Source::Flatpak : Packages::Source::Repo, pkg);
                }
                list->syncSoon();
            };
            chevron();
        }
        if (shown == 0) row(g, "search_off", "No app matches");
        // What an uninstall is doing, or how it ended
        auto& p = Packages::get();
        if (p.busy() || (!p.status().empty() && p.status().find("ninstall") != std::string::npos))
            caption(list, p.status());
    };
    list->sync();
}

void defaultAppsPage(Flex* page, Popup&) {
    struct State {
        std::vector<std::string> current; // desktop id per kind ("" unknown)
        int open = -1;                    // the kind whose choices are showing
        Signal<> changed;
    };
    auto state = std::make_shared<State>();
    const auto& kinds = defaultKinds();
    state->current.resize(kinds.size());

    auto query = [state](size_t i) {
        const auto& kind = defaultKinds()[i];
        if (kind.mimes.empty()) {
            const auto* app = appForCommand(candidates(kind), appsLuaValue(kind.luaKey));
            state->current[i] = app ? app->id : "";
            return;
        }
        m3::run({"xdg-mime", "query", "default", kind.mimes.front()}, [state, i](int code, const std::string& out) {
            std::string id = code == 0 ? out : "";
            while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) id.pop_back();
            state->current[i] = id;
            state->changed.emit();
        });
    };
    for (size_t i = 0; i < kinds.size(); ++i) query(i);

    auto pick = [state, query](size_t i, const Apps::App& app) {
        const auto& kind = defaultKinds()[i];
        state->current[i] = app.id;
        state->open = -1;
        state->changed.emit();
        if (!kind.mimes.empty()) {
            std::vector<std::string> argv = {"xdg-mime", "default", app.id};
            argv.insert(argv.end(), kind.mimes.begin(), kind.mimes.end());
            m3::run(argv, [query, i](int, const std::string&) { query(i); });
        }
        if (kind.luaKey && setAppsLuaValue(kind.luaKey, app.exec)) {
            // Our own launches follow at once; Hyprland's binds and env on reload
            if (std::string(kind.luaKey) == "terminal") setenv("TERMINAL", app.exec.c_str(), 1);
            if (std::string(kind.luaKey) == "browser") setenv("BROWSER", app.exec.c_str(), 1);
            m3::run({"hyprctl", "reload"}, nullptr);
        }
    };

    auto appIcon = [](const std::string& icon, float size) {
        return std::make_unique<ImageView>(size, [icon, size](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().icon(icon, static_cast<int>(std::lround(size * scale)));
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        });
    };

    auto* live = page->add<Live>();
    live->setSpacing(6);
    live->watch(state->changed);
    live->conns.push_back(Apps::get().changed.connect([live] { live->syncSoon(); }));
    live->sync = [live, state, pick, appIcon] {
        live->clearChildren();
        auto* g = group(live, "Default apps", "What opens links, folders and files, and the apps behind Super+B, Super+E and Super+Return");
        const auto& kinds = defaultKinds();
        for (size_t i = 0; i < kinds.size(); ++i) {
            const auto& kind = kinds[i];
            const auto apps = candidates(kind);
            const auto& id = state->current[i];
            const auto found = std::ranges::find(apps, id, &Apps::App::id);
            const Apps::App* current = found != apps.end() ? *found : nullptr;
            if (!current && !id.empty())
                for (const auto& a : Apps::get().all())
                    if (a.id == id) current = &a;
            // Hidden handlers (NoDisplay) are not in the list: their bare id
            const std::string bare = id.ends_with(".desktop") ? id.substr(0, id.size() - 8) : id;
            auto r = row(g, kind.icon, kind.title, current ? current->name : id.empty() ? "Not set" : bare);
            r.subtitle->setVisible(true);
            const bool open = state->open == static_cast<int>(i);
            if (current) r.row->adopt(appIcon(current->icon, 28), 2);
            pillButton(r.row, open ? "Done" : "Change", false, [live, state, i, open] {
                state->open = open ? -1 : static_cast<int>(i);
                live->syncSoon();
            }, open ? "expand_less" : "expand_more");
            if (!open) continue;
            if (apps.empty()) row(g, "", "No installed app says it opens these", "");
            for (const auto* a : apps) {
                auto* b = g->add<Button>(Flex::Direction::Row);
                b->setPadding({56, 10, 20, 10})->setSpacing(14);
                b->adopt(appIcon(a->icon, 28));
                auto* name = b->add<Label>(a->name);
                name->setSize(14)->setWeight(a == current ? 600 : 400);
                name->grow = true;
                if (a == current) b->add<Icon>("check", 20)->setColor("primary", false);
                b->onClicked = [pick, i, app = *a](uint32_t button) {
                    // Rebuilding drops this button: not inside its own click
                    if (button == BTN_LEFT) EventLoop::get().defer([pick, i, app] { pick(i, app); });
                };
            }
        }
    };
    live->sync();
    installedApps(page);
}

// Calendar: your own events, calendars subscribed to by their ICS link,
// and reminders
void calendarPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->sync = [live] {
        live->clearChildren();
        auto& cal = m3::Calendar::get();
        auto* mine = group(live, "Your calendar", "Events you add here, from the dashboard's calendar or below");
        const std::time_t now = std::time(nullptr);
        const auto soon = cal.between(now, now + 30L * 86400);
        size_t own = 0;
        for (const auto& o : soon) own += o.event->source.empty();
        auto r = row(mine, "event", "Coming up", own == 0 ? "Nothing in the next 30 days" : std::format("{} in the next 30 days", own));
        char today[16];
        std::tm tm{};
        localtime_r(&now, &tm);
        std::strftime(today, sizeof today, "%Y-%m-%d", &tm);
        const std::string arg = std::string("event new:") + today;
        pillButton(r.row, "Add event", true, [arg] {
            if (open) open(arg);
        }, "add");

        auto* subs = group(live, "Subscribed calendars",
                           "Google, Outlook, Nextcloud, Proton…: paste the calendar's \"secret address in iCal format\" (an ICS link). "
                           "They refresh every half hour");
        for (const auto& s : cal.sources()) {
            auto sr = row(subs, "calendar_month", s.name, s.error.empty() ? s.url : s.error);
            if (!s.error.empty()) sr.subtitle->setColor("error", false);
            sr.subtitle->setMaxWidth(380);
            auto* sw = sr.row->add<Switch>();
            sw->setChecked(s.enabled, false);
            const std::string id = s.id;
            sw->onToggled = [id](bool on) { m3::Calendar::get().setEnabled(id, on); };
            auto* del = sr.row->add<Button>();
            pill(del)->setJustify(Align::Center);
            del->fixedWidth = del->fixedHeight = 40;
            del->add<Icon>("delete", 20)->setColor("on_surface_variant", false);
            del->tooltip = "Remove this calendar";
            del->onClicked = [id](uint32_t b) {
                if (b == BTN_LEFT) EventLoop::get().defer([id] { m3::Calendar::get().unsubscribe(id); });
            };
        }
        // Adding one: a name and its link
        // Adding one: a name and its link, on a line of their own
        auto* add = subs->add<Flex>(Flex::Direction::Row);
        add->setPadding({16, 12})->setSpacing(10)->setAlignItems(Align::Center);
        add->add<Icon>("add_link", 22)->setColor("on_surface_variant", false);
        auto* nameField = add->add<TextField>();
        nameField->fixedWidth = 150;
        nameField->setPlaceholder("Name");
        auto* urlField = add->add<TextField>();
        urlField->grow = true;
        urlField->setPlaceholder("ICS link: https://… or webcal://…");
        auto subscribe = [nameField, urlField] {
            if (urlField->text().empty()) return;
            const std::string n = nameField->text(), u = urlField->text();
            EventLoop::get().defer([n, u] { m3::Calendar::get().subscribe(n, u); });
        };
        urlField->onSubmit = [subscribe](const std::string&) { subscribe(); };
        pillButton(add, "Add", true, subscribe, "add");
        if (!cal.sources().empty()) {
            auto ref = row(subs, "refresh", cal.refreshing() ? "Refreshing…" : "Refresh now", "");
            ref.subtitle->setVisible(false);
            pillButton(ref.row, "Refresh", false, [] { m3::Calendar::get().refresh(); }, "refresh");
        }

        auto* rem = group(live, "Reminders");
        choiceRow<int>(rem, "notifications", "For new and subscribed events", "A notification before each starts", "/calendar/remind", 10,
                       {{-1, "None"}, {0, "At start"}, {10, "10 min"}, {30, "30 min"}, {60, "1 h"}});
    };
    live->watch(m3::Calendar::get().changed);
    live->sync();
}

void toolsPage(Flex* page, Popup& popup) {
    // Screen sharing: what apps (browsers, calls) see, asked through the portal
    auto* share = group(page, "Screen sharing", "When an app asks to share your screen, a window or a part of it");
    switchRow(share, "screen_share", "Material3UI picker", "Choose a screen, a window or a region in the shell's own dialog",
              "/share/picker", true);
    switchRow(share, "history", "Remember apps by default", "Apps may share again without asking (each time, the picker can change it)",
              "/share/remember", false);
    choiceRow<int>(share, "speed", "Frame rate", "At most", "/share/maxFps", 60, {{30, "30"}, {60, "60"}, {120, "120"}});

    auto* clip = group(page, "Clipboard", "History through cliphist (and wl-clipboard)");
    switchRow(clip, "history", "Keep history", "Starts the wl-paste watchers if nothing else runs them", "/clipboard/history", true,
              [](bool on) {
                  if (on) Clipboard::get().ensureWatchers();
              });
    auto r = row(clip, "content_paste", "Clipboard history");
    pillButton(r.row, "Clear", false, [] { Clipboard::get().wipe(); }, "delete_sweep");
    pillButton(r.row, "Open", true, [&popup] {
        popup.dismiss();
        if (open) open("clipboard");
    });

    auto* shots = group(page, "Screenshots", "grim and slurp; edited in satty or swappy");
    textRow(shots, "folder", "Save to", "/screenshot/directory", "~/Pictures/Screenshots");
    switchRow(shots, "content_copy", "Copy to the clipboard", "", "/screenshot/copy", true);
    switchRow(shots, "edit", "Open in the editor", "", "/screenshot/edit", false);
    switchRow(shots, "notifications", "Notify when saved", "", "/screenshot/notify", true);
    auto take = row(shots, "screenshot_region", "Take a screenshot", "Also: material3ui msg screenshot region|window|screen");
    pillButton(take.row, "Open tool", true, [&popup] {
        popup.dismiss();
        if (open) open("screenshot");
    }, "photo_camera");
}

void weatherPage(Flex* page, Popup&) {
    auto* g = group(page, "Weather", "From Open-Meteo, refreshed every 15 minutes");
    switchRow(g, "partly_cloudy_day", "Show weather", "On the bar, the dashboard and the lock screen", "/weather/enabled", true,
              [](bool on) {
                  if (on) Weather::get().refresh();
              });
    choiceRow<std::string>(g, "thermostat", "Units", "", "/weather/units", "metric", {{"metric", "°C · km/h"}, {"imperial", "°F · mph"}});

    // Location: detected, or searched for
    auto* loc = page->add<Live>();
    loc->setSpacing(6);
    auto query = std::make_shared<std::string>();
    auto results = std::make_shared<std::vector<Weather::Place>>();
    loc->sync = [loc, query, results] {
        loc->clearChildren();
        auto* g = group(loc, "Location");
        const auto place = Config::get().value<nlohmann::json>("/weather/location", nlohmann::json());
        auto current = row(g, "location_on", place.is_object() ? place.value("name", "Unknown") : "Not set",
                           place.is_object() ? std::format("{:.2f}, {:.2f}", place.value("lat", 0.0), place.value("lon", 0.0))
                                             : "Found from your IP address on first use");
        pillButton(current.row, "Use my location", false, [] { Weather::get().detect(); }, "my_location");
        pillButton(current.row, "Refresh", true, [] { Weather::get().refresh(); }, "refresh");
        auto* fieldRow = g->add<Flex>(Flex::Direction::Row);
        fieldRow->setPadding({16, 12});
        auto* field = fieldRow->add<TextField>();
        field->grow = true;
        field->setPlaceholder("Search for a city");
        field->setText(*query);
        if (!query->empty()) field->focus();
        field->onChanged = [query, results, loc](const std::string& t) {
            *query = t;
            Weather::get().search(t, [query, results, loc, t](std::vector<Weather::Place> found) {
                if (*query != t) return; // a newer search is on its way
                *results = std::move(found);
                loc->syncSoon();
            });
        };
        for (const auto& p : *results) {
            auto r = row(g, "place", p.name, std::format("{:.2f}, {:.2f}", p.lat, p.lon));
            pillButton(r.row, "Use", true, [p, query, results] {
                query->clear();
                results->clear();
                Weather::get().setPlace(p);
            });
        }
    };
    loc->conns.push_back(Config::get().changed.connect([loc] { loc->syncSoon(); }));
    loc->sync();

    // Live conditions
    auto* now = page->add<Live>();
    now->setSpacing(6)->setPadding({0, 10, 0, 0});
    now->sync = [now] {
        now->clearChildren();
        auto& w = Weather::get();
        auto* card = now->add<Flex>(Flex::Direction::Column);
        card->setPadding({24, 20})->setSpacing(14)->setAlignItems(Align::Stretch)->setRadius(Radii(28));
        card->setBackground("primary_container", false);
        if (!w.ready()) {
            card->add<Label>(w.error().empty() ? "Getting the weather…" : w.error())->setSize(16)->setColor("on_primary_container", false);
            return;
        }
        auto* head = card->add<Flex>(Flex::Direction::Row);
        head->setSpacing(18);
        head->add<Icon>(w.icon(), 64)->setFill(true)->setColor("on_primary_container", false);
        auto* texts = head->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(2);
        texts->grow = true;
        texts->add<Label>(w.temp(w.temperature()))->setSize(48)->setWeight(600)->setColor("on_primary_container", false);
        texts->add<Label>(Weather::describe(w.code()) + "  ·  feels like " + w.temp(w.feelsLike()))->setSize(15)->setColor("on_primary_container", false);
        auto* right = head->add<Flex>(Flex::Direction::Column);
        right->setAlignItems(Align::End)->setSpacing(4);
        right->add<Label>(w.place())->setSize(14)->setWeight(600)->setMaxWidth(260)->setColor("on_primary_container", false);
        right->add<Label>(std::format("Humidity {}%  ·  Wind {}", w.humidity(), w.windText()))->setSize(13)->setColor("on_primary_container", false);
        if (w.updated() > 0) {
            const std::time_t t = static_cast<std::time_t>(w.updated());
            std::tm tm{};
            localtime_r(&t, &tm);
            char buf[32];
            std::strftime(buf, sizeof buf, "Updated %H:%M", &tm);
            right->add<Label>(buf)->setSize(12)->setColor("on_primary_container", false);
        }
        // The week, as tonal tiles
        auto* week = card->add<Flex>(Flex::Direction::Row);
        week->setSpacing(6)->setAlignItems(Align::Stretch);
        for (size_t i = 0; i < w.days().size(); ++i) {
            const auto& d = w.days()[i];
            std::tm tm{};
            strptime(d.date.c_str(), "%Y-%m-%d", &tm);
            char day[16];
            std::strftime(day, sizeof day, "%a", &tm);
            auto* tile = week->add<Flex>(Flex::Direction::Column);
            tile->setSpacing(4)->setPadding({4, 10})->setRadius(Radii(18))->setBackground(ColorRef("surface", 0.35f), false);
            tile->grow = true;
            tile->add<Label>(i == 0 ? std::string("Today") : std::string(day))->setSize(13)->setWeight(600)->setColor("on_primary_container", false);
            tile->add<Icon>(Weather::icon(d.code, true), 24)->setColor("on_primary_container", false);
            tile->add<Label>(w.temp(d.max))->setSize(14)->setWeight(500)->setColor("on_primary_container", false);
            tile->add<Label>(w.temp(d.min))->setSize(13)->setColor("on_primary_container", false);
        }
        if (!w.error().empty()) card->add<Label>(w.error())->setSize(12)->setColor("on_primary_container", false);
    };
    now->watch(Weather::get().changed);
    now->sync();
}

void lockScreenPage(Flex* page, Popup& popup) {
    auto* clock = group(page, "Clock", "Place it on the board below; the login screen shows the same");
    choiceRow<std::string>(clock, "schedule", "Style", "", "/lock/clock/style", "stacked",
                           {{"stacked", "Stacked"}, {"twotone", "Two-tone"}, {"inline", "One line"}, {"analog", "Analog"},
                            {"dots", "Minimal analog"}});
    choiceRow<int>(clock, "format_bold", "Weight", "", "/lock/clock/weight", 400, {{300, "Light"}, {400, "Regular"}, {600, "Bold"}});
    sliderRow(clock, "format_size", "Size", "/lock/clock/size", 150, 72, 200, 4, [](double v) { return std::format("{:.0f} px", v); });
    auto scaleText = [](double v) { return std::format("{:.0f}%", v * 100); };
    auto* lockText = group(page, "Text & widget size", "Also in Look & feel, beside the shell's and the bar's");
    sliderRow(lockText, "lock", "Lock & login screen", "/appearance/textScale/lock", 1.0, 0.7, 1.6, 0.05, scaleText);
    sliderRow(lockText, "widgets", "Widget size", "/appearance/textScale/widgets", 1.0, 0.6, 1.8, 0.05, scaleText);

    // Layout: a board of the lock screen's grid over the wallpaper
    auto* live = page->add<Live>();
    live->setSpacing(6);
    auto sel = std::make_shared<std::pair<int, int>>(-1, -1); // selected cell (col, row)
    live->sync = [live, sel] {
        live->clearChildren();
        using namespace lockgrid;
        auto items = load();
        auto at = [&](int c, int r) -> Item* {
            for (auto& it : items)
                if (it.col == c && it.row == r) return &it;
            return nullptr;
        };
        const char* colName[] = {"left", "centre", "right"};
        const char* rowName[] = {"Top", "Upper", "Lower", "Bottom"};

        auto* g = group(live, "Layout", "The lock and login screens, as a grid: click a cell to add a widget, or a widget to move or remove it");
        auto full = row(g, "fullscreen", "Edit on the lock screen", "The real screen at full size, with your widgets live");
        pillButton(full.row, "Open editor", true, [] {
            if (open) open("lockeditor");
        }, "edit");
        auto* holder = g->add<Flex>(Flex::Direction::Column);
        holder->setAlignItems(Align::Stretch)->setSpacing(12)->setPadding({16, 16});

        // What the selection allows
        auto* tools = holder->add<Flex>(Flex::Direction::Column);
        tools->setAlignItems(Align::Stretch)->setSpacing(10);
        const int c = sel->first, r = sel->second;
        if (c < 0) {
            caption(tools, "Select a cell on the board below.");
        } else if (Item* it = at(c, r)) {
            auto* bar = tools->add<Flex>(Flex::Direction::Row);
            bar->setSpacing(8);
            const auto* d = find(it->id);
            auto* name = bar->add<Label>(std::format("{}  ·  {} {}", d->name, rowName[r], colName[c]));
            name->setSize(14)->setWeight(600);
            name->grow = true;
            // Moving onto a taken cell swaps the two
            auto move = [items, c, r, sel](int dc, int dr) mutable {
                const int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) return;
                for (auto& o : items)
                    if (o.col == nc && o.row == nr) {
                        o.col = c;
                        o.row = r;
                    } else if (o.col == c && o.row == r) {
                        o.col = nc;
                        o.row = nr;
                    }
                *sel = {nc, nr};
                save(items);
            };
            if (c > 0) iconButton(bar, "arrow_back", [move]() mutable { move(-1, 0); });
            if (r > 0) iconButton(bar, "arrow_upward", [move]() mutable { move(0, -1); });
            if (r < kRows - 1) iconButton(bar, "arrow_downward", [move]() mutable { move(0, 1); });
            if (c < kCols - 1) iconButton(bar, "arrow_forward", [move]() mutable { move(1, 0); });
            pillButton(bar, "Remove", false, [items, c, r, sel]() mutable {
                std::erase_if(items, [&](const Item& o) { return o.col == c && o.row == r; });
                *sel = {-1, -1};
                save(items);
            }, "delete");
        } else {
            auto* l = tools->add<Label>(std::format("Add to {} {}", rowName[r], colName[c]));
            l->setSize(14)->setWeight(600);
            l->alignSelf = Align::Start;
            auto* gallery = tools->add<Flow>();
            size_t free = 0;
            for (const auto& d : widgets()) {
                if (std::ranges::any_of(items, [&](const Item& o) { return o.id == d.id; })) continue;
                ++free;
                auto* b = gallery->add<Button>(Flex::Direction::Column);
                b->setBackground("tertiary_container", false)->setRadius(Radii(18))->setPadding({12, 10})->setSpacing(2);
                b->setAlignItems(Align::Start);
                b->pressedRadius = 10;
                b->fixedWidth = 160;
                auto* top = b->add<Flex>(Flex::Direction::Row);
                top->setSpacing(6);
                top->add<Icon>(d.icon, 18)->setColor("on_tertiary_container", false);
                top->add<Label>(d.name)->setSize(13)->setWeight(600)->setColor("on_tertiary_container", false);
                b->add<Label>(d.about)->setSize(12)->setMaxWidth(136)->setColor("on_tertiary_container", false);
                const std::string id = d.id;
                b->onClicked = [items, id, c, r, sel](uint32_t) mutable {
                    items.push_back({id, c, r});
                    *sel = {c, r};
                    save(items);
                };
            }
            if (free == 0) caption(gallery, "Every widget is placed. Remove one to put it here.");
        }
        // Wallpaper under the cells, like the real screen
        auto* board = holder->add<Node>();
        board->fixedHeight = 4 * 70 + 3 * 6 + 24;
        const std::string wp = Wallpaper::get().lockPath();
        auto* bg = board->add<ImageView>(10, 10, [wp](float scale, int& w, int& h) -> GLuint {
            if (wp.empty()) return 0;
            auto* t = Images::get().picture(wp, static_cast<int>(700 * scale), static_cast<int>(320 * scale), true);
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        });
        bg->cover = true;
        auto* scrim = board->add<Flex>(Flex::Direction::Column);
        scrim->setBackground(ColorRef("surface", 0.55f), false)->setRadius(Radii(20))->setPadding(Insets(12))->setSpacing(6);
        scrim->setAlignItems(Align::Stretch);
        for (int r = 0; r < kRows; ++r) {
            auto* rowFlex = scrim->add<Flex>(Flex::Direction::Row);
            rowFlex->setSpacing(6)->setAlignItems(Align::Stretch)->setJustify(Align::Center);
            for (int c = 0; c < kCols; ++c) {
                const Item* it = at(c, r);
                const bool selected = sel->first == c && sel->second == r;
                auto* cell = rowFlex->add<Button>(Flex::Direction::Row);
                cell->fixedWidth = 208; // equal thirds of the board
                cell->fixedHeight = 70;
                cell->setRadius(Radii(16))->setSpacing(8)->setJustify(Align::Center);
                cell->pressedRadius = 8;
                if (it) {
                    const auto* d = find(it->id);
                    cell->setBackground(selected ? "primary" : "secondary_container", false);
                    cell->add<Icon>(d->icon, 22)->setFill(true)->setColor(selected ? "on_primary" : "on_secondary_container", false);
                    cell->add<Label>(d->name)->setSize(14)->setWeight(600)->setColor(selected ? "on_primary" : "on_secondary_container", false);
                } else {
                    cell->setBackground(selected ? ColorRef("primary", 0.25f) : ColorRef("surface_container_highest", 0.35f), false);
                    cell->setBorder(selected ? 2 : 1, selected ? ColorRef("primary") : ColorRef("outline_variant"));
                    cell->add<Icon>("add", 22)->setColor(selected ? "primary" : "on_surface_variant", false);
                }
                cell->onClicked = [sel, live, c, r](uint32_t) {
                    *sel = sel->first == c && sel->second == r ? std::pair{-1, -1} : std::pair{c, r};
                    live->syncSoon();
                };
            }
        }

        auto* foot = holder->add<Flex>(Flex::Direction::Row);
        foot->setJustify(Align::End);
        pillButton(foot, "Default layout", false, [sel] {
            *sel = {-1, -1};
            Config::get().set("/lock/grid", nullptr);
            Config::get().set("/lock/widgets", nullptr);
        }, "restart_alt");
    };
    live->conns.push_back(Config::get().changed.connect([live] { live->syncSoon(); }));
    live->sync();

    auto* login = group(page, "Login screen", "Material3UI's greeter for greetd");
    const bool installed = access("/usr/share/material3ui-greeter/greet-run.sh", F_OK) == 0;
    switchRow(login, "sync", "Match the lock screen", "Copies the clock, widgets, weather, wallpaper and colours when they change",
              "/lock/syncGreeter", true);
    if (installed) {
        auto status = row(login, "palette", "Sync appearance", "Copies your look to the login screen now");
        pillButton(status.row, "Sync now", false, [] {
            m3::run({"material3ui-greeter", "sync"}, [](int code, const std::string& out) {
                // Older installs colour their output for a terminal: keep the words
                std::string text = std::regex_replace(out, std::regex("\x1b\\[[0-9;]*m|│|✓|✗"), "");
                text = std::regex_replace(text, std::regex("^\\s+|\\s+$"), "");
                m3::run({"notify-send", "-a", "Settings", code == 0 ? "Login screen synced" : "Login screen sync failed", text}, nullptr);
            });
        }, "sync");
    }
    // Its files come from the checkout: compare, then install them with root
    const std::string tool = Packages::get().installDir() + "/greeter/material3ui-greeter";
    if (access(tool.c_str(), X_OK) == 0) {
        auto update = row(login, installed ? "system_update_alt" : "login",
                          installed ? "Login screen updates" : "Use the Material3UI login screen",
                          installed ? "Checking…" : "Replaces your display manager with greetd at the next boot");
        auto* holder = update.row->add<Flex>(Flex::Direction::Row);
        auto* sub = update.subtitle;
        const auto alive = holder->add<AliveGuard>()->alive;
        auto act = [tool, installed, sub, alive] {
            sub->setText(installed ? "Updating…" : "Installing…");
            // How it ended, once it has
            auto conn = std::make_shared<Connection>();
            *conn = Packages::get().changed.connect([alive, sub, conn] {
                if (Packages::get().busy() || !*alive) return;
                sub->setText(Packages::get().status());
                conn->disconnect();
            });
            // The package brings greetd with it
            Packages::get().runAsRoot(installed ? "Updating the login screen…" : "Installing the login screen…",
                                      "'" + tool + (installed ? "' install" : "' install --enable"),
                                      installed ? "Login screen updated" : "Login screen installed: it starts at the next boot");
        };
        if (!installed) {
            pillButton(holder, "Install", true, act, "download");
        } else {
            m3::run({tool, "outdated"}, [alive, holder, sub, act](int code, const std::string&) {
                if (!*alive) return;
                const bool outdated = code == 0;
                sub->setText(outdated ? "A newer version came with the last shell update" : "Up to date with the installed shell");
                pillButton(holder, outdated ? "Update" : "Reinstall", outdated, act, outdated ? "system_update_alt" : "refresh");
            });
        }
    }
    auto preview = row(login, "visibility", "Preview", "Locks the screen now: unlock with your password");
    pillButton(preview.row, "Lock now", true, [&popup] {
        // The app stays open behind it; a popup makes way
        if (!popup.isWindow()) popup.close();
        if (lock) lock();
    }, "lock");
}

void notificationsPage(Flex* page, Popup&) {
    auto* g = group(page, "Notifications");
    auto dnd = row(g, "do_not_disturb_on", "Do not disturb", "Only critical notifications show while this is on");
    auto* sw = dnd.row->add<Switch>();
    sw->setChecked(Notifications::get().doNotDisturb(), false);
    sw->onToggled = [](bool on) { Notifications::get().setDoNotDisturb(on); };
    choiceRow<int>(g, "timer", "Pop-ups stay for", "", "/notifications/timeout", 6, {{3, "3 s"}, {6, "6 s"}, {10, "10 s"}, {0, "Until closed"}});
    // The history: dismissed notifications, by day, in the notification centre
    auto* h = group(page, "Notification history", "Dismissed notifications, kept for 30 days, under the centre's History tab");
    auto keep = row(h, "history", "Keep notification history", std::format("{} kept", Notifications::get().history().size()));
    auto* keepSw = keep.row->add<Switch>();
    keepSw->setChecked(Notifications::get().keepsHistory(), false);
    keepSw->onToggled = [](bool on) { Config::get().set("/notifications/history", on); };
    auto clear = row(h, "delete_sweep", "Clear history", "Forgets every dismissed notification");
    auto* count = keep.subtitle;
    confirmButton(clear.row, "notif-history", "delete", [count] {
        Notifications::get().clearHistory();
        count->setText("0 kept");
    });
    auto daemon = row(g, "info", Notifications::get().active() ? "Material3UI is your notification daemon"
                                                              : "Another notification daemon is running",
                      Notifications::get().active() ? "" : "Stop it (mako, dunst, swaync…) and restart the shell");
    (void)daemon;
}

void lockPage(Flex* page, Popup&) {
    auto* g = group(page, "Lock & idle");
    const std::vector<std::pair<int, std::string>> times = {{0, "Never"}, {60, "1 min"}, {300, "5 min"}, {600, "10 min"}, {1800, "30 min"}};
    choiceRow<int>(g, "lock_clock", "Lock after", "", "/idle/lockAfter", 600, times);
    choiceRow<int>(g, "monitor", "Screen off after", "", "/idle/screenOffAfter", 900,
                   {{0, "Never"}, {120, "2 min"}, {600, "10 min"}, {900, "15 min"}, {3600, "1 hour"}});
    switchRow(g, "bedtime", "Lock before sleep", "", "/lock/lockBeforeSleep", true);
    switchRow(g, "wallpaper", "Separate lock-screen wallpaper", "Uses lock.wallpaperPath", "/lock/useSeparateWallpaper", false);
    auto test = row(g, "lock", "Lock now");
    pillButton(test.row, "Lock", true, [] {
        if (lock) lock();
    });
}

// Key capture: click, then press a key
// Modifier chips plus the key, with live conflict warnings. Pressing the
// shortcut (KeyCapture) fills in both; the chips still toggle by hand.
struct ComboEditor {
    Flex* root;
    std::shared_ptr<std::array<bool, 4>> mods;
    std::shared_ptr<std::string> key;
    std::string combo() const {
        static const char* names[] = {"SUPER", "CTRL", "ALT", "SHIFT"};
        std::string c;
        for (int i = 0; i < 4; ++i)
            if ((*mods)[static_cast<size_t>(i)]) c += std::string(names[i]) + " + ";
        return c + *key;
    }
};

ComboEditor comboEditor(Node* parent, const std::string& initial, const std::string& exceptId) {
    auto* col = parent->add<Flex>(Flex::Direction::Column);
    col->setAlignItems(Align::Stretch)->setSpacing(8)->setPadding({20, 6});
    auto* rowFlex = col->add<Flex>(Flex::Direction::Row);
    rowFlex->setSpacing(6);
    auto mods = std::make_shared<std::array<bool, 4>>();
    auto key = std::make_shared<std::string>();
    static const char* names[] = {"SUPER", "CTRL", "ALT", "SHIFT"};
    static const char* labels[] = {"Super", "Ctrl", "Alt", "Shift"};
    // Splits a combo into the chips and the key
    auto load = [mods, key](const std::string& combo) {
        const std::string norm = Keybinds::normalize(combo);
        const auto cut = norm.rfind(" + ");
        *key = norm.substr(cut == std::string::npos ? 0 : cut + 3);
        for (int i = 0; i < 4; ++i) (*mods)[static_cast<size_t>(i)] = norm.find(names[i]) != std::string::npos;
        for (const char* n : names)
            if (*key == n) key->clear();
    };
    load(initial.empty() ? "SUPER + " : initial);
    auto* warn = col->add<Label>();
    warn->setSize(12)->setColor("error", false)->setMaxWidth(600);
    ComboEditor ed{col, mods, key};
    auto refresh = [warn, ed, exceptId] {
        const auto hits = ed.key->empty() ? std::vector<std::string>{} : Keybinds::get().conflictsFor(ed.combo(), exceptId);
        std::string text;
        for (const auto& h : hits) text += (text.empty() ? "" : ", ") + h;
        warn->setText(hits.empty() ? "" : "Also used by: " + text + ". Both will run.");
        warn->setVisible(!hits.empty());
    };
    auto paints = std::make_shared<std::vector<std::function<void()>>>();
    for (int i = 0; i < 4; ++i) {
        auto* chip = rowFlex->add<Button>();
        pill(chip)->setPadding({14, 8});
        auto* l = chip->add<Label>(labels[i]);
        l->setSize(13)->setWeight(500);
        auto paint = [chip, l, mods, i] {
            const bool on = (*mods)[static_cast<size_t>(i)];
            chip->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_highest"));
            l->setColor(on ? "on_secondary_container" : "on_surface");
        };
        paint();
        paints->push_back(paint);
        chip->onClicked = [mods, i, paint, refresh](uint32_t) {
            (*mods)[static_cast<size_t>(i)] = !(*mods)[static_cast<size_t>(i)];
            paint();
            refresh();
        };
    }
    rowFlex->add<Label>("+")->setSize(15)->setColor("on_surface_variant", false);
    auto* capture = rowFlex->add<KeyCapture>(initial);
    capture->grow = true;
    capture->onCaptured = [load, paints, refresh](const std::string& combo) {
        load(combo);
        for (auto& p : *paints) p();
        refresh();
    };
    refresh();
    return ed;
}

// What a custom bind can do. `lua` takes one "%s" for its argument; `arg`
// says which: none, a command, a direction, a workspace or a name
struct BindAction {
    const char* group;
    const char* label;
    const char* lua; // "" runs a command
    enum Arg { None, Command, Direction, Workspace, Name } arg = None;
    bool mouse = false;
};
const std::vector<BindAction>& bindActions() {
    static const std::vector<BindAction> all = {
        {"Apps", "Run a command", "", BindAction::Command},
        {"Windows", "Close the window", "hl.dsp.window.close()"},
        {"Windows", "Force-quit the window", "hl.dsp.window.kill()"},
        {"Windows", "Fullscreen", "hl.dsp.window.fullscreen(\"fullscreen\")"},
        {"Windows", "Maximize", "hl.dsp.window.fullscreen(\"maximized\")"},
        {"Windows", "Float or tile", "hl.dsp.window.float({ action = \"toggle\" })"},
        {"Windows", "Pin on every workspace", "hl.dsp.window.pin()"},
        {"Windows", "Centre the window", "hl.dsp.window.center()"},
        {"Windows", "Pseudo-tile", "hl.dsp.window.pseudo({ action = \"toggle\" })"},
        {"Windows", "Switch the split", "hl.dsp.layout(\"togglesplit\")"},
        {"Windows", "Next window", "hl.dsp.window.cycle_next()"},
        {"Focus", "Focus in a direction", "hl.dsp.focus({ direction = \"%s\" })", BindAction::Direction},
        {"Focus", "Focus the urgent or last window", "hl.dsp.focus({ urgent_or_last = true })"},
        {"Focus", "Focus the next monitor", "hl.dsp.focus({ monitor = \"+1\" })"},
        {"Move", "Move the window in a direction", "hl.dsp.window.move({ direction = \"%s\" })", BindAction::Direction},
        {"Move", "Swap with the window in a direction", "hl.dsp.window.swap({ direction = \"%s\" })", BindAction::Direction},
        {"Move", "Move the window to a workspace", "hl.dsp.window.move({ workspace = \"%s\" })", BindAction::Workspace},
        {"Move", "Move the window to the next monitor", "hl.dsp.window.move({ monitor = \"+1\" })"},
        {"Workspaces", "Go to a workspace", "hl.dsp.focus({ workspace = \"%s\" })", BindAction::Workspace},
        {"Workspaces", "Next workspace", "hl.dsp.focus({ workspace = \"e+1\" })"},
        {"Workspaces", "Previous workspace", "hl.dsp.focus({ workspace = \"e-1\" })"},
        {"Workspaces", "Show or hide a special workspace", "hl.dsp.workspace.toggle_special(\"%s\")", BindAction::Name},
        {"Workspaces", "Send the window to a special workspace", "hl.dsp.window.move({ workspace = \"special:%s\" })", BindAction::Name},
        {"Groups", "Group or ungroup", "hl.dsp.group.toggle()"},
        {"Groups", "Next tab in the group", "hl.dsp.group.next()"},
        {"Mouse", "Drag the window", "hl.dsp.window.drag()", BindAction::None, true},
        {"Mouse", "Resize the window", "hl.dsp.window.resize()", BindAction::None, true},
        {"Session", "Turn the screens off or on", "hl.dsp.dpms(\"toggle\")"},
        {"Session", "Leave Hyprland", "hl.dsp.exit()"},
    };
    return all;
}

// The new-bind form, kept while the page rebuilds around it
struct BindForm {
    size_t action = 0;
    std::string name, arg = "left", mouseButton = "mouse:272";
};

void keybindsPage(Flex* page, Popup&) {
    auto& kb = Keybinds::get();
    if (!kb.supported()) {
        auto* g = group(page, "Keybinds");
        row(g, "info", "The keybind editor needs Hyprland's Lua config (0.55+)", "Edit binds in your hyprland.conf instead");
        return;
    }
    auto* live = page->add<Live>();
    auto editing = std::make_shared<std::string>(); // id being edited, "+" for a new custom bind
    auto form = std::make_shared<BindForm>();
    live->sync = [live, editing, form] {
        live->clearChildren();
        auto& kb = Keybinds::get();
        auto* intro = live->add<Label>("Click a bind to change its keys. Changes apply at once and survive updates.");
        intro->setSize(13)->setColor("on_surface_variant", false);
        intro->alignSelf = Align::Start;

        std::vector<std::string> groups;
        for (const auto& b : kb.binds())
            if (std::ranges::find(groups, b.group) == groups.end()) groups.push_back(b.group);
        for (const auto& name : groups) {
            auto* g = group(live, name);
            for (const auto& b : kb.binds()) {
                if (b.group != name) continue;
                auto* r = g->add<Button>(Flex::Direction::Row);
                r->setPadding({20, 10})->setSpacing(12);
                r->minHeight = 52;
                auto* t = r->add<Label>(b.action);
                t->setSize(15);
                t->grow = true;
                if (b.conflict) r->add<Icon>("warning", 18)->setColor("error", false);
                if (b.changed) r->add<Icon>("edit", 16)->setColor("primary", false);
                r->add<Label>(Keybinds::pretty(b.combo))->setSize(13)->setWeight(500)->setColor("on_surface_variant", false);
                const auto bind = b;
                r->onClicked = [live, editing, bind](uint32_t) {
                    *editing = *editing == bind.id ? "" : bind.id;
                    live->syncSoon();
                };
                if (*editing != b.id) continue;
                auto ed = comboEditor(g, b.combo, b.id);
                auto* actions = ed.root->add<Flex>(Flex::Direction::Row);
                actions->setSpacing(8)->setJustify(Align::End);
                pillButton(actions, "Disable", false, [bind, editing] {
                    editing->clear();
                    Keybinds::get().disable(bind.id, bind.label);
                });
                if (b.changed) pillButton(actions, "Reset", false, [bind, editing] {
                    editing->clear();
                    Keybinds::get().reset(bind.id);
                });
                pillButton(actions, "Save", true, [ed, bind, editing] {
                    if (ed.key->empty()) return;
                    editing->clear();
                    Keybinds::get().remap(bind.id, ed.combo(), bind.label);
                });
            }
        }

        const auto disabled = kb.disabled();
        if (!disabled.empty()) {
            auto* g = group(live, "Disabled");
            for (const auto& d : disabled) {
                auto r = row(g, "block", d.action, d.group);
                pillButton(r.row, "Enable", false, [id = d.id] { Keybinds::get().reset(id); });
            }
        }

        auto* custom = group(live, "Your binds", "Any action or command, from a key combination or a mouse drag");
        for (size_t i = 0; i < kb.custom().size(); ++i) {
            const auto& c = kb.custom()[i];
            auto r = row(custom, c.mouse ? "mouse" : c.action.empty() ? "terminal" : "bolt", c.label,
                         (c.action.empty() ? c.command : std::string()) + (c.action.empty() ? "  ·  " : "") + Keybinds::pretty(c.keys));
            confirmButton(r.row, "bind:" + c.keys, "delete", [i] { Keybinds::get().removeCustom(i); });
        }
        if (*editing == "+") {
            const auto& actions = bindActions();
            const BindAction& act = actions[form->action];
            auto* box = custom->add<Flex>(Flex::Direction::Column);
            box->setAlignItems(Align::Stretch)->setSpacing(10)->setPadding({20, 10});
            auto* name = box->add<TextField>();
            name->setPlaceholder("Name (e.g. Pin the window)");
            name->setText(form->name);
            name->onChanged = [form](const std::string& t) { form->name = t; };

            // The action, in groups
            auto* pick = box->add<Flex>(Flex::Direction::Row);
            pick->setSpacing(10);
            pick->add<Label>("Does")->setSize(14)->setColor("on_surface_variant", false);
            auto* drop = pick->add<Button>(Flex::Direction::Row);
            pill(drop->setBackground("secondary_container", false))->setPadding({14, 8})->setSpacing(6);
            drop->add<Label>(act.label)->setSize(14)->setWeight(500)->setColor("on_secondary_container", false);
            drop->add<Icon>("expand_more", 18)->setColor("on_secondary_container", false);
            auto* menu = box->add<Flex>(Flex::Direction::Column);
            menu->setSpacing(2)->setAlignItems(Align::Stretch)->setVisible(false);
            std::string lastGroup;
            for (size_t i = 0; i < actions.size(); ++i) {
                if (lastGroup != actions[i].group) {
                    lastGroup = actions[i].group;
                    menu->add<Label>(lastGroup)->setSize(12)->setWeight(600)->setColor("primary", false);
                }
                auto* item = menu->add<Button>(Flex::Direction::Row);
                pill(item)->setPadding({14, 7});
                item->add<Label>(actions[i].label)->setSize(14);
                item->onClicked = [form, live, i](uint32_t button) {
                    if (button != BTN_LEFT) return;
                    form->action = i;
                    const auto kind = bindActions()[i].arg;
                    form->arg = kind == BindAction::Direction ? "left" : kind == BindAction::Workspace ? "1" : kind == BindAction::Name ? "magic" : "";
                    live->syncSoon();
                };
            }
            drop->onClicked = [menu](uint32_t button) {
                if (button == BTN_LEFT) menu->setVisible(!menu->visible());
            };

            // Its argument
            if (act.arg == BindAction::Direction) {
                connectedGroup<std::string>(box, form->arg, {{"left", "Left"}, {"right", "Right"}, {"up", "Up"}, {"down", "Down"}},
                                            [form](std::string v) { form->arg = v; });
            } else if (act.arg != BindAction::None) {
                auto* f = box->add<TextField>();
                f->setPlaceholder(act.arg == BindAction::Command ? "Command (e.g. code)" : act.arg == BindAction::Workspace ? "Workspace (e.g. 3)" : "Name (e.g. magic)");
                f->setText(form->arg);
                f->onChanged = [form](const std::string& t) { form->arg = t; };
            }

            // The keys, or for a drag, the modifiers and a mouse button
            auto ed = comboEditor(box, act.mouse ? "SUPER + " + form->mouseButton : "", "");
            if (act.mouse) {
                connectedGroup<std::string>(box, form->mouseButton,
                                            {{"mouse:272", "Left button"}, {"mouse:273", "Right button"}, {"mouse:274", "Middle button"}},
                                            [form, ed](std::string v) {
                                                form->mouseButton = v;
                                                *ed.key = v;
                                            });
            }
            auto* buttons = box->add<Flex>(Flex::Direction::Row);
            buttons->setSpacing(8)->setJustify(Align::End);
            pillButton(buttons, "Cancel", false, [editing, live] {
                editing->clear();
                live->syncSoon();
            });
            pillButton(buttons, "Add", true, [ed, form, editing] {
                const BindAction& a = bindActions()[form->action];
                if (ed.key->empty() || (a.arg != BindAction::None && form->arg.empty())) return;
                std::string lua = a.lua;
                if (const auto at = lua.find("%s"); at != std::string::npos) lua.replace(at, 2, form->arg);
                const std::string label = form->name.empty() ? a.label : form->name;
                editing->clear();
                if (a.arg == BindAction::Command) Keybinds::get().addCustom(ed.combo(), form->arg, label);
                else Keybinds::get().addCustom(ed.combo(), "", label, lua, a.mouse);
                *form = BindForm{};
            });
        } else {
            auto r = row(custom, "add", "Add a bind", "Press the keys you want; Hyprland's own binds pause while you do");
            pillButton(r.row, "Add", true, [editing, live] {
                *editing = "+";
                live->syncSoon();
            });
        }

        if (!kb.others().empty()) {
            auto* g = group(live, "Other binds", "From user.lua or other configs; edit them there");
            for (const auto& o : kb.others()) {
                auto r = row(g, "", o.label);
                r.title->setSize(13)->setColor("on_surface_variant", false);
                r.row->add<Label>(Keybinds::pretty(o.combo))->setSize(13)->setColor("on_surface_variant", false);
            }
        }
        auto* footer = live->add<Flex>(Flex::Direction::Row);
        footer->setJustify(Align::End);
        pillButton(footer, "Reset all to defaults", false, [] { Keybinds::get().resetAll(); }, "restart_alt");
    };
    live->watch(kb.changed);
    kb.refresh();
}

void packagesPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    auto tab = std::make_shared<int>(0); // 0 updates, 1 official, 2 AUR, 3 Flatpak
    auto* tabs = live->add<Flex>(Flex::Direction::Row);
    tabs->setSpacing(6);
    auto* body = live->add<Flex>(Flex::Direction::Column);
    body->setAlignItems(Align::Stretch)->setSpacing(12);
    auto query = std::make_shared<std::string>();
    const char* names[] = {"Updates", "Official", "AUR", "Flatpak"};

    std::function<void()> rebuild = [=] {
        tabs->clearChildren();
        for (int i = 0; i < 4; ++i) {
            auto* b = tabs->add<Button>();
            pill(b)->setPadding({16, 8});
            b->setBackground(i == *tab ? "primary" : "surface_container_high", false);
            b->add<Label>(names[i])->setSize(14)->setWeight(500)->setColor(i == *tab ? "on_primary" : "on_surface", false);
            b->onClicked = [=](uint32_t) {
                *tab = i;
                query->clear();
                if (i > 0) Packages::get().search(static_cast<Packages::Source>(i - 1), "");
                live->syncSoon();
            };
        }
        body->clearChildren();
        auto& p = Packages::get();
        if (*tab == 0) {
            // An update waiting on a restart: what to restart, and why
            p.checkRestart();
            if (const auto& rs = p.restart(); rs.any() && !p.busy()) {
                auto* banner = body->add<Flex>(Flex::Direction::Column);
                banner->setPadding(Insets(20))->setSpacing(12)->setRadius(Radii(24))->setBackground("tertiary_container", false);
                banner->setAlignItems(Align::Stretch);
                auto* top = banner->add<Flex>(Flex::Direction::Row);
                top->setSpacing(16)->setAlignItems(Align::Center);
                top->add<Icon>("restart_alt", 32)->setColor("on_tertiary_container", false);
                auto* texts = top->add<Flex>(Flex::Direction::Column);
                texts->setSpacing(2)->setAlignItems(Align::Start);
                texts->grow = true;
                texts->add<Label>(rs.system ? "Restart to finish updating" : rs.session ? "Log out to finish updating"
                                                                                        : "Restart the shell to finish updating")
                    ->setSize(18)->setWeight(600)->setColor("on_tertiary_container", false);
                std::string why;
                for (const auto& w : rs.why) why += (why.empty() ? "" : ", ") + w;
                texts->add<Label>("Updated while running: " + why)
                    ->setSize(13)->setMaxWidth(560)->setMaxLines(2)->setColor("on_tertiary_container", false);
                auto* actions = banner->add<Flex>(Flex::Direction::Row);
                actions->setSpacing(8)->setJustify(Align::End);
                if (rs.shell && !rs.system)
                    pillButton(actions, "Restart shell", !rs.session, [] {
                        spawn("setsid -f sh -c 'pkill -x material3ui; sleep 0.5; exec material3ui'");
                    }, "refresh");
                if (rs.session && !rs.system) pillButton(actions, "Log out", true, [] { Session::get().logout(); }, "logout");
                if (rs.system) pillButton(actions, "Restart now", true, [] { spawn("systemctl reboot"); }, "restart_alt");
            }
            // The status on top, the actions on their own row below it: side
            // by side they were wider than the page and spilled past the card
            auto* hero = body->add<Flex>(Flex::Direction::Column);
            hero->setPadding(Insets(20))->setSpacing(14)->setRadius(Radii(24))->setBackground("surface_container_high", false);
            hero->setAlignItems(Align::Stretch);
            auto* top = hero->add<Flex>(Flex::Direction::Row);
            top->setSpacing(16);
            // Working: M3 Expressive's wavy loader in the icon's place
            const bool working = p.checking() || p.busy();
            if (working)
                top->add<kit::WavyLoader>(36);
            else
                top->add<Icon>(p.updates().empty() ? "verified" : "system_update_alt", 36)->setColor("primary", false);
            auto* texts = top->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(2);
            texts->grow = true;
            const bool pending = !p.updates().empty() && !p.checking() && p.error().empty();
            const std::string status = p.busy() ? p.status()
                                     : p.checking() ? "Checking for updates…"
                                     : !p.error().empty() ? p.error()
                                     : p.lastChecked() == 0 ? "Not checked yet"
                                     : p.updates().empty() ? "Your system is up to date"
                                     : std::format("{} update{}", p.updates().size(), p.updates().size() == 1 ? "" : "s");
            texts->add<Label>(status)->setSize(18)->setWeight(600)->setMaxWidth(560)->setMaxLines(2);
            // The breakdown and the helper on one quieter line
            std::string detail = pending ? std::format("{} official · {} AUR · {} Flatpak", p.count(Packages::Source::Repo),
                                                       p.count(Packages::Source::Aur), p.count(Packages::Source::Flatpak))
                                         : "";
            if (pending && p.count(Packages::Source::Shell) > 0) detail += " · shell update";
            if (!p.aurHelper().empty()) detail += (detail.empty() ? "" : "  ·  ") + std::string("AUR helper: ") + p.aurHelper();
            if (!detail.empty()) texts->add<Label>(detail)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(560);
            // How the last update or install ended
            if (!p.busy() && !p.status().empty())
                texts->add<Label>(p.status())->setSize(13)->setColor(p.status().starts_with("Didn't") ? "error" : "primary", false)
                    ->setMaxWidth(560)->setMaxLines(2);
            if (working) hero->add<kit::WavyBar>();
            auto* actions = hero->add<Flex>(Flex::Direction::Row);
            actions->setSpacing(8)->setJustify(Align::End);
            actions->setVisible(!p.busy()); // one task at a time
            // Pacman questions a background run can't answer: the old way
            if (p.status().starts_with("Didn't"))
                pillButton(actions, "Use a terminal", false, [] { Packages::get().upgradeInTerminal(); }, "terminal");
            if (!p.checking()) pillButton(actions, "Check now", false, [] { Packages::get().check(); }, "refresh");
            if (!p.updates().empty()) pillButton(actions, "Update now", true, [] { Packages::get().upgrade(); }, "download");
            if (!p.updates().empty()) {
                // Grouped by where they come from; each shows the installed
                // version and the new one
                using Src = Packages::Source;
                const std::pair<Src, const char*> sources[] = {
                    {Src::Shell, "Material3UI"}, {Src::Repo, "Official"}, {Src::Aur, "AUR"}, {Src::Flatpak, "Flatpak"}};
                for (const auto& [source, name] : sources) {
                    const int n = p.count(source);
                    if (n == 0) continue;
                    auto* list = group(body, std::format("{} · {} update{}", name, n, n == 1 ? "" : "s"), "Installed version → new version");
                    for (const auto& u : p.updates()) {
                        if (u.source != source) continue;
                        const bool shell = source == Src::Shell;
                        auto r = row(list, shell ? "" : source == Src::Flatpak ? "apps" : "deployed_code", u.name);
                        auto* versions = r.row->add<Flex>(Flex::Direction::Row);
                        versions->setSpacing(8)->setAlignItems(Align::Center);
                        versions->add<Label>(u.from.empty() ? "—" : u.from)->setSize(13)->setMaxWidth(170)->setColor("on_surface_variant", false);
                        versions->add<Icon>("arrow_forward", 16)->setColor("on_surface_variant", false);
                        auto* to = versions->add<Flex>(Flex::Direction::Row);
                        pill(to->setBackground("primary_container", false))->setPadding({10, 4});
                        to->add<Label>(u.to)->setSize(13)->setWeight(600)->setMaxWidth(170)->setColor("on_primary_container", false);
                        // Material3UI's own update wears its logo
                        if (shell)
                            r.row->adopt(std::make_unique<ImageView>(24, [](float scale, int& w, int& h) -> GLuint {
                                             auto* t = Images::get().icon(paths::dataFile("icons/Material3UI.svg"),
                                                                          static_cast<int>(std::lround(24 * scale)));
                                             if (!t) return 0;
                                             w = t->width;
                                             h = t->height;
                                             return t->id;
                                         }),
                                         0);
                    }
                }
            }
            // How the shell itself updates
            auto* shellGroup = group(body, "Material3UI updates");
            auto ch = row(shellGroup, "conversion_path", "Update channel",
                          p.channel() == "stable" ? "Releases only: tested versions, when they come out"
                                                  : "The newest commits on main: early, less tested");
            connectedGroup<std::string>(ch.row, p.channel() == "stable" ? "stable" : "canary",
                                        {{"stable", "Stable"}, {"canary", "Latest (git)"}},
                                        [](std::string c) { Packages::get().setChannel(c); });
            auto bk = row(shellGroup, "backup", "Settings backups", "One is made before each update; restore them in System");
            pillButton(bk.row, "Backups", false, [] { settingsPage("system"); }, "settings_backup_restore");
            // This version's notes (all of them: About → Changelog)
            group(body, "What's new in " M3_VERSION, "Material3UI's latest changes, as installed");
            releaseNotes(body, 1, 600);
            return;
        }
        auto* field = body->add<TextField>();
        field->setPlaceholder(*tab == 1 ? "Search the official repositories" : *tab == 2 ? "Search the AUR" : "Search Flathub");
        field->setText(*query);
        const auto source = static_cast<Packages::Source>(*tab - 1);
        field->onChanged = [query, source](const std::string& t) {
            *query = t;
            Packages::get().search(source, t);
        };
        field->focus();
        if (p.searching()) caption(body, "Searching…");
        auto* list = body->add<Flex>(Flex::Direction::Column);
        list->setAlignItems(Align::Stretch)->setSpacing(2);
        for (const auto& r : p.results()) {
            auto rr = row(list, r.source == Packages::Source::Flatpak ? "apps" : "deployed_code",
                          std::format("{}  {}", r.name, r.version), r.description);
            rr.row->add<Label>(r.repo)->setSize(12)->setWeight(600)->setColor("primary", false);
            if (r.installed) {
                auto* badge = rr.row->add<Flex>(Flex::Direction::Row);
                pill(badge->setBackground("secondary_container", false))->setPadding({12, 6})->setSpacing(4);
                badge->add<Icon>("check", 16)->setColor("on_secondary_container", false);
                badge->add<Label>("Installed")->setSize(12)->setColor("on_secondary_container", false);
            } else {
                pillButton(rr.row, "Install", true, [r] { Packages::get().install(r); }, "download");
            }
        }
    };
    // The search field keeps focus while results stream in: rebuild only the
    // list on result changes, the tabs and field on tab changes
    live->sync = rebuild;
    live->watch(Packages::get().changed);
    rebuild();
    if (Packages::get().lastChecked() == 0) Packages::get().check();
}

void aboutPage(Flex* page, Popup&) {
    auto* hero = page->add<Flex>(Flex::Direction::Column);
    hero->setSpacing(10)->setPadding({0, 12});
    const std::string logo = paths::dataFile("icons/Material3UI.svg");
    hero->add<ImageView>(112, [logo](float scale, int& w, int& h) -> GLuint {
        if (logo.empty()) return 0;
        auto* t = Images::get().icon(logo, static_cast<int>(std::lround(112 * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
    hero->add<Label>("Material3UI")->setSize(28)->setWeight(600);
    hero->add<Label>("Native shell " M3_VERSION " · C++, Wayland and OpenGL ES")->setSize(14)->setColor("on_surface_variant", false);
    auto* intro = hero->add<Flex>(Flex::Direction::Row);
    intro->setSpacing(8)->setPadding({0, 4, 0, 0});
    pillButton(intro, "What's new", false, [] {
        if (open) open("whatsnew");
    }, "new_releases");
    pillButton(intro, "Welcome tour", false, [] {
        if (open) open("welcome");
    }, "waving_hand");

    auto* sys = group(page, "This device");
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    utsname u{};
    uname(&u);
    std::string os = readFirstLine("/etc/os-release", "PRETTY_NAME=");
    if (os.size() >= 2 && os.front() == '"') os = os.substr(1, os.size() - 2);
    std::string cpu = readFirstLine("/proc/cpuinfo", "model name");
    if (auto c = cpu.find(':'); c != std::string::npos) cpu = cpu.substr(c + 2);
    std::string hypr = "Not running";
    if (auto v = Hyprland::get().requestJson("version")) hypr = v->value("tag", v->value("version", "Hyprland"));
    auto& stats = SystemStats::get();
    auto lease = stats.lease(); // one sample for memory and uptime
    row(sys, "computer", host, "Device name");
    // The distribution's logo (LOGO= in os-release, in pixmaps or the icon
    // theme) and Hyprland's, in place of the glyphs, where there is one
    auto useLogo = [](const Row& r, const std::string& nameOrPath) {
        r.row->remove(r.row->children()[0].get());
        // The glyphs' size, so the text lines up with the other rows
        r.row->adopt(std::make_unique<ImageView>(22, [nameOrPath](float scale, int& w, int& h) -> GLuint {
                         auto* t = Images::get().icon(nameOrPath, static_cast<int>(std::lround(22 * scale)));
                         if (!t) return 0;
                         w = t->width;
                         h = t->height;
                         return t->id;
                     }),
                     0);
    };
    auto osRow = row(sys, "deployed_code", os.empty() ? "Linux" : os, std::string("Kernel ") + u.release);
    if (std::string mark = readFirstLine("/etc/os-release", "LOGO="); !mark.empty()) {
        if (mark.size() >= 2 && mark.front() == '"') mark = mark.substr(1, mark.size() - 2);
        for (const char* ext : {".svg", ".png"})
            if (std::filesystem::exists("/usr/share/pixmaps/" + mark + ext)) {
                mark = "/usr/share/pixmaps/" + mark + ext;
                break;
            }
        if (Images::get().icon(mark, 22)) useLogo(osRow, mark);
    }
    row(sys, "memory", cpu.empty() ? "CPU" : cpu, std::format("{} logical cores", stats.cores()));
    row(sys, "memory_alt", formatBytes(stats.memTotal()), "Memory");
    auto wm = row(sys, "grid_view", "Hyprland " + hypr, "Compositor");
    if (const auto mark = paths::dataFile("icons/hyprland-logo.svg"); !mark.empty()) useLogo(wm, mark);

    // The changelog: the latest few releases, the rest on request
    group(page, "Changelog", "What changed in each release");
    auto* notes = page->add<Flex>(Flex::Direction::Column);
    notes->setAlignItems(Align::Stretch)->setSpacing(10);
    releaseNotes(notes, 3, 600);
    auto* more = page->add<Flex>(Flex::Direction::Row);
    more->setJustify(Align::Center);
    pillButton(more, "Show all releases", false, [notes, more] {
        EventLoop::get().defer([notes, more] {
            notes->clearChildren();
            releaseNotes(notes, 1000, 600);
            more->setVisible(false);
        });
    }, "expand_more");

    auto* credits = group(page, "Credits");
    row(credits, "favorite", "NAnDoroid-shell by na-ive", "The project Material3UI grew from");
    row(credits, "architecture", "Noctalia", "The native shell follows its architecture (MIT); no code is shared");
    row(credits, "auto_awesome", "end-4, DankMaterialShell, Ambxst, pC, P3DROVFX, Vynx", "Design and interaction references");
}

// Settings' Keyboard entry: shortcuts are Hyprland's, in Hyprland Settings
void keyboardPage(Flex* page, Popup&) {
    auto* typing = group(page, "Layouts & typing", "Keyboard layouts, switching between them, repeat rate and delay");
    auto t = row(typing, "keyboard_alt", "Layouts & typing", "In Hyprland Settings → Input devices");
    pillButton(t.row, "Open", true, [] {
        if (open) open("hyprsettings hypr-input");
    }, "open_in_new");
    auto* g = group(page, "Keyboard shortcuts", "Remap the shell's shortcuts and add your own: actions, commands and mouse drags");
    auto r = row(g, "keyboard_command_key", "Keybinds", "In Hyprland Settings → Keybinds");
    pillButton(r.row, "Open", true, [] {
        if (open) open("hyprsettings hypr-keybinds");
    }, "open_in_new");
}

// Android 16's settings icons: Material Symbols in a tinted circle, each
// category its own colour, as Google's (sampled from Android 16's Settings:
// pastel behind a deep glyph of the same hue). Dark themes swap the pair:
// the deep shade behind, the pastel glyph on it.
struct HueChip {
    Color chip, glyph;
};
HueChip categoryColours(const std::string& id) {
    struct Pair {
        const char* pastel;
        const char* deep;
    };
    static const Pair blue{"#bae6fa", "#004a63"}, apps{"#cddff6", "#1a4a8a"}, pink{"#f6d8ee", "#7a1e5a"},
        orange{"#fad8bc", "#6a3a0a"}, yellow{"#fcde7a", "#5c4300"}, green{"#c0e4ba", "#0c4c23"}, grey{"#dedede", "#424242"},
        teal{"#a8eafa", "#00505f"}, red{"#fcd2d2", "#6a1e1e"}, purple{"#ecdcfc", "#4c258e"};
    static const std::vector<std::pair<std::vector<std::string>, const Pair*>> table = {
        {{"network", "vpn", "bluetooth", "printers", "weather", "hypr-windows", "hypr-layouts", "hypr-groups"}, &blue},
        {{"defaults", "hypr-input"}, &apps},
        {{"notifications", "sound", "hypr-animations"}, &pink},
        {{"display", "appearance", "style", "bar", "dock", "hypr-decoration", "hypr-monitors"}, &orange},
        {{"storage", "monitor", "calendar", "hypr-render"}, &yellow},
        {{"battery", "packages", "wellbeing", "hypr-startup"}, &green},
        {{"lock", "firewall", "lockscreen", "users", "hypr-window-rules", "hypr-layer-rules", "hypr-workspace-rules", "hypr-profiles"}, &teal},
        {{"hypr-debug"}, &red},
        {{"accessibility", "tools", "hypr-plugins"}, &purple},
    };
    const Pair* pair = &grey; // the rest: system, about, keyboard, mouse…
    for (const auto& [ids, p] : table)
        if (std::ranges::find(ids, id) != ids.end()) pair = p;
    const Color surface = Theme::get().color("surface");
    const bool dark = 0.2126f * surface.r + 0.7152f * surface.g + 0.0722f * surface.b < 0.5f;
    Color pastel = *Color::fromHex(pair->pastel), deep = *Color::fromHex(pair->deep);
    // Themed (Look & feel → Themed icons), as Android harmonises colours with
    // the wallpaper: each hue turned towards the palette's, half the way and
    // at most 15° (Material's harmonising), so categories stay apart but all
    // belong to it
    if (Config::get().value<bool>("/appearance/themedIcons", true)) {
        auto toHsl = [](const Color& c, float& h, float& s, float& l) {
            const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
            l = (mx + mn) / 2;
            const float d = mx - mn;
            s = d == 0 ? 0 : d / (1 - std::abs(2 * l - 1));
            if (d == 0) h = 0;
            else if (mx == c.r) h = 60 * std::fmod((c.g - c.b) / d + 6, 6.0f);
            else if (mx == c.g) h = 60 * ((c.b - c.r) / d + 2);
            else h = 60 * ((c.r - c.g) / d + 4);
        };
        auto fromHsl = [](float h, float s, float l) {
            auto f = [&](float n) {
                const float k = std::fmod(n + h / 30.0f, 12.0f), a = s * std::min(l, 1 - l);
                return l - a * std::max(-1.0f, std::min({k - 3, 9 - k, 1.0f}));
            };
            return Color{f(0), f(8), f(4), 1};
        };
        float ph, ps, pl;
        toHsl(Theme::get().color("primary"), ph, ps, pl);
        auto harmonise = [&](Color& c) {
            float h, s, l;
            toHsl(c, h, s, l);
            if (s < 0.05f) { // the greys take on a touch of the palette, as Android's neutrals do
                c = fromHsl(ph, std::min(0.12f, ps), l);
                return;
            }
            float diff = std::fmod(ph - h + 540.0f, 360.0f) - 180.0f;
            const float turn = std::clamp(diff * 0.5f, -15.0f, 15.0f);
            c = fromHsl(std::fmod(h + turn + 360.0f, 360.0f), s, l);
        };
        harmonise(pastel);
        harmonise(deep);
    }
    return dark ? HueChip{deep, pastel} : HueChip{pastel, deep};
}

struct PageDef {
    const char* id;
    const char* icon;
    const char* title;
    const char* blurb;
    void (*build)(Flex*, Popup&);
};

// In Android 17's order; the shell's own pages sit where Android keeps
// their nearest kin
const PageDef kPages[] = {
    {"network", "wifi", "Network & internet", "Wi-Fi, Ethernet and your connection", networkPage},
    {"vpn", "vpn_key", "VPN", "WireGuard, OpenVPN and Cloudflare WARP", vpnPage},
    {"bluetooth", "bluetooth", "Connected devices", "Bluetooth, pairing and devices", bluetoothPage},
    {"printers", "print", "Printers", "Printers nearby, the default and the queue", printersPage},
    {"defaults", "apps", "Apps", "Default apps for links, files and the terminal", defaultAppsPage},
    {"notifications", "notifications", "Notifications", "Do not disturb and pop-ups", notificationsPage},
    {"tools", "construction", "Tools", "Clipboard history and screenshots", toolsPage},
    {"wellbeing", "digital_wellbeing", "Digital Wellbeing", "Screen time, app timers, Focus mode and Bedtime", wellbeingPage},
    {"calendar", "calendar_month", "Calendar", "Your events, subscribed calendars and reminders", calendarPage},
    {"weather", "partly_cloudy_day", "Weather", "Location, units and the forecast", weatherPage},
    {"battery", "battery_full", "Battery", "Charge, power draw, health and the power mode", batteryPage},
    {"storage", "storage", "Storage", "Disks, folders and freeing up space", storagePage},
    {"monitor", "monitoring", "System monitor", "CPU, memory and processes: stop, end or restart them", monitorPage},
    {"sound", "volume_up", "Sound", "Outputs, inputs, balance and apps", soundPage},
    {"display", "brightness_medium", "Display", "Brightness, night light and monitors", displayPage},
    {"appearance", "palette", "Wallpaper & style", "Wallpaper, Material You colours and fonts", appearancePage},
    {"style", "blur_on", "Look & feel", "Blur, transparency, corners and motion", stylePage},
    {"bar", "toolbar", "Status bar", "Position, widgets and the island", barPage},
    {"dock", "dock_to_bottom", "Dock", "Pinned apps, running apps and auto-hide", dockPage},
    {"lockscreen", "lock_person", "Lock screen", "Clock, widgets and the login screen", lockScreenPage},
    {"accessibility", "accessibility_new", "Accessibility", "Text, colour correction, magnifier and motion", accessibilityPage},
    {"lock", "security", "Security & privacy", "Screen lock, idle and sleep", lockPage},
    {"firewall", "shield_lock", "Firewall", "Which devices on the network may connect", firewallPage},
    {"users", "account_circle", "Users", "Your profile, password, administrators and new profiles", usersPage},
    {"system", "settings_suggest", "System", "Date & time, languages and reset", systemPage},
    // Hyprland itself: every compositor option, live
    {"hypr-windows", "space_dashboard", "Windows & borders", "Gaps, borders, their colours and snapping", hyprWindowsPage},
    {"hypr-decoration", "auto_awesome", "Decoration", "Corners, opacity, blur, shadows and glow", hyprDecorationPage},
    {"hypr-monitors", "desktop_windows", "Displays", "Arrangement, resolution, scale, rotation, VRR and HDR", hyprMonitorsPage},
    {"hypr-animations", "animation", "Animations", "Every animation's length, curve and style, and a curve editor", hyprAnimationsPage},
    {"hypr-keybinds", "keyboard_command_key", "Keybinds", "Press a shortcut to set it; any action or command, mouse drags too", keybindsPage},
    {"hypr-window-rules", "rule", "Window rules", "Float, size and place apps' windows; per-app opacity and more", hyprWindowRulesPage},
    {"hypr-layer-rules", "layers", "Layer rules", "Blur and animations for bars, launchers and notifications", hyprLayerRulesPage},
    {"hypr-workspace-rules", "view_carousel", "Workspace rules", "Which monitor each workspace is on, its gaps and layout", hyprWorkspaceRulesPage},
    {"hypr-startup", "rocket_launch", "Startup apps", "Apps and commands to run when Hyprland starts", hyprStartupPage},
    {"hypr-environment", "data_object", "Environment", "Variables for the session's apps", hyprEnvironmentPage},
    {"hypr-layouts", "dashboard", "Layouts", "Dwindle, master and scrolling tiling", hyprLayoutsPage},
    {"hypr-input", "keyboard_alt", "Input devices", "Keyboard layouts, pointer, touchpad, touch and tablets", hyprInputPage},
    {"hypr-gestures", "swipe", "Gestures", "Workspace swipes and the scrolling layout", hyprGesturesPage},
    {"hypr-cursor", "arrow_selector_tool", "Cursor", "Hiding, warping, zoom and hardware cursors", hyprCursorPage},
    {"hypr-groups", "tab_group", "Window groups", "Tabbed groups and the group bar", hyprGroupsPage},
    {"hypr-misc", "tune", "Behaviour", "Focus, swallowing, keybind and XWayland behaviour", hyprMiscPage},
    {"hypr-plugins", "extension", "Plugins", "Loaded plugins, hyprpm and plugin files", hyprPluginsPage},
    {"hypr-profiles", "switch_account", "Profiles", "Save and switch whole setups by name", hyprProfilesPage},
    {"hypr-render", "memory", "Rendering", "Direct scanout, colour management and OpenGL", hyprRenderPage},
    {"hypr-debug", "bug_report", "Debug", "Logs, overlays and damage tracking", hyprDebugPage},
    {"mouse", "touchpad_mouse", "Mouse & touchpad", "Touchpad, pointer and gestures, in Hyprland Settings", mousePage},
    {"keybinds", "keyboard", "Keyboard", "Layouts, typing and shortcuts", keyboardPage},
    {"packages", "system_update", "Software & updates", "Updates from the repos, the AUR and Flathub", packagesPage},
    {"about", "info", "About", "This device and the shell", aboutPage},
};

// Android's grouping of the settings list
// The pages each app shows: Hyprland's own options in Hyprland Settings,
// everything else in Settings
const std::vector<PageDef>& pagesOf(bool hypr) {
    static const auto pick = [](bool want) {
        std::vector<PageDef> out;
        for (const auto& p : kPages)
            if (std::string_view(p.id).starts_with("hypr-") == want) out.push_back(p);
        return out;
    };
    static const std::vector<PageDef> shell = pick(false), hyprland = pick(true);
    return hypr ? hyprland : shell;
}

int pageGroup(const std::string& id) {
    // Hyprland Settings: how it looks, how windows behave, the internals
    if (id == "hypr-monitors" || id == "hypr-windows" || id == "hypr-decoration" || id == "hypr-animations") return 0;
    if (id == "hypr-layouts" || id == "hypr-input" || id == "hypr-gestures" || id == "hypr-cursor" || id == "hypr-groups")
        return 1;
    if (id == "hypr-keybinds" || id == "hypr-startup" || id == "hypr-environment" || id.ends_with("-rules")) return 3;
    if (id.starts_with("hypr-")) return 2;
    if (id == "network" || id == "vpn" || id == "bluetooth" || id == "printers") return 0;
    if (id == "defaults" || id == "notifications" || id == "wellbeing" || id == "tools" || id == "calendar" || id == "weather") return 1;
    if (id == "battery" || id == "storage" || id == "monitor" || id == "sound" || id == "display") return 2;
    if (id == "appearance" || id == "style" || id == "bar" || id == "dock" || id == "lockscreen") return 3;
    if (id == "accessibility" || id == "lock" || id == "firewall" || id == "users") return 4;
    return 5; // system, keyboard, updates, about
}

} // namespace

void settingsPage(const std::string& id) {
    if (id.starts_with("search:")) {
        if (searchFor[0]) searchFor[0](id.substr(7));
        return;
    }
    const bool hypr = id.starts_with("hypr-");
    if (reshow[hypr]) reshow[hypr](id);
}

void settingsClosed(bool hypr) {
    reshow[hypr] = nullptr; // its pages are gone
    searchFor[hypr] = nullptr;
}

template <bool Hypr>
void settingsApp(Flex* card, Popup& popup, const std::string& initial) {
    // Ctrl+Z steps back through Hyprland option changes
    popup.onKey = [](const KeyEvent& e) {
        if (!e.ctrl() || (e.sym != XKB_KEY_z && e.sym != XKB_KEY_Z) || !HyprOptions::get().canUndo()) return false;
        const std::string name = HyprOptions::get().undo();
        if (!name.empty()) m3::run({"notify-send", "-a", "Settings", "-t", "2500", "Undone", name}, nullptr);
        return true;
    };
    // As an app window it fills the window and follows its size
    const bool window = popup.isWindow();
    card->setPadding(Insets(12))->setSpacing(0);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setAlignItems(Align::Start)->setSpacing(12);

    // Navigation, as Android 17's settings list: a large title, a search
    // field, then the pages in rounded groups, each with its icon in a tonal
    // circle and a one-line summary. Scrolls when the screen is short.
    auto* sideScroll = columns->add<ScrollView>();
    sideScroll->blurEdges = true;
    sideScroll->maxHeight = window ? 1e6f : 680;
    sideScroll->fixedWidth = 280;
    auto* side = sideScroll->add<Flex>(Flex::Direction::Column);
    side->setAlignItems(Align::Stretch)->setSpacing(10)->setPadding({4, 10, 4, 8});
    auto* brand = side->add<Label>(Hypr ? "Hyprland" : "Settings");
    brand->setSize(34)->setWeight(400);
    brand->alignSelf = Align::Start;
    auto* searchBox = side->add<Flex>(Flex::Direction::Row);
    pill(searchBox->setBackground(ColorRef("on_surface", 0.1f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    searchBox->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* search = searchBox->add<TextField>();
    search->setPlaceholder(Hypr ? "Search Hyprland options" : "Search settings");
    search->bare = true;
    search->grow = true;
    search->minWidth = 120;
    // The other app, a window of its own, first under the search: Hyprland's
    // options from Settings, the shell's settings from Hyprland Settings
    auto* other = side->add<Button>(Flex::Direction::Row);
    pill(other->setBackground("secondary_container", false))->setPadding({16, 12, 14, 12})->setSpacing(12);
    other->pressedRadius = 12;
    other->add<Icon>(Hypr ? "settings" : "tune", 20)->setColor("on_secondary_container", false);
    auto* otherTexts = other->add<Flex>(Flex::Direction::Column);
    otherTexts->setAlignItems(Align::Start)->setSpacing(1);
    otherTexts->grow = true;
    otherTexts->add<Label>(Hypr ? "Material3UI Settings" : "Hyprland Settings")
        ->setSize(15)->setWeight(600)->setColor("on_secondary_container", false);
    otherTexts->add<Label>(Hypr ? "The shell: bar, dock, network, sound…" : "Every compositor option, animations and more")
        ->setSize(12)->setColor("on_secondary_container", false)->setMaxWidth(170)->setMaxLines(2);
    other->add<Icon>("open_in_new", 18)->setColor("on_secondary_container", false);
    other->onClicked = [](uint32_t button) {
        if (button == BTN_LEFT && open) open(Hypr ? "settings" : "hyprsettings");
    };

    // The page: a fixed header with the close button, then what scrolls
    auto* right = columns->add<Flex>(Flex::Direction::Column);
    right->setAlignItems(Align::Stretch)->setSpacing(4);
    if (window)
        right->grow = true;
    else
        right->fixedWidth = 740;
    // The page's large title, as Android 17's pages open
    auto* hero = right->add<Flex>(Flex::Direction::Row);
    hero->setSpacing(16)->setPadding({12, 10, 0, 10})->setAlignItems(Align::Start);
    auto* heroTexts = hero->add<Flex>(Flex::Direction::Column);
    heroTexts->setAlignItems(Align::Start)->setSpacing(4);
    heroTexts->grow = true;
    auto* heroTitle = heroTexts->add<Label>();
    heroTitle->setSize(36)->setWeight(400);
    auto* heroBlurb = heroTexts->add<Label>();
    heroBlurb->setSize(14)->setColor("on_surface_variant", false);
    // Close: a tonal round button in the corner
    auto* close = hero->add<Button>();
    pill(close->setBackground("surface_container_highest", false))->setPadding(Insets(10));
    close->pressedRadius = 12;
    close->add<Icon>("close", 22)->setColor("on_surface", false);
    close->onClicked = [&popup](uint32_t button) {
        if (button == BTN_LEFT) popup.close();
    };
    close->setVisible(!window); // the window's title bar has its own

    auto* scroll = right->add<ScrollView>();
    scroll->blurEdges = true;
    scroll->maxHeight = window ? 1e6f : 600;
    auto* content = scroll->add<Flex>(Flex::Direction::Column);
    content->setAlignItems(Align::Stretch)->setSpacing(6)->setPadding({4, 4, 12, 12});

    struct NavItem {
        Button* button;
        Flex* chip; // the icon's round tonal container
        Icon* icon;
        Label* label;
        Label* summary;
        std::string id; // its category colours (categoryColours)
    };
    auto items = std::make_shared<std::vector<NavItem>>();
    auto show = std::make_shared<std::function<void(size_t)>>();
    auto shown = std::make_shared<size_t>(0);
    *show = [content, scroll, items, heroTitle, heroBlurb, sideScroll, side, shown, &popup](size_t index) {
        *shown = index;
        const auto& def = pagesOf(Hypr)[index];
        content->clearChildren();
        heroTitle->setText(def.title);
        heroBlurb->setText(def.blurb);
        def.build(content, popup);
        scroll->scrollTo(0, false);
        // New page: rises into place
        content->setOpacity(0, false);
        content->setTranslateY(24, false);
        content->setOpacity(1);
        content->setTranslateY(0);
        for (size_t i = 0; i < items->size(); ++i) {
            const bool on = i == index;
            auto& it = (*items)[i];
            it.button->setBackground(on ? ColorRef("secondary_container") : ColorRef::none());
            // Android 16's settings list: each icon in its category's
            // tinted circle; the current page's glyph filled
            const auto c = categoryColours(it.id);
            it.chip->setBackground(ColorRef(c.chip));
            it.icon->setFill(on)->setColor(ColorRef(c.glyph));
            it.label->setWeight(on ? 600 : 500)->setColor(on ? "on_secondary_container" : "on_surface", false);
            it.summary->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
        }
        // Keep the active item in view (after the next layout)
        Button* active = (*items)[index].button;
        EventLoop::get().addTimer(30, [sideScroll, side, active] {
            const float y = active->rect().y - side->rect().y;
            sideScroll->scrollTo(y - sideScroll->rect().h / 2 + active->rect().h / 2);
        });
    };
    size_t first = 0;
    // Android's groups: connections, apps, device, style, access, system
    std::vector<Segments*> groups;
    for (int g = 0; g < 6; ++g) {
        auto* seg = side->add<Segments>();
        seg->collapseWhenEmpty = true; // searching can empty it
        groups.push_back(seg);
    }
    for (size_t i = 0; i < pagesOf(Hypr).size(); ++i) {
        auto* b = groups[static_cast<size_t>(pageGroup(pagesOf(Hypr)[i].id))]->add<Button>(Flex::Direction::Row);
        b->setPadding({10, 10, 12, 10})->setSpacing(12);
        b->pressedRadius = 10;
        auto* chip = b->add<Flex>(Flex::Direction::Row);
        pill(chip)->setJustify(Align::Center);
        chip->fixedWidth = chip->fixedHeight = 38;
        auto* icon = chip->add<Icon>(pagesOf(Hypr)[i].icon, 20);
        auto* texts = b->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->grow = true;
        auto* label = texts->add<Label>(pagesOf(Hypr)[i].title);
        label->setSize(15)->setWeight(500)->setMaxWidth(190);
        // Wraps to a second line, as Android's summaries do, rather than
        // cutting off in the narrow sidebar
        auto* summary = texts->add<Label>(pagesOf(Hypr)[i].blurb);
        summary->setSize(12)->setMaxWidth(190)->setMaxLines(2);
        b->onClicked = [show, i](uint32_t) {
            // The page rebuild must not run inside the click that asked for it
            EventLoop::get().defer([show, i] { (*show)(i); });
        };
        items->push_back({b, chip, icon, label, summary, pagesOf(Hypr)[i].id});
        if (initial == pagesOf(Hypr)[i].id) first = i;
    }
    // A new wallpaper (or theme) recolours the icons at once, as Android's do
    auto* recolour = side->add<Live>();
    recolour->sync = [items, shown] {
        for (size_t i = 0; i < items->size(); ++i) {
            auto& it = (*items)[i];
            const auto c = categoryColours(it.id);
            it.chip->setBackground(ColorRef(c.chip));
            it.icon->setFill(i == *shown)->setColor(ColorRef(c.glyph));
        }
    };
    recolour->watch(Theme::get().changed);
    recolour->watch(Config::get().changed);
    // Universal search, as Android's: every setting by name, not just the
    // pages. The index is read from the pages themselves, built out of view
    // once (their row titles and section headings).
    struct Hit {
        std::string text;
        size_t page;
    };
    auto index = std::make_shared<std::vector<Hit>>();
    auto buildIndex = [index, &popup] {
        if (!index->empty()) return;
        for (size_t i = 0; i < pagesOf(Hypr).size(); ++i) {
            Flex scratch(Flex::Direction::Column);
            pagesOf(Hypr)[i].build(&scratch, popup);
            std::set<std::string> seen;
            std::function<void(const Node&)> walk = [&](const Node& n) {
                const auto* l = dynamic_cast<const Label*>(&n);
                if (l && !dynamic_cast<const Icon*>(&n) && l->text().size() > 2 &&
                    ((l->size() == 15 && l->weight() == 500) || (l->size() == 14 && l->weight() == 600)) &&
                    seen.insert(l->text()).second)
                    index->push_back({l->text(), i});
                for (const auto& c : n.children()) walk(*c);
            };
            walk(scratch);
        }
    };
    auto lower = [](std::string t) {
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t;
    };
    // Opens a page and, given a setting, scrolls to it and lights it up
    auto openAt = [show, content, scroll](size_t page, const std::string& setting) {
        EventLoop::get().defer([show, content, scroll, page, setting] {
            (*show)(page);
            if (setting.empty()) return;
            EventLoop::get().addTimer(60, [content, scroll, setting] {
                std::function<Label*(Node&)> find = [&](Node& n) -> Label* {
                    if (auto* l = dynamic_cast<Label*>(&n); l && l->text() == setting) return l;
                    for (auto& c : n.children())
                        if (auto* hit = find(*c)) return hit;
                    return nullptr;
                };
                Label* label = find(*content);
                if (!label) return;
                scroll->scrollTo(label->rect().y - content->rect().y - 40);
                // The row it names, briefly highlighted
                if (auto* row = dynamic_cast<Flex*>(label->parentNode() ? label->parentNode()->parentNode() : nullptr)) {
                    row->setBackground(ColorRef("primary", 0.22f));
                    EventLoop::get().addTimer(1400, [content, row] {
                        // Still on screen? (the page may have changed)
                        std::function<bool(Node&)> has = [&](Node& n) {
                            if (&n == row) return true;
                            for (auto& c : n.children())
                                if (has(*c)) return true;
                            return false;
                        };
                        if (has(*content)) row->setBackground(ColorRef::none());
                    });
                }
            });
        });
    };
    const auto current = shown; // the page to return to when the search clears
    search->onChanged = [=](const std::string& t) {
        const std::string q = lower(t);
        if (q.empty()) {
            for (auto& it : *items) it.button->setVisible(true);
            EventLoop::get().defer([show, current] { (*show)(*current); });
            return;
        }
        buildIndex();
        // Pages, then settings, that match
        std::vector<size_t> pages;
        std::vector<const Hit*> settings;
        std::set<size_t> withHits;
        for (size_t i = 0; i < pagesOf(Hypr).size(); ++i)
            if (lower(pagesOf(Hypr)[i].title).find(q) != std::string::npos || lower(pagesOf(Hypr)[i].blurb).find(q) != std::string::npos) {
                pages.push_back(i);
                withHits.insert(i);
            }
        for (const auto& h : *index)
            if (lower(h.text).find(q) != std::string::npos) {
                settings.push_back(&h);
                withHits.insert(h.page);
            }
        for (size_t i = 0; i < items->size(); ++i) (*items)[i].button->setVisible(withHits.contains(i));
        EventLoop::get().defer([=] {
            content->clearChildren();
            heroTitle->setText("Search");
            heroBlurb->setText(withHits.empty() ? "No results for “" + t + "”"
                                                : std::format("{} result{}", pages.size() + settings.size(),
                                                              pages.size() + settings.size() == 1 ? "" : "s"));
            auto result = [&](Node* g, const std::string& icon, const std::string& title, const std::string& where,
                              size_t page, const std::string& setting) {
                auto* b = g->add<Button>(Flex::Direction::Row);
                b->setPadding({20, 12})->setSpacing(16);
                b->pressedRadius = 10;
                b->add<Icon>(icon, 22)->setColor("on_surface_variant", false);
                auto* texts = b->add<Flex>(Flex::Direction::Column);
                texts->setAlignItems(Align::Start)->setSpacing(1);
                texts->grow = true;
                texts->add<Label>(title)->setSize(15)->setWeight(500)->setMaxWidth(440);
                texts->add<Label>(where)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(440);
                b->onClicked = [openAt, page, setting](uint32_t button) {
                    if (button == BTN_LEFT) openAt(page, setting);
                };
            };
            if (!pages.empty()) {
                auto* g = group(content, "Pages");
                for (size_t i : pages) result(g, pagesOf(Hypr)[i].icon, pagesOf(Hypr)[i].title, pagesOf(Hypr)[i].blurb, i, "");
            }
            if (!settings.empty()) {
                auto* g = group(content, "Settings");
                for (size_t k = 0; k < settings.size() && k < 40; ++k)
                    result(g, pagesOf(Hypr)[settings[k]->page].icon, settings[k]->text, pagesOf(Hypr)[settings[k]->page].title,
                           settings[k]->page, settings[k]->text);
            }
            scroll->scrollTo(0, false);
        });
    };
    searchFor[Hypr] = [search](const std::string& t) {
        search->setText(t);
        if (search->onChanged) search->onChanged(t);
    };
    search->onSubmit = [=](const std::string& t) {
        const std::string q = lower(t);
        if (q.empty()) return;
        buildIndex();
        for (size_t i = 0; i < pagesOf(Hypr).size(); ++i)
            if (lower(pagesOf(Hypr)[i].title).find(q) != std::string::npos) return openAt(i, "");
        for (const auto& h : *index)
            if (lower(h.text).find(q) != std::string::npos) return openAt(h.page, h.text);
    };
    // Only called from inside this popup (a page's reset button)
    reshow[Hypr] = [show](const std::string& id) {
        for (size_t i = 0; i < pagesOf(Hypr).size(); ++i)
            if (id == pagesOf(Hypr)[i].id) (*show)(i);
    };
    (*show)(first);
    if (initial.starts_with("search:")) EventLoop::get().defer([q = initial.substr(7)] { if (searchFor[Hypr]) searchFor[Hypr](q); });
}

void settings(Flex* card, Popup& popup, const std::string& initial) {
    settingsApp<false>(card, popup, initial);
}

void hyprSettings(Flex* card, Popup& popup, const std::string& initial) {
    settingsApp<true>(card, popup, initial);
}

} // namespace m3::shell::panels
