#include "shell/Shell.h"

#include "build_config.h"
#include "core/Config.h"
#include "core/EventLoop.h"
#include "ipc/IpcServer.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "services/Hyprland.h"
#include "shell/bar/Bar.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

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
    auto bar = std::make_unique<Bar>(output);
    if (!m_barsVisible) bar->setVisible(false);
    m_bars[output] = std::move(bar);
}

void Shell::removeOutput(wl::Output* output) {
    m_bars.erase(output);
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
