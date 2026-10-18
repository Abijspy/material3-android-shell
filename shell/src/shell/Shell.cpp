#include "shell/Shell.h"

#include "build_config.h"
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "ipc/IpcServer.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "services/Hyprland.h"
#include "services/Audio.h"
#include "services/Clipboard.h"
#include "services/Search.h"
#include "services/Effects.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Tray.h"
#include "services/Idle.h"
#include "services/Polkit.h"
#include "services/Screenshot.h"
#include "services/Session.h"
#include "services/Wallpaper.h"
#include "shell/Desktop.h"
#include "shell/Dock.h"
#include "shell/LockScreen.h"
#include "shell/Notify.h"
#include "shell/Osd.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/bar/Bar.h"
#include "ui/Theme.h"
#include "wayland/Display.h"
#include "wayland/Surface.h"

#include <cmath>
#include <format>
#include <unistd.h>

namespace m3::shell {

Shell::Shell() = default;
Shell::~Shell() = default;

bool Shell::start() {
    auto& display = wl::Display::get();
    if (!display.connect()) return false;
    if (!Renderer::get().init(display.display())) return false;
    if (!IpcServer::get().listen()) return false;
    // Outside Hyprland the bars still run; workspaces just stay empty
    Hyprland::get();
    Effects::get();
    Clipboard::get().ensureWatchers();
    // New apps bring their icons; before anyone redraws with them
    Apps::get().changed.connectForever([] { Images::get().rescan(); });

    display.onOutputReady = [this](wl::Output* o) { addOutput(o); };
    display.onOutputRemoved = [this](wl::Output* o) { removeOutput(o); };
    for (auto& o : display.outputs())
        if (o->ready) addOutput(o.get());

    // Wallpaper colours only need a repaint; config edits may change sizes and fonts
    Theme::get().changed.connectForever([this] { restyle(false); });
    Config::get().changed.connectForever([this] { restyle(true); });

    // Transient surfaces follow the focused output
    auto focused = [this] { return focusedOutput(); };
    m_toasts = std::make_unique<Toasts>(focused);
    m_osd = std::make_unique<Osd>(focused);
    // Locking: IPC, loginctl lock-session, idle timeout, before suspend
    panels::lock = [] { EventLoop::get().defer([] { LockScreen::get().lock(); }); };
    Session::get().lockRequested.connectForever([] { LockScreen::get().lock(); });
    Idle::get().lockRequested.connectForever([] { LockScreen::get().lock(); });
    Idle::get().start();
    LockScreen::get().lockedChanged.connectForever([this] {
        if (LockScreen::get().locked()) m_popup.reset();
    });

    // Polkit: a dialog per request, closed when polkitd is done with it
    Polkit::get().requested.connectForever([this](int id) {
        togglePopup("polkit:" + std::to_string(id), focusedOutput(), -1);
    });
    Polkit::get().finished.connectForever([this](int id) {
        if (m_popup && m_popup->name() == "polkit:" + std::to_string(id)) m_popup->close();
    });
    if (Config::get().value<bool>("/polkit/enabled", true)) Polkit::get().start();

    // "settings dock" opens a panel with an argument (here, a Settings page)
    panels::open = [this](const std::string& what) {
        EventLoop::get().defer([this, what] {
            const auto sp = what.find(' ');
            const std::string name = what.substr(0, sp), arg = sp == std::string::npos ? "" : what.substr(sp + 1);
            togglePopup(name, focusedOutput(), name == "power" ? -1 : 100000, arg);
        });
    };

    // The login screen follows the lock screen: sync it a moment after
    // settings change (a slider drag is many writes)
    Config::get().changed.connectForever([this] {
        if (m_greeterSync) EventLoop::get().cancel(m_greeterSync);
        m_greeterSync = EventLoop::get().addTimer(3000, [this] {
            m_greeterSync = 0;
            if (Config::get().value<bool>("/lock/syncGreeter", true) && access("/var/lib/material3ui-greeter", W_OK) == 0)
                m3::run({"material3ui-greeter", "sync", "--quiet"}, nullptr);
        });
    });

    panels::editBar = [this](bool on) {
        // Not inside the click that asked: rebuilding destroys that button
        EventLoop::get().defer([this, on] {
            Bar::editing = on;
            if (on && m_popup) m_popup->close();
            for (auto& [_, bar] : m_bars) bar->rebuild();
        });
    };

    registerIpc();
    return true;
}

void Shell::addOutput(wl::Output* output) {
    LockScreen::get().addOutput(output); // a monitor plugged in while locked
    if (m_bars.contains(output)) return;
    auto bar = std::make_unique<Bar>(output, [this, output](const std::string& what, float x) {
        const auto sp = what.find(' ');
        togglePopup(what.substr(0, sp), output, x, sp == std::string::npos ? "" : what.substr(sp + 1));
    });
    if (!m_barsVisible) bar->setVisible(false);
    if (Config::get().value<bool>("/desktop/wallpaper", true)) m_desktops[output] = std::make_unique<Desktop>(output);
    m_bars[output] = std::move(bar);
    syncDock(output);
}

void Shell::syncDock(wl::Output* output) {
    // Created after the bar, so a bottom bar's zone is already taken
    if (Config::get().value<std::string>("/dock/mode", "autohide") == "off") m_docks.erase(output);
    else if (auto it = m_docks.find(output); it != m_docks.end()) it->second->rebuild();
    else m_docks[output] = std::make_unique<Dock>(output);
}

void Shell::removeOutput(wl::Output* output) {
    LockScreen::get().removeOutput(output);
    m_desktops.erase(output);
    m_docks.erase(output);
    if (m_popup && m_popup->output() == output) m_popup.reset();
    m_bars.erase(output);
}

wl::Output* Shell::focusedOutput() const {
    // Hyprland knows which monitor has focus; otherwise the first output
    auto& h = Hyprland::get();
    for (const auto& m : h.monitors())
        if (m.focused)
            for (const auto& [o, _] : m_bars)
                if (o->name == m.name) return o;
    return m_bars.empty() ? nullptr : m_bars.begin()->first;
}

void Shell::togglePopup(const std::string& name, wl::Output* output, float anchorX, const std::string& arg) {
    if (m_popup && !m_popup->closing() && m_popup->name() == name && m_popup->output() == output) {
        m_popup->close();
        return;
    }
    m_popup.reset();
    if (!output) return;

    Popup::Options opts;
    opts.name = name;
    opts.anchorX = anchorX;
    std::function<void(ui::Flex*, Popup&)> build;
    if (name == "audio") {
        opts.width = 400;
        build = panels::audio;
    } else if (name == "network") {
        build = panels::network;
    } else if (name == "bluetooth") {
        opts.width = 340;
        build = panels::bluetooth;
    } else if (name == "battery") {
        opts.width = 380;
        build = panels::battery;
    } else if (name == "run") {
        opts.width = 560;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::run;
    } else if (name == "quicksettings") {
        opts.width = 440;
        build = [arg](ui::Flex* card, Popup& p) { panels::quickSettings(card, p, arg); };
    } else if (name == "notifications") {
        opts.width = 440;
        build = panels::notifications;
    } else if (name == "power") {
        opts.width = 520;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::power;
    } else if (name == "launcher") {
        opts.width = 680;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::launcher(card, p, arg); };
    } else if (name == "dashboard") {
        opts.width = 0; // the calendar and the 340 px column, evenly padded
        build = panels::dashboard;
    } else if (name == "sysmon") {
        opts.width = 680;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::systemMonitor;
    } else if (name == "wallpapers") {
        opts.width = 880;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::wallpapers;
    } else if (name == "lockeditor") {
        opts.fill = true;
        build = panels::lockEditor;
    } else if (name == "barwidgets") {
        opts.width = 480;
        build = [arg](ui::Flex* card, Popup& p) { panels::barWidgets(card, p, arg); };
    } else if (name == "clipboard") {
        opts.width = 560;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::clipboard;
    } else if (name == "screenshot") {
        opts.width = 460;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::screenshot;
    } else if (name == "settings") {
        opts.width = 1024;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::settings(card, p, arg); };
    } else if (name == "overview") {
        opts.fill = true;
        build = panels::overview;
    } else if (name.starts_with("polkit:")) {
        opts.width = 460;
        opts.anchorX = -1;
        opts.centredVertically = true;
        const int id = std::stoi(name.substr(7));
        build = [id](ui::Flex* card, Popup& p) { panels::polkit(card, p, id); };
    } else if (name.starts_with("traymenu:")) {
        opts.width = 300;
        const std::string key = name.substr(9);
        build = [key](ui::Flex* card, Popup& p) { panels::trayMenu(card, p, key); };
    } else {
        return;
    }
    m_popup = std::make_unique<Popup>(output, opts, build);
    Popup* raw = m_popup.get();
    m_popup->onClosed = [this, raw, name] {
        // Closing the polkit dialog (Esc, Cancel, click outside) dismisses the request
        if (name.starts_with("polkit:")) {
            const int id = std::stoi(name.substr(7));
            if (Polkit::get().find(id)) Polkit::get().cancel(id);
        }
        // Destroy outside the popup's own callback
        EventLoop::get().defer([this, raw] {
            if (m_popup.get() == raw) m_popup.reset();
        });
    };
}

