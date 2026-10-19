#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <map>
#include <memory>
#include <string>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

class Bar;
class Popup;
class Toasts;
class Osd;
class Desktop;
class Dock;

// Owns every surface and wires services to them: one bar per output, kept in
// step with hotplug, plus the IPC commands the shell itself answers.
class Shell {
public:
    Shell();
    ~Shell();

    bool start();

private:
    void addOutput(wl::Output* output);
    void removeOutput(wl::Output* output);
    void registerIpc();
    void restyle(bool relayout);
    void togglePopup(const std::string& name, wl::Output* output, float anchorX, const std::string& arg = {});
    // The Settings app window: opened, or raised (and restored from
    // minimised) with `page` shown
    void openSettings(const std::string& page);
    void openHyprSettings(const std::string& page);
    wl::Output* focusedOutput() const;

    std::map<wl::Output*, std::unique_ptr<Bar>> m_bars;
    std::unique_ptr<Popup> m_popup;
    std::unique_ptr<Popup> m_settings; // an app window, beside the popups
    std::unique_ptr<Popup> m_hyprSettings; // Hyprland Settings, the same
    std::map<wl::Output*, std::unique_ptr<Desktop>> m_desktops;
    std::map<wl::Output*, std::unique_ptr<Dock>> m_docks;
    void syncDock(wl::Output* output);
    void holdDocks(bool held); // the dock stays out while its menu is open
    std::unique_ptr<Toasts> m_toasts;
    std::unique_ptr<Osd> m_osd;
    bool m_barsVisible = true;
    EventLoop::TimerId m_greeterSync = 0; // debounce for the login screen sync
};

} // namespace m3::shell
