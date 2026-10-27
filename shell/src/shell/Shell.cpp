#include "shell/Shell.h"
#include "shell/DesktopEditor.h"
#include "shell/Switcher.h"
#include "shell/Tooltip.h"

#include "build_config.h"
#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "ipc/IpcServer.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "services/HyprOptions.h"
#include "services/Compositor.h"
#include "services/Alarms.h"
#include "services/Audio.h"
#include "services/Backups.h"
#include "services/Firmware.h"
#include "services/Packages.h"
#include "services/Repos.h"
#include "services/Printers.h"
#include "services/ThemedIcons.h"
#include "services/Wellbeing.h"
#include "services/Calendar.h"
#include "quickshare/QuickShare.h"
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
#include "shell/ClockPanel.h"
#include "shell/Desktop.h"
#include "shell/Dock.h"
#include "shell/LockScreen.h"
#include "shell/Notify.h"
#include "shell/Osd.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/PrintersPage.h"
#include "shell/Ringer.h"
#include "services/Hooks.h"
#include "shell/bar/Bar.h"
#include "shell/bar/Frame.h"
#include "shell/bar/Widgets.h"
#include "ui/Theme.h"
#include "wayland/Display.h"
#include "wayland/Surface.h"

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <unistd.h>

namespace m3::shell {

Shell::Shell() = default;
Shell::~Shell() = default;

bool Shell::start() {
    // No config yet: a new install (read before anything writes one)
    std::error_code ec;
    const bool fresh = !std::filesystem::exists(paths::configFile(), ec) || std::filesystem::file_size(paths::configFile(), ec) < 4;
    auto& display = wl::Display::get();
    if (!display.connect()) return false;
    if (!Renderer::get().init(display.display())) return false;
    if (!IpcServer::get().listen()) return false;
    // Outside a compositor the shell knows the bars still run; workspaces
    // just stay empty
    Compositor::get();
    Effects::get();
    // A test instance (M3_INSTANCE, beside the shell) leaves what there's
    // one of in a session to it: the clipboard's watchers, Quick Share, the
    // polkit agent, update checks and the login screen's sync
    const bool sole = paths::instance().empty();
    if (sole) Clipboard::get().ensureWatchers();
    else log::info("instance {}: beside the shell, on {}", paths::instance(), std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "?");
    Calendar::get(); // events, subscriptions and their reminders
    // Quick Share: phones nearby may send files; each is asked here
    if (sole)
        quickshare::QuickShare::get().asking.connectForever([this](int id) {
            EventLoop::get().defer([this, id] { togglePopup("quickshare", focusedOutput(), -1, std::to_string(id)); });
        });
    // New apps bring their icons; before anyone redraws with them
    Apps::get().changed.connectForever([] { Images::get().rescan(); });

    display.onOutputReady = [this](wl::Output* o) { addOutput(o); };
    display.onOutputRemoved = [this](wl::Output* o) { removeOutput(o); };
    for (auto& o : display.outputs())
        if (o->ready) addOutput(o.get());

    // Wallpaper colours only need a repaint; config edits may change sizes and fonts
    // The Settings icon follows the wallpaper's colours too
    themedicons::update();
    Theme::get().changed.connectForever([this] {
        themedicons::update();
        restyle(false);
    });
    Config::get().changed.connectForever([this] { restyle(true); });

    // Transient surfaces follow the focused output
    auto focused = [this] { return focusedOutput(); };
    m_toasts = std::make_unique<Toasts>(focused);
    m_osd = std::make_unique<Osd>(focused);
    // Locking: IPC, loginctl lock-session, idle timeout, before suspend
    panels::lock = [] { EventLoop::get().defer([] { LockScreen::get().lock(); }); };
    // Tooltips on the bar and the dock
    wl::Surface::tooltipHook = [](wl::Surface* s, ui::Node* n) { Tooltip::get().hover(s, n); };
    ui::Node::tooltipChanged = [](ui::Node* n) { Tooltip::get().changed(n); };
    Session::get().lockRequested.connectForever([] { LockScreen::get().lock(); });
    Idle::get().lockRequested.connectForever([] { LockScreen::get().lock(); });
    Idle::get().start();
    // Updates: checked in the background (Settings → Software & updates); a
    // notification's Review opens the page
    Packages::get().openSettings = [this](const std::string& page) { EventLoop::get().defer([this, page] { openSettings(page); }); };
    if (sole) Packages::get().startAutoCheck();
    // Alarms and timers: what goes off shows at the top, over everything
    Alarms::get();
    m_ringer = std::make_unique<Ringer>(focused);
    LockScreen::get().lockedChanged.connectForever([this] {
        Hooks::get().fire(LockScreen::get().locked() ? "session_locked" : "session_unlocked");
        if (LockScreen::get().locked()) m_popup.reset();
        else Wellbeing::get().unlocked();
    });
    // Digital Wellbeing: screen time while unlocked
    Wellbeing::get().locked = [] { return LockScreen::get().locked(); };
    Wellbeing::get().start();

    // Polkit: a dialog per request, closed when polkitd is done with it
    Polkit::get().requested.connectForever([this](int id) {
        togglePopup("polkit:" + std::to_string(id), focusedOutput(), -1);
    });
    Polkit::get().finished.connectForever([this](int id) {
        if (m_popup && m_popup->name() == "polkit:" + std::to_string(id)) m_popup->close();
    });
    if (sole && Config::get().value<bool>("/polkit/enabled", true)) Polkit::get().start();
    // A device asks to pair (from its side) while no Bluetooth list is open
    Bluetooth::get().requested.connectForever([this] {
        if (!m_popup || m_popup->name() != "btpair") togglePopup("btpair", focusedOutput(), -1);
    });

    // "settings dock" opens a panel with an argument (here, a Settings page)
    panels::openAboveDock = [this](const std::string& name, const std::string& arg, float anchor) {
        EventLoop::get().defer([this, name, arg, anchor] { togglePopup(name, focusedOutput(), anchor, arg); });
    };
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
            if (paths::instance().empty() && Config::get().value<bool>("/lock/syncGreeter", true) && access("/var/lib/material3ui-greeter", W_OK) == 0)
                m3::run({"material3ui-greeter", "sync", "--quiet"}, nullptr);
        });
    });

    panels::editBar = [this](bool on) {
        // Not inside the click that asked: rebuilding destroys that button
        EventLoop::get().defer([this, on] {
            // A side bar is too narrow to edit in place: Settings does it
            if (on && barVertical()) return openSettings("bar");
            Bar::editing = on;
            if (on && m_popup) m_popup->close();
            for (auto& [_, bar] : m_bars) bar->rebuild();
        });
    };

    registerIpc();
    // Hooks: the services' events from here on, and this one (IPC is up)
    Hooks::get().watch();
    m_started = true;
    Hooks::get().fire("started");
    // Once the desktop is up and settled
    EventLoop::get().addTimer(2500, [this, fresh] { greet(fresh); });
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
    syncFrame(output); // after the bar: its zone comes first
    syncDock(output);
    if (m_started) Hooks::get().fire("monitor_added", {{"M3_MONITOR", output->name}});
}

