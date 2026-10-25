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
class Frame;

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
    // `anchor`: along the bar (x, or y on a side bar), < 0 to centre it
    void togglePopup(const std::string& name, wl::Output* output, float anchor, const std::string& arg = {});
    // The Settings app window: opened, or raised (and restored from
    // minimised) with `page` shown
    void openSettings(const std::string& page);
    void openHyprSettings(const std::string& page);
    // The first-run tour window
    void openWelcome();
    // A new install gets the tour, an update What's new (once each version)
    void greet(bool freshInstall);
    wl::Output* focusedOutput() const;

    std::map<wl::Output*, std::unique_ptr<Bar>> m_bars;
    std::unique_ptr<Popup> m_popup;
    std::string m_popupArg; // what the open popup shows (a transfer, an event…)
    std::unique_ptr<Popup> m_settings; // an app window, beside the popups
    std::unique_ptr<Popup> m_hyprSettings; // Hyprland Settings, the same
    std::unique_ptr<Popup> m_welcome; // the tour window
    std::map<wl::Output*, std::unique_ptr<Desktop>> m_desktops;
    std::map<wl::Output*, std::unique_ptr<Dock>> m_docks;
    std::map<wl::Output*, std::unique_ptr<Frame>> m_frames; // the Framed bar style
    void syncDock(wl::Output* output);
    void syncFrame(wl::Output* output); // there in the Framed style, else gone
    void holdDocks(bool held); // the dock stays out while its menu is open
    std::unique_ptr<Toasts> m_toasts;
    std::unique_ptr<Osd> m_osd;
    bool m_barsVisible = true;
    EventLoop::TimerId m_greeterSync = 0; // debounce for the login screen sync
};

} // namespace m3::shell
