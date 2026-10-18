#include "shell/Shell.h"

#include "build_config.h"
#include "core/Config.h"
#include "core/EventLoop.h"
#include "ipc/IpcServer.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "services/Hyprland.h"
#include "services/Audio.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Tray.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/bar/Bar.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <cmath>
#include <format>

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

    display.onOutputReady = [this](wl::Output* o) { addOutput(o); };
    display.onOutputRemoved = [this](wl::Output* o) { removeOutput(o); };
    for (auto& o : display.outputs())
        if (o->ready) addOutput(o.get());

    // Wallpaper colours only need a repaint; config edits may change sizes and fonts
    Theme::get().changed.connectForever([this] { restyle(false); });
    Config::get().changed.connectForever([this] { restyle(true); });

    registerIpc();
    return true;
}

void Shell::addOutput(wl::Output* output) {
    if (m_bars.contains(output)) return;
    auto bar = std::make_unique<Bar>(output, [this, output](const std::string& name, float x) {
        togglePopup(name, output, x);
    });
    if (!m_barsVisible) bar->setVisible(false);
    m_bars[output] = std::move(bar);
}

void Shell::removeOutput(wl::Output* output) {
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

void Shell::togglePopup(const std::string& name, wl::Output* output, float anchorX) {
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
    } else if (name == "run") {
        opts.width = 560;
        opts.anchorX = -1;
        opts.centredVertically = true;
        build = panels::run;
    } else {
        return;
    }
    m_popup = std::make_unique<Popup>(output, opts, build);
    Popup* raw = m_popup.get();
    m_popup->onClosed = [this, raw] {
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
    ipc.add("bar", "bar [toggle|show|hide]: status bar visibility", [this](const IpcServer::Args& args) {
        const std::string mode = args.empty() ? "toggle" : args[0];
        if (mode == "show") m_barsVisible = true;
        else if (mode == "hide") m_barsVisible = false;
        else if (mode == "toggle") m_barsVisible = !m_barsVisible;
        else return std::string("error: bar takes toggle, show or hide");
        for (auto& [_, bar] : m_bars) bar->setVisible(m_barsVisible);
        return std::string(m_barsVisible ? "shown" : "hidden");
    });
    ipc.add("popup", "popup <audio|network|bluetooth|run>: toggle a panel", [this](const IpcServer::Args& a) {
        if (a.empty()) return std::string("error: popup needs a name");
        auto* out = focusedOutput();
        // Status panels open under the right end of the bar
        float x = -1;
        if (out && a[0] != "run") x = 100000;
        togglePopup(a[0], out, x);
        return std::string(m_popup ? "open" : "closed");
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