void Shell::restyle(bool relayout) {
    if (relayout) {
        TextEngine::get().clear();
        for (auto& [_, bar] : m_bars) bar->rebuild();
        // Desktop widgets follow settings too (desktop clock on/off)
        for (auto& [output, desktop] : m_desktops) desktop = std::make_unique<Desktop>(output);
        for (auto& [output, _] : m_bars) syncDock(output);
    } else {
        for (auto& [_, bar] : m_bars) bar->redraw();
    }
}

void Shell::registerIpc() {
    auto& ipc = IpcServer::get();
    ipc.add("ping", "Check the shell is running", [](const auto&) { return std::string("pong"); });
    ipc.add("version", "Print the shell version", [](const auto&) { return std::string(M3_VERSION); });
    ipc.add("quit", "Exit the shell", [](const auto&) {
        EventLoop::get().defer([] { EventLoop::get().quit(0); });
        return std::string("bye");
    });
    ipc.add("reload", "Re-read config.json and the colour palette", [this](const auto&) {
        Config::get().reload();
        restyle(true);
        return std::string("reloaded");
    });
    ipc.add("bar", "bar [toggle|show|hide|edit|done]: visibility, or edit the widgets in place", [this](const IpcServer::Args& args) {
        const std::string mode = args.empty() ? "toggle" : args[0];
        if (mode == "edit" || mode == "done") {
            panels::editBar(mode == "edit");
            return std::string(mode == "edit" ? "editing" : "done");
        }
        if (mode == "show") m_barsVisible = true;
        else if (mode == "hide") m_barsVisible = false;
        else if (mode == "toggle") m_barsVisible = !m_barsVisible;
        else return std::string("error: bar takes toggle, show or hide");
        for (auto& [_, bar] : m_bars) bar->setVisible(m_barsVisible);
        return std::string(m_barsVisible ? "shown" : "hidden");
    });
    ipc.add("popup", "popup <quicksettings [wifi]|notifications|dashboard|overview|sysmon|power|audio|network|bluetooth|battery|run>: toggle a panel",
            [this](const IpcServer::Args& a) {
        if (a.empty()) return std::string("error: popup needs a name");
        // Status panels open under the right end of the bar; the dashboard
        // under the island
        togglePopup(a[0], focusedOutput(), a[0] == "dashboard" ? -1 : 100000, a.size() > 1 ? a[1] : "");
        return std::string(m_popup && !m_popup->closing() ? "open" : "closed");
    });
    ipc.add("launcher", "launcher [text]: toggle Spotlight, optionally pre-filled (\"#\" clipboard, \":\" emoji…)",
            [this](const IpcServer::Args& a) {
        std::string text;
        for (const auto& w : a) text += (text.empty() ? "" : " ") + w;
        togglePopup("launcher", focusedOutput(), -1, text);
        return std::string(m_popup && !m_popup->closing() ? "open" : "closed");
    });
    ipc.add("clipboard", "clipboard: toggle the clipboard history", [this](const auto&) {
        togglePopup("clipboard", focusedOutput(), -1);
        return std::string(m_popup && !m_popup->closing() ? "open" : "closed");
    });
    ipc.add("screenshot", "screenshot [region|window|screen [delay s]|ocr]: the tool, or capture at once",
            [this](const IpcServer::Args& a) {
        if (a.empty()) {
            togglePopup("screenshot", focusedOutput(), -1);
            return std::string(m_popup && !m_popup->closing() ? "open" : "closed");
        }
        if (a[0] == "ocr") {
            Screenshot::get().ocr();
            return std::string("pick a region");
        }
        const auto mode = a[0] == "window" ? Screenshot::Mode::Window : a[0] == "screen" ? Screenshot::Mode::Screen
                        : a[0] == "region" ? Screenshot::Mode::Region : static_cast<Screenshot::Mode>(-1);
        if (static_cast<int>(mode) < 0) return std::string("error: screenshot takes region, window or screen");
        int delay = 0;
        try {
            if (a.size() > 1) delay = std::stoi(a[1]);
        } catch (...) {
        }
        auto* out = focusedOutput();
        Screenshot::get().capture(mode, delay * 1000, out ? out->name : "");
        return std::string("capturing");
    });
    ipc.add("record", "record [region|screen]: start screen recording, or stop it", [this](const IpcServer::Args& a) {
        auto& s = Screenshot::get();
        const bool was = s.recording();
        auto* out = focusedOutput();
        s.toggleRecording(a.empty() || a[0] != "screen", out ? out->name : "");
        return std::string(was ? "stopping" : "recording");
    });
    ipc.add("notifications", "notifications [list|clear|dnd on|dnd off]", [](const IpcServer::Args& a) {
        auto& n = Notifications::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        if (cmd == "clear") n.clear();
        if (cmd == "dnd") n.setDoNotDisturb(a.size() > 1 && a[1] == "on");
        std::string out = std::format("daemon: {}  dnd: {}\n", n.active() ? "active" : "inactive", n.doNotDisturb());
        for (const auto& x : n.history()) out += std::format("{}\t{}\t{}\t{}\n", x.id, x.app, x.summary, x.body);
        return out;
    });
    ipc.add("volume", "volume up|down|mute|set <0-150>|balance <-100..100>", [](const IpcServer::Args& a) {
        auto& audio = Audio::get();
        const std::string cmd = a.empty() ? "" : a[0];
        auto arg = [&](float def) {
            try { return a.size() > 1 ? std::stof(a[1]) : def; } catch (...) { return def; }
        };
        if (cmd == "up") audio.stepVolume(0.05f);
        else if (cmd == "down") audio.stepVolume(-0.05f);
        else if (cmd == "mute") audio.toggleMuted();
        else if (cmd == "mic-mute") audio.toggleMuted(true);
        else if (cmd == "set") audio.setVolume(arg(50) / 100);
        else if (cmd == "balance") audio.setBalance(arg(0) / 100);
        else if (!cmd.empty()) return std::string("error: volume takes up, down, mute, mic-mute, set or balance");
        const auto* s = audio.sink();
        if (!s) return std::string("no output");
        std::string chans;
        for (size_t i = 0; i < s->volumes.size(); ++i)
            chans += std::format("{}{} {}%", i ? ", " : "", s->channels[i], std::lround(s->volumes[i] * 100));
        return std::format("{} {}%{} balance {:+.2f} [{}]", s->description, std::lround(s->volume() * 100),
                           s->muted ? " muted" : "", s->balance(), chans);
    });
    ipc.add("brightness", "brightness up|down|set <0-100>", [](const IpcServer::Args& a) {
        auto& b = Brightness::get();
        if (!b.available()) return std::string("error: no backlight");
        const std::string cmd = a.empty() ? "" : a[0];
        if (cmd == "up") b.step(0.05f);
        else if (cmd == "down") b.step(-0.05f);
        else if (cmd == "set" && a.size() > 1) b.set(std::stof(a[1]) / 100);
        return std::format("{}%", std::lround(b.value() * 100));
    });
    ipc.add("media", "media play-pause|next|previous|status", [](const IpcServer::Args& a) {
        auto& m = Mpris::get();
        const std::string cmd = a.empty() ? "status" : a[0];
        if (cmd == "play-pause") m.playPause();
        else if (cmd == "next") m.next();
        else if (cmd == "previous") m.previous();
        const auto* p = m.active();
        return p ? std::format("{}: {} — {} [{}]", p->identity, p->title, p->artist, p->status) : std::string("no player");
    });
    ipc.add("settings", "settings [network|bluetooth|sound|display|appearance|style|bar|dock|tools|weather|notifications|lockscreen|lock|keybinds|packages|about]",
            [this](const IpcServer::Args& a) {
        togglePopup("settings", focusedOutput(), -1, a.empty() ? "" : a[0]);
        return std::string(m_popup && !m_popup->closing() ? "open" : "closed");
    });
    ipc.add("wallpaper", "wallpaper [next|prev|set <path>|pick]", [this](const IpcServer::Args& a) {
        auto& w = Wallpaper::get();
        const std::string cmd = a.empty() ? "" : a[0];
        if (cmd == "next") w.next(1);
        else if (cmd == "prev") w.next(-1);
        else if (cmd == "set" && a.size() > 1) w.set(a[1]);
        else if (cmd == "pick") togglePopup("wallpapers", focusedOutput(), -1);
        return w.path().empty() ? std::string("no wallpaper") : w.path();
    });
    ipc.add("debug", "debug layout: every surface's node tree (for UI tests)", [](const IpcServer::Args&) {
        std::string out;
        for (const auto* s : wl::Surface::all()) out += s->dump();
        return out;
    });
    ipc.add("lock", "Lock the session", [](const auto&) {
        LockScreen::get().lock();
        return std::string("locked");
    });
    ipc.add("idle", "idle [inhibit|allow]: keep the screen awake (Caffeine)", [](const IpcServer::Args& a) {
        if (!a.empty()) Idle::get().setInhibited(a[0] == "inhibit");
        return std::string(Idle::get().inhibited() ? "inhibited" : "allowed");
    });
    ipc.add("tray", "List tray items", [](const auto&) {
        std::string out;
        for (const auto& i : Tray::get().items())
            out += std::format("{}\t{}\ticon={}\tpixmap={}x{}\n", i.key, i.title.empty() ? i.id : i.title, i.iconName,
                               i.pixmapW, i.pixmapH);
        return out.empty() ? std::string("no items") : out;
    });
    ipc.add("status", "Summary of every service", [](const auto&) {
        auto& n = Network::get();
        auto& b = Bluetooth::get();
        auto& p = Power::get();
        const auto* s = Audio::get().sink();
        return std::format("audio: {}\nnetwork: {} {} {}% wifi={}\nbluetooth: {} powered={} connected={}\n"
                           "battery: {} {}% charging={}\nbrightness: {}\nmedia: {}\ntray: {} items\n",
                           s ? s->description : "unavailable", n.available() ? "up" : "unavailable", n.name(),
                           n.strength(), n.wifiEnabled(), b.available() ? "up" : "unavailable", b.powered(),
                           b.connected().size(), p.hasBattery() ? "present" : "none", p.percent(), p.charging(),
                           Brightness::get().available() ? std::format("{}%", std::lround(Brightness::get().value() * 100)) : "none",
                           Mpris::get().active() ? Mpris::get().active()->title : "none", Tray::get().items().size());
    });
    ipc.add("outputs", "List outputs the shell is drawing on", [this](const auto&) {
        std::string out;
        for (auto& [o, _] : m_bars) out += std::format("{}\t{}\tscale {}\n", o->name, o->description, o->scale);
        return out;
    });
    ipc.add("workspaces", "List Hyprland workspaces as the shell sees them", [](const auto&) {
        auto& h = Hyprland::get();
        if (!h.available()) return std::string("error: not running under Hyprland");
        std::string out;
        for (const auto& w : h.workspaces())
            out += std::format("{}\t{}\t{} windows{}\n", w.id, w.monitor, w.windows,
                               h.activeWorkspace(w.monitor) == w.id ? "\tactive" : "");
        return out;
    });
}

} // namespace m3::shell