void Shell::syncFrame(wl::Output* output) {
    if (!Bar::framed()) {
        m_frames.erase(output);
        return;
    }
    if (auto it = m_frames.find(output); it != m_frames.end()) it->second->rebuild();
    else m_frames[output] = std::make_unique<Frame>(output);
    m_frames[output]->setVisible(m_barsVisible);
}

void Shell::syncDock(wl::Output* output) {
    // Created after the bar, so a bottom bar's zone is already taken
    if (Config::get().value<std::string>("/dock/mode", "autohide") == "off") m_docks.erase(output);
    else if (auto it = m_docks.find(output); it != m_docks.end()) it->second->rebuild();
    else m_docks[output] = std::make_unique<Dock>(output);
}

void Shell::removeOutput(wl::Output* output) {
    Hooks::get().fire("monitor_removed", {{"M3_MONITOR", output->name}});
    LockScreen::get().removeOutput(output);
    m_desktops.erase(output);
    m_docks.erase(output);
    m_frames.erase(output);
    if (m_popup && m_popup->output() == output) m_popup.reset();
    m_bars.erase(output);
}

wl::Output* Shell::focusedOutput() const {
    // The compositor knows which monitor has focus; otherwise the first output
    const std::string focused = Compositor::get().focusedOutput();
    if (!focused.empty())
        for (const auto& [o, _] : m_bars)
            if (o->name == focused) return o;
    return m_bars.empty() ? nullptr : m_bars.begin()->first;
}

void Shell::holdDocks(bool held) {
    for (auto& [_, d] : m_docks) d->setHeld(held);
}

void Shell::openSettings(const std::string& page) {
    // Hyprland's own pages live in Hyprland Settings
    if (page.starts_with("hypr-")) return openHyprSettings(page);
    if (m_settings && !m_settings->closing()) {
        if (!page.empty()) panels::settingsPage(page);
        // Raised, and back from minimised if it was
        auto& c = Compositor::get();
        for (const auto& w : c.windows(true))
            if (w.appId == "material3ui-settings") c.focusWindow(w.id);
        return;
    }
    // Popups belong to the bar; the app replaces any that is open
    if (m_popup) m_popup->close();
    Popup::Options opts;
    opts.name = "settings";
    opts.window = true;
    opts.appId = "material3ui-settings"; // the .desktop file: dock icon and pinning
    opts.title = "Settings";
    opts.icon = "settings";
    m_settings = std::make_unique<Popup>(nullptr, opts, [page](ui::Flex* card, Popup& p) { panels::settings(card, p, page); });
    m_settings->onClosed = [this] {
        panels::settingsClosed();
        EventLoop::get().defer([this] { m_settings.reset(); });
    };
}

void Shell::openHyprSettings(const std::string& page) {
    if (m_hyprSettings && !m_hyprSettings->closing()) {
        if (!page.empty()) panels::settingsPage(page);
        auto& c = Compositor::get();
        for (const auto& w : c.windows(true))
            if (w.appId == "material3ui-hyprland") c.focusWindow(w.id);
        return;
    }
    if (m_popup) m_popup->close();
    Popup::Options opts;
    opts.name = "hyprsettings";
    opts.window = true;
    opts.appId = "material3ui-hyprland"; // its own .desktop file and dock icon
    opts.title = "Hyprland Settings";
    opts.icon = "tune";
    m_hyprSettings = std::make_unique<Popup>(nullptr, opts, [page](ui::Flex* card, Popup& p) { panels::hyprSettings(card, p, page); });
    m_hyprSettings->onClosed = [this] {
        panels::settingsClosed(true);
        EventLoop::get().defer([this] { m_hyprSettings.reset(); });
    };
}

void Shell::openWelcome() {
    if (m_welcome && !m_welcome->closing()) {
        auto& c = Compositor::get();
        for (const auto& w : c.windows(true))
            if (w.appId == "material3ui-welcome") c.focusWindow(w.id);
        return;
    }
    if (m_popup) m_popup->close();
    Popup::Options opts;
    opts.name = "welcome";
    opts.window = true;
    opts.appId = "material3ui-welcome";
    opts.title = "Welcome";
    opts.icon = "waving_hand";
    opts.windowWidth = 900;
    opts.windowHeight = 680;
    m_welcome = std::make_unique<Popup>(nullptr, opts, panels::welcome);
    m_welcome->onClosed = [this] { EventLoop::get().defer([this] { m_welcome.reset(); }); };
}

void Shell::greet(bool freshInstall) {
    const std::string seen = Config::get().value<std::string>("/welcome/version", "");
    if (seen == M3_VERSION) return;
    Config::get().set("/welcome/version", M3_VERSION);
    // Before this was kept, an existing config means an update to this one
    if (freshInstall) openWelcome();
    else togglePopup("whatsnew", focusedOutput(), -1, seen);
}

void Shell::togglePopup(const std::string& name, wl::Output* output, float anchor, const std::string& arg) {
    if (name == "settings") return openSettings(arg);
    if (name == "welcome") return openWelcome();
    if (name == "hyprsettings") return openHyprSettings(arg);
    if (name == "desktopedit") return desktopeditor::open(output ? output : focusedOutput());
    // The system monitor lives in Settings now (Ctrl+Alt+Delete, the bar's widget)
    if (name == "sysmon") return openSettings("monitor");
    // The same popup again closes it; the same kind for something else
    // (another transfer) takes its place
    if (m_popup && !m_popup->closing() && m_popup->name() == name && m_popup->output() == output && m_popupArg == arg) {
        m_popup->close();
        return;
    }
    m_popup.reset();
    holdDocks(false);
    if (!output) return;

    Popup::Options opts;
    opts.name = name;
    opts.anchor = anchor;
    std::function<void(ui::Flex*, Popup&)> build;
    if (name == "audio") {
        opts.width = 400;
        build = panels::audio;
    } else if (name == "network") {
        build = panels::network;
    } else if (name == "bluetooth") {
        opts.width = 340;
        build = panels::bluetooth;
    } else if (name == "dockmenu") {
        opts.width = 270;
        opts.aboveDock = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::dockMenu(card, p, arg); };
    } else if (name == "btpair") {
        opts.width = 420;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = panels::bluetoothPair;
    } else if (name == "battery") {
        opts.width = 380;
        build = panels::battery;
    } else if (name == "run") {
        opts.width = 560;
        opts.anchor = -1;
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
        opts.anchor = -1;
        opts.centredVertically = true;
        build = panels::power;
    } else if (name == "launcher") {
        opts.width = 680;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::launcher(card, p, arg); };
    } else if (name == "dashboard") {
        opts.width = 0; // the calendar and the 340 px column, evenly padded
        build = panels::dashboard;
    } else if (name == "wallpapers") {
        opts.width = 880;
        opts.anchor = -1;
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
        opts.anchor = -1;
        opts.centredVertically = true;
        build = panels::clipboard;
    } else if (name == "screenshot") {
        opts.width = 540; // the Record tab's rows of choices
        opts.anchor = -1;
        opts.centredVertically = true;
        build = panels::screenshot;
    } else if (name == "settings") {
        opts.width = 1024;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::settings(card, p, arg); };
    } else if (name == "quickshare") {
        opts.width = 520;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::quickSharePanel(card, p, arg); };
    } else if (name == "print") {
        opts.width = 520;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::printDialog(card, p, arg); };
    } else if (name == "quickshare-send") {
        opts.width = 560;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::quickShareSend(card, p, arg); };
    } else if (name == "event") {
        opts.width = 560;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::eventEditor(card, p, arg); };
    } else if (name == "clock") {
        opts.width = 440;
        build = [arg](ui::Flex* card, Popup& p) { panels::clock(card, p, arg); };
    } else if (name == "files") {
        opts.width = 980;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::filePicker(card, p, arg.empty() ? "wallpaper" : arg); };
    } else if (name == "whatsnew") {
        opts.width = 560;
        opts.anchor = -1;
        opts.centredVertically = true;
        build = [arg](ui::Flex* card, Popup& p) { panels::whatsNew(card, p, arg); };
    } else if (name == "overview") {
        opts.fill = true;
        build = panels::overview;
    } else if (name.starts_with("polkit:")) {
        opts.width = 460;
        opts.anchor = -1;
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
    m_popupArg = arg;
    holdDocks(name == "dockmenu"); // a new popup releases a held dock too
    Popup* raw = m_popup.get();
    m_popup->onClosed = [this, raw, name] {
        if (name == "dockmenu") holdDocks(false);
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
        for (auto& [output, bar] : m_bars) {
            bar->rebuild();
            syncFrame(output);
        }
        // Open panels and windows too: text may have grown (Accessibility)
        for (auto* s : wl::Surface::all()) s->requestLayout();
        // Desktop widgets follow settings too (sizes, formats), on the same
        // surface: a new one would flash the wallpaper. The desktop itself
        // comes and goes with its switch.
        const bool desktops = Config::get().value<bool>("/desktop/wallpaper", true);
        for (auto& [output, _] : m_bars) {
            if (!desktops) m_desktops.erase(output);
            else if (auto it = m_desktops.find(output); it != m_desktops.end()) it->second->refresh();
            else m_desktops[output] = std::make_unique<Desktop>(output);
        }
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
        for (auto& [_, frame] : m_frames) frame->setVisible(m_barsVisible);
        return std::string(m_barsVisible ? "shown" : "hidden");
    });
    ipc.add("popup", "popup <quicksettings [wifi]|notifications|dashboard|overview|power|audio|network|bluetooth|battery|run|clock [alarms|timers]>: toggle a panel",
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
        for (const auto& x : n.current()) out += std::format("{}\t{}\t{}\t{}\n", x.id, x.app, x.summary, x.body);
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
    ipc.add("zoom", "zoom in|out|reset: the magnifier (Accessibility)", [](const IpcServer::Args& a) {
        const std::string cmd = a.empty() ? "" : a[0];
        double z = Config::get().value<double>("/accessibility/zoom", 1.0);
        z = cmd == "in" ? z + 0.5 : cmd == "out" ? z - 0.5 : 1.0;
        z = std::clamp(z, 1.0, 4.0);
        Config::get().set("/accessibility/zoom", z);
        return std::format("zoom {:.1f}×", z);
    });
    ipc.add("hypr", "hypr options | get <name> | set <name> <json> | reset <name> | undo | profile list|apply|next|previous|save|delete: Hyprland Settings",
            [](const IpcServer::Args& a) {
        auto& h = HyprOptions::get();
        if (!h.loaded()) h.load();
        const std::string cmd = a.empty() ? "options" : a[0];
        if (cmd == "options") {
            std::map<int, int> kinds;
            int custom = 0;
            for (const auto& o : h.options()) {
                ++kinds[static_cast<int>(o.kind)];
                custom += o.custom;
            }
            static const char* names[] = {"bool", "int", "float", "text", "choice", "color", "gradient", "vec2", "gaps"};
            std::string out = std::format("{} options, {} set in Settings:", h.options().size(), custom);
            for (auto [k, n] : kinds) out += std::format(" {} {}", n, names[k]);
            return out;
        }
        if (cmd == "profile") {
            const std::string sub = a.size() > 1 ? a[1] : "list";
            std::string name;
            for (size_t i = 2; i < a.size(); ++i) name += (i > 2 ? " " : "") + a[i];
            if (sub == "list") {
                std::string out;
                for (const auto& p : h.profiles()) out += (p == h.activeProfile() ? "* " : "  ") + p + "\n";
                return out.empty() ? std::string("no profiles (save one in Hyprland Settings → Profiles)") : out;
            }
            if (sub == "apply") {
                const auto err = h.applyProfile(name);
                return err.empty() ? "applied " + h.activeProfile() : "error: " + err;
            }
            if (sub == "next" || sub == "previous" || sub == "prev") {
                const auto p = h.cycleProfile(sub == "next" ? 1 : -1);
                return p.empty() ? std::string("no profiles") : "applied " + p;
            }
            if (sub == "save" && !name.empty()) {
                h.saveProfile(name);
                return "saved " + name;
            }
            if (sub == "delete" && !name.empty()) {
                h.deleteProfile(name);
                return "deleted " + name;
            }
            return std::string("usage: hypr profile list|apply <name>|next|previous|save <name>|delete <name>");
        }
        if (cmd == "undo") {
            const std::string name = h.undo();
            return name.empty() ? std::string("nothing to undo") : std::format("undid {} = {}", name, h.find(name)->value.dump());
        }
        if (a.size() < 2) return std::string("error: name needed");
        const auto* o = h.find(a[1]);
        if (!o) return std::string("error: no option ") + a[1];
        if (cmd == "get")
            return std::format("{} = {} (default {}){}", o->name, o->value.dump(), o->fallback.dump(), o->custom ? " [Settings]" : "");
        std::string err;
        if (cmd == "set" && a.size() > 2) {
            std::string text;
            for (size_t i = 2; i < a.size(); ++i) text += (i > 2 ? " " : "") + a[i];
            try {
                err = h.set(a[1], nlohmann::json::parse(text));
            } catch (const std::exception&) {
                err = h.set(a[1], text); // a bare string
            }
        } else if (cmd == "reset") {
            err = h.reset(a[1]);
        }
        return err.empty() ? std::format("{} = {}", o->name, h.find(a[1])->value.dump()) : "error: " + err;
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
    ipc.add("settings", "settings [network|vpn|bluetooth|printers|defaults|notifications|tools|wellbeing|calendar|clock|weather|battery|storage|monitor|"
                        "sound|display|appearance|style|bar|dock|lockscreen|accessibility|lock|firewall|users|system|mouse|keybinds|hooks|packages|firmware|about]",
            [this](const IpcServer::Args& a) {
        openSettings(a.empty() ? "" : a[0]);
        return std::string(m_settings && !m_settings->closing() ? "open" : "closed");
    });
    ipc.add("desktop", "desktop edit|menu: place the desktop's widgets and shortcuts, or open its menu", [this](const IpcServer::Args& a) {
        if (!a.empty() && a[0] == "edit") {
            desktopeditor::open(focusedOutput());
            return std::string("editing");
        }
        if (!a.empty() && a[0] == "menu") {
            // At the pointer, as a right click on the desktop opens it
            float x = 100, y = 100;
            if (auto c = Compositor::get().cursorPosition()) {
                x = c->first;
                y = c->second;
                for (const auto& m : Compositor::get().outputs())
                    if (x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
                        x -= m.x;
                        y -= m.y;
                    }
            }
            desktopeditor::menu(focusedOutput(), x, y);
            return std::string("menu");
        }
        return std::string("usage: desktop edit|menu");
    });
    ipc.add("hyprsettings", "hyprsettings [hypr-monitors|hypr-windows|hypr-decoration|hypr-animations|hypr-keybinds|hypr-startup|hypr-environment|hypr-layouts|hypr-input|hypr-gestures|"
                            "hypr-cursor|hypr-groups|hypr-misc|hypr-render|hypr-debug]: Hyprland Settings",
            [this](const IpcServer::Args& a) {
        openHyprSettings(a.empty() ? "" : a[0]);
        return std::string(m_hyprSettings && !m_hyprSettings->closing() ? "open" : "closed");
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
    ipc.add("layout", "layout check: anything drawn outside its card or pill, or off its surface, on every open surface (for UI tests)",
            [](const IpcServer::Args& a) {
        if (!a.empty() && a[0] != "check") return std::string("error: layout check");
        std::string out;
        int checked = 0, surfaces = 0;
        for (const auto* s : wl::Surface::all()) {
            ++surfaces;
            out += s->overflow(checked);
        }
        if (out.empty()) return std::format("ok: nothing outside its box ({} nodes on {} surfaces)", checked, surfaces);
        return out + std::format("{} found ({} nodes on {} surfaces)", std::count(out.begin(), out.end(), '\n'), checked, surfaces);
    });
    ipc.add("lock", "Lock the session", [](const auto&) {
        LockScreen::get().lock();
        return std::string("locked");
    });
    ipc.add("welcome", "Show the first-run tour", [this](const auto&) {
        EventLoop::get().defer([this] { openWelcome(); });
        return std::string();
    });
    ipc.add("whatsnew", "whatsnew [since-version]: the release notes popup", [this](const IpcServer::Args& a) {
        const std::string since = a.empty() ? "" : a[0];
        EventLoop::get().defer([this, since] { togglePopup("whatsnew", focusedOutput(), -1, since); });
        return std::string();
    });
    ipc.add("backup", "backup [list|restore [file]]: back up the settings now, list the backups, or restore one (the newest)", [](const IpcServer::Args& a) {
        if (!a.empty() && a[0] == "list") {
            std::string out;
            for (const auto& e : Backups::get().list()) {
                char when[32];
                std::tm tm{};
                localtime_r(&e.time, &tm);
                std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", &tm);
                out += std::format("{}  {:<15} {}\n", when, e.reason, e.path);
            }
            return out.empty() ? std::string("no backups") : out;
        }
        if (!a.empty() && a[0] == "restore") {
            // The newest settings backup, or the one named
            for (const auto& e : Backups::get().list())
                if (a.size() < 2 || e.path == a[1] || e.path.ends_with("/" + a[1])) {
                    Backups::get().restore(e);
                    return "restoring " + e.path;
                }
            return std::string("no such backup");
        }
        Backups::get().create("manual");
        return std::string("backing up");
    });
    ipc.add("quickshare", "quickshare [status|accept <id>|decline <id>|cancel <id>|send [files…]|devices|sendto <device> <files…>]: Quick Share",
            [](const IpcServer::Args& a) {
        auto& qs = quickshare::QuickShare::get();
        const std::string cmd = a.empty() ? "status" : a[0];
        const auto absolute = [](const std::string& p) {
            std::error_code ec;
            const auto abs = std::filesystem::absolute(p, ec);
            return ec ? p : abs.string();
        };
        if (cmd == "send") {
            std::string paths;
            for (size_t i = 1; i < a.size(); ++i) paths += absolute(a[i]) + "\n";
            panels::open("quickshare-send " + paths);
            return std::string("choose a device");
        }
        if (cmd == "cancel" && a.size() > 1) {
            qs.cancel(std::atoi(a[1].c_str()));
            return "cancelled " + a[1];
        }
        // Without the popup: look for a few seconds, then list or send
        static quickshare::QuickShare::Lease lease;
        static EventLoop::TimerId drop = 0;
        const auto hold = [] {
            if (!lease) lease = quickshare::QuickShare::get().discover();
            EventLoop::get().debounce(drop, 60000, [] { lease.reset(); });
        };
        if (cmd == "devices") {
            hold();
            std::string out;
            for (const auto& d : qs.devices()) out += std::format("{}  {}  type {}\n", d.id, d.name, d.type);
            if (out.empty()) out = "none yet (looking for a minute; ask again)\n";
            if (const auto note = qs.discoveryNote(); !note.empty()) out += note + "\n";
            return out;
        }
        if (cmd == "sendto" && a.size() > 2) {
            hold();
            for (const auto& d : qs.devices())
                if (d.id == a[1] || d.name == a[1]) {
                    std::vector<std::string> files;
                    for (size_t i = 2; i < a.size(); ++i) files.push_back(absolute(a[i]));
                    const int id = qs.send(d, files);
                    return id ? "sending: transfer " + std::to_string(id) : std::string("nothing to send");
                }
            return std::string("no such device (yet): see quickshare devices");
        }
        if ((cmd == "accept" || cmd == "decline") && a.size() > 1) {
            const int id = std::atoi(a[1].c_str());
            if (cmd == "accept") qs.accept(id);
            else qs.decline(id);
            return (cmd == "accept" ? "accepted " : "declined ") + a[1];
        }
        std::string out = qs.receiving() ? "visible as " + qs.deviceName() + "\n" : "not visible" + (qs.status().empty() ? "" : ": " + qs.status()) + "\n";
        static const char* states[] = {"connecting", "asking", "receiving", "done", "declined", "failed"};
        for (const auto& [id, t] : qs.transfers())
            out += std::format("{}  {}{}  {}  pin {}  {} file(s) {}/{} bytes{}\n", id, t.outgoing ? "→ " : "", states[static_cast<int>(t.state)], t.sender, t.pin, t.files.size(),
                               t.received, t.total, t.error.empty() ? "" : "  " + t.error);
        return out;
    });
    ipc.add("hooks",
            "hooks [list] | fire <event> [NAME=value…] | run <event> | set <event> <command> | clear <event> | events [n] | log [n]: "
            "commands that run on events (Settings → Hooks); fire acts as if it happened, run tries a hook with sample values",
            [](const IpcServer::Args& a) {
        auto& h = Hooks::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        auto tail = [](const std::string& file, size_t n) {
            std::ifstream in(file);
            std::vector<std::string> lines;
            for (std::string l; std::getline(in, l);) lines.push_back(l);
            std::string out;
            for (size_t i = lines.size() > n ? lines.size() - n : 0; i < lines.size(); ++i) out += lines[i] + "\n";
            return out.empty() ? std::string("(nothing yet)\n") : out;
        };
        auto count = [&](size_t fallback) {
            const int n = a.size() > 1 ? std::atoi(a[1].c_str()) : 0;
            return n > 0 ? static_cast<size_t>(n) : fallback;
        };
        if (cmd == "events") return tail(Hooks::eventsFile(), count(20));
        if (cmd == "log") return tail(Hooks::logFile(), count(40));
        if (cmd == "list") {
            std::string out = std::format("hooks {} · events recorded to {}\n", h.enabled() ? "on" : "off", Hooks::eventsFile());
            for (const auto& e : Hooks::events()) {
                const auto list = h.commands(e.id);
                out += std::format("{:<28}{}", e.id, list.empty() ? "-" : h.paused(e.id) ? "paused" : "");
                for (const auto& c : list) out += "\n    " + c;
                out += "\n";
            }
            return out;
        }
        if (a.size() < 2) return std::string("error: hooks ") + cmd + " needs an event (hooks list shows them)";
        const std::string event = a[1];
        if (!Hooks::find(event)) return "error: no event \"" + event + "\" (hooks list shows them)";
        if (cmd == "fire") {
            Hooks::Env env;
            for (size_t i = 2; i < a.size(); ++i)
                if (const auto eq = a[i].find('='); eq != std::string::npos && eq > 0) env.emplace_back(a[i].substr(0, eq), a[i].substr(eq + 1));
            h.simulate(event, env);
            return "fired " + event + (h.commands(event).empty() ? " (no hooks set for it)" : "");
        }
        if (cmd == "run") {
            if (h.commands(event).empty()) return "error: no hooks set for " + event;
            h.test(event);
            return "running " + event + "'s hooks with sample values; see hooks log";
        }
        if (cmd == "set" && a.size() > 2) {
            std::string command;
            for (size_t i = 2; i < a.size(); ++i) command += (i > 2 ? " " : "") + a[i];
            auto list = h.commands(event);
            list.push_back(command);
            h.setCommands(event, list);
            return std::format("{} now runs {} command{}", event, list.size(), list.size() == 1 ? "" : "s");
        }
        if (cmd == "clear") {
            h.setCommands(event, {});
            return "cleared " + event;
        }
        return std::string("error: hooks [list|fire|run|set|clear|events|log]");
    });
    ipc.add("config", "config export [file]|import <file>: the shell's settings as a JSON file, and back (backed up first)",
            [](const IpcServer::Args& a) {
        const auto absolute = [](const std::string& p) {
            std::error_code ec;
            const auto abs = std::filesystem::absolute(p, ec);
            return ec ? p : abs.string();
        };
        if (!a.empty() && a[0] == "export") {
            const std::string file = a.size() > 1 ? absolute(a[1]) : paths::home() + "/Downloads/material3ui-settings.json";
            const std::string error = Config::get().exportTo(file);
            return error.empty() ? "exported to " + file : "error: " + error;
        }
        if (a.size() > 1 && a[0] == "import") {
            const std::string file = absolute(a[1]);
            Backups::get().create("before-import", [file](bool) {
                const std::string error = Config::get().importFrom(file);
                if (!error.empty()) log::warn("config import: {}", error);
            });
            return "importing " + file + " (the settings now are backed up first)";
        }
        return std::string("usage: config export [file] | config import <file>");
    });
    ipc.add("wellbeing", "wellbeing [status|bedtime on|off|focus on|off]: Digital Wellbeing", [](const IpcServer::Args& a) {
        auto& w = Wellbeing::get();
        const std::string cmd = a.empty() ? "status" : a[0];
        const bool on = a.size() > 1 && a[1] == "on";
        if (cmd == "bedtime" && a.size() > 1) w.setBedtimeNow(on);
        else if (cmd == "focus" && a.size() > 1) w.setFocus(on);
        else if (cmd != "status") return std::string("usage: wellbeing [status|bedtime on|off|focus on|off]");
        std::string out = std::format("screen time {} min, unlocks {}, bedtime {}, focus {}\n", w.today().total() / 60, w.today().unlocks,
                                      w.bedtime() ? "on" : "off", w.focus() ? "on" : "off");
        for (const auto& [cls, secs] : w.today().seconds) out += std::format("  {}  {} s\n", cls, secs);
        return out;
    });
    ipc.add("print", "print [files…]: the print dialog for these files (or printers status)", [](const IpcServer::Args& a) {
        if (a.empty()) {
            auto& pr = Printers::get();
            std::string out = !pr.installed() ? "printing isn't set up\n" : !pr.running() ? "the print service isn't running\n" : "";
            for (const auto& p : pr.printers())
                out += std::format("{}  {}{}{}\n", p.name, p.state, p.isDefault ? "  default" : "", p.message.empty() ? "" : "  " + p.message);
            for (const auto& j : pr.jobs()) out += std::format("job {}  {}  {} bytes\n", j.id, j.user, j.size);
            return out.empty() ? std::string("no printers\n") : out;
        }
        std::string files;
        for (const auto& f : a) {
            std::error_code ec;
            const auto abs = std::filesystem::absolute(f, ec);
            files += (ec ? f : abs.string()) + "\n";
        }
        panels::open("print " + files);
        return std::string("choose a printer");
    });
    ipc.add("calendar", "calendar [list [days]|subscribe <name> <ics-url>|refresh]: events coming up, or add a calendar", [](const IpcServer::Args& a) {
        auto& cal = Calendar::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        if (cmd == "subscribe" && a.size() >= 3) {
            cal.subscribe(a[1], a[2]);
            return std::string("subscribed: ") + a[1];
        }
        if (cmd == "refresh") {
            cal.refresh();
            return std::string("refreshing");
        }
        const long days = a.size() > 1 ? std::max(1L, std::atol(a[1].c_str())) : 7;
        const std::time_t now = std::time(nullptr);
        std::string out;
        for (const auto& o : cal.between(now, now + days * 86400)) {
            std::tm tm{};
            localtime_r(&o.start, &tm);
            char when[32];
            std::strftime(when, sizeof when, o.event->allDay ? "%a %d %b       " : "%a %d %b %H:%M", &tm);
            out += std::format("{}  {}\n", when, o.event->title);
        }
        for (const auto& src : cal.sources())
            out += std::format("[{}] {}{}\n", src.enabled ? "on" : "off", src.name, src.error.empty() ? "" : " — " + src.error);
        return out.empty() ? std::string("nothing coming up") : out;
    });
    ipc.add("switcher", "switcher [next|prev]: the Super+Tab window switcher", [this](const IpcServer::Args& a) {
        const int by = !a.empty() && a[0] == "prev" ? -1 : 1;
        EventLoop::get().defer([this, by] { switcher::step(focusedOutput(), by); });
        return std::string();
    });
    ipc.add("idle", "idle [inhibit|allow]: keep the screen awake (Caffeine)", [](const IpcServer::Args& a) {
        if (!a.empty()) Idle::get().setInhibited(a[0] == "inhibit");
        return std::string(Idle::get().inhibited() ? "inhibited" : "allowed");
    });
    ipc.add("alarm", "alarm [list] | add <HH:MM> [once|daily|weekdays|weekends|mon,wed…] [label] | on|off|remove <id> | stop|snooze [id]",
            [](const IpcServer::Args& a) {
        auto& al = Alarms::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        auto join = [&a](size_t from) {
            std::string out;
            for (size_t i = from; i < a.size(); ++i) out += (out.empty() ? "" : " ") + a[i];
            return out;
        };
        if (cmd == "list") {
            std::string out;
            for (const auto& x : al.alarms())
                out += std::format("{}\t{:02}:{:02}\t{}\t{}\t{}\n", x.id, x.hour, x.minute, x.snoozed ? "snoozed" : x.enabled ? "on" : "off",
                                   Alarms::daysText(x), x.label);
            for (const auto& r : al.ringing())
                if (!r.timer) out += "ringing: " + r.id + "\n";
            return out.empty() ? std::string("no alarms") : out;
        }
        if (cmd == "add") {
            Alarms::Alarm x;
            if (a.size() < 2 || std::sscanf(a[1].c_str(), "%d:%d", &x.hour, &x.minute) != 2 || x.hour < 0 || x.hour > 23 || x.minute < 0 ||
                x.minute > 59)
                return std::string("error: alarm add <HH:MM> [days] [label]");
            size_t labelFrom = 2;
            if (a.size() > 2) {
                const std::string& d = a[2];
                if (d == "daily") x.days = {0, 1, 2, 3, 4, 5, 6};
                else if (d == "weekdays") x.days = {1, 2, 3, 4, 5};
                else if (d == "weekends") x.days = {0, 6};
                else if (d != "once") {
                    // "mon,wed,fri": each part a day's name, or its start
                    static const char* names[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
                    for (size_t from = 0; from <= d.size();) {
                        const size_t comma = std::min(d.find(',', from), d.size());
                        const std::string part = d.substr(from, comma - from);
                        int day = -1;
                        for (int i = 0; i < 7 && part.size() >= 3; ++i)
                            if (std::string_view(names[i]).starts_with(part)) day = i;
                        if (day < 0) {
                            x.days.clear();
                            break;
                        }
                        x.days.push_back(day);
                        from = comma + 1;
                    }
                }
                // A word that isn't days starts the label
                if (d == "once" || !x.days.empty()) labelFrom = 3;
            }
            x.label = join(labelFrom);
            const std::string id = al.save(x);
            std::time_t at = 0;
            if (const auto* saved = al.alarm(id)) at = Alarms::next(*saved, std::time(nullptr));
            return std::format("{}: {}", id, Alarms::untilText(at));
        }
        if (cmd == "stop" || cmd == "snooze") {
            if (al.ringing().empty() && a.size() < 2) return std::string("nothing ringing");
            if (a.size() > 1) cmd == "stop" ? al.stop(a[1]) : al.snooze(a[1]);
            else if (cmd == "stop") al.stopAll();
            else
                for (const auto& r : std::vector(al.ringing())) r.timer ? al.stop(r.id) : al.snooze(r.id);
            return cmd == "stop" ? std::string("stopped") : std::string("snoozed");
        }
        if ((cmd == "on" || cmd == "off" || cmd == "remove") && a.size() > 1) {
            if (!al.alarm(a[1])) return std::string("error: no alarm ") + a[1];
            if (cmd == "remove") al.remove(a[1]);
            else al.setEnabled(a[1], cmd == "on");
            return cmd;
        }
        return std::string("error: alarm [list] | add <HH:MM> [days] [label] | on|off|remove <id> | stop|snooze [id]");
    });
    ipc.add("timer", "timer [list] | start <length: 5, 1:30, 1h20m, 90s> [label] | pause|resume|reset|remove <id>", [](const IpcServer::Args& a) {
        auto& al = Alarms::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        if (cmd == "list") {
            std::string out;
            const int64_t now = Alarms::nowMs();
            for (const auto& t : al.timers())
                out += std::format("{}\t{}\t{} of {}\t{}\n", t.id, t.running() ? "running" : "paused", Alarms::countdown(t.remaining(now)),
                                   Alarms::countdown(t.length), t.label);
            for (const auto& r : al.ringing())
                if (r.timer) out += "ringing: " + r.id + "\n";
            return out.empty() ? std::string("no timers") : out;
        }
        if (cmd == "start") {
            const int64_t ms = a.size() > 1 ? Alarms::parseLength(a[1]) : 0;
            if (ms <= 0) return std::string("error: timer start <length> [label]: 5 (minutes), 1:30, 1h20m, 90s");
            std::string label;
            for (size_t i = 2; i < a.size(); ++i) label += (label.empty() ? "" : " ") + a[i];
            return al.startTimer(ms, label) + ": " + Alarms::countdown(ms);
        }
        if (a.size() < 2 || std::ranges::none_of(al.timers(), [&](const auto& t) { return t.id == a[1]; }))
            return std::string("error: timer [list] | start <length> [label] | pause|resume|reset|remove <id>");
        if (cmd == "pause") al.pause(a[1]);
        else if (cmd == "resume") al.resume(a[1]);
        else if (cmd == "reset") al.reset(a[1]);
        else if (cmd == "remove") al.removeTimer(a[1]);
        else return std::string("error: timer pause|resume|reset|remove <id>");
        return cmd;
    });
    ipc.add("updates", "updates [list|check|upgrade]: what's waiting (firmware too), look again, or install (asks for the password)",
            [](const IpcServer::Args& a) {
        auto& p = Packages::get();
        auto& fw = Firmware::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        if (cmd == "check") {
            p.check();
            if (fw.installed() && Packages::includesFirmware()) fw.check(true);
            return std::string("checking");
        }
        if (cmd == "upgrade") {
            if (p.updates().empty()) return std::string("nothing to update");
            if (p.busy()) return std::string("busy: ") + p.status();
            p.upgrade();
            return std::string("updating");
        }
        if (cmd != "list") return std::string("error: updates [list|check|upgrade]");
        static const char* names[] = {"repo", "aur", "flatpak", "shell"};
        std::string out;
        for (const auto& u : p.updates())
            out += std::format("{}\t{}\t{} -> {}\n", names[static_cast<int>(u.source)], u.name, u.from.empty() ? "-" : u.from, u.to);
        for (const auto& d : fw.devices())
            if (!d.releases.empty() && d.releases.front().upgrade)
                out += std::format("firmware\t{}\t{} -> {}\n", d.name, d.version, d.releases.front().version);
        if (p.checking() || fw.checking()) out += "checking…\n";
        if (p.busy()) out += "busy: " + p.status() + "\n";
        return out.empty() ? std::string(p.lastChecked() ? "up to date" : "not checked yet") : out;
    });
    ipc.add("firmware", "firmware [list|check|update [device id]]: fwupd's devices and their updates", [](const IpcServer::Args& a) {
        auto& fw = Firmware::get();
        if (!fw.installed()) return std::string("fwupd isn't installed (Settings → Firmware installs it)");
        const std::string cmd = a.empty() ? "list" : a[0];
        if (cmd == "check") {
            fw.check(true);
            return std::string("checking");
        }
        if (cmd == "update") {
            if (fw.updating() || fw.checking()) return std::string("busy");
            fw.update(a.size() > 1 ? a[1] : "");
            return std::string("updating");
        }
        if (cmd != "list") return std::string("error: firmware [list|check|update [device id]]");
        std::string out;
        for (const auto& d : fw.devices()) {
            out += std::format("{}\t{}\t{}\t{}", d.id, d.name, d.version.empty() ? "-" : d.version, d.updatable() ? "updatable" : "-");
            if (!d.releases.empty()) out += " -> " + d.releases.front().version;
            if (d.needsReboot()) out += "\tneeds a restart";
            out += "\n";
        }
        if (fw.checking()) out += "checking…\n";
        if (!fw.error().empty()) out += "error: " + fw.error() + "\n";
        return out.empty() ? std::string(fw.lastChecked() ? "no devices" : "not checked yet (firmware check)") : out;
    });
    ipc.add("repos", "repos [list] | enable|disable <repo>: pacman's repositories, mirrors and held packages; Flatpak's remotes",
            [](const IpcServer::Args& a) {
        auto& r = Repos::get();
        const std::string cmd = a.empty() ? "list" : a[0];
        if ((cmd == "enable" || cmd == "disable") && a.size() > 1) {
            r.setEnabled(a[1], cmd == "enable");
            return r.error().empty() ? std::string("asking for the password") : "error: " + r.error();
        }
        if (cmd != "list") return std::string("error: repos [list] | enable|disable <repo>");
        r.load();
        std::string out;
        for (const auto& repo : r.conf().repos) out += std::format("repo\t{}\t{}\n", repo.name, repo.enabled ? "on" : "off");
        out += std::format("mirrors\t{}\n", r.mirrors().size());
        out += std::format("parallel downloads\t{}\n", r.conf().parallelDownloads);
        for (const auto& h : r.conf().ignored) out += "held\t" + h + "\n";
        for (const auto& f : r.remotes()) out += std::format("flatpak\t{}\t{}\t{}\n", f.name, f.enabled ? "on" : "off", f.url);
        return out;
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
    ipc.add("workspaces", "List the compositor's workspaces as the shell sees them", [](const auto&) {
        auto& c = Compositor::get();
        if (!c.available()) return std::string("error: not running under a compositor the shell knows (Hyprland)");
        std::string out;
        for (const auto& w : c.workspaces())
            out += std::format("{}\t{}\t{} windows{}\n", w.number, w.output, w.windows,
                               c.activeWorkspace(w.output) == w.number ? "\tactive" : "");
        return out;
    });
}

} // namespace m3::shell
