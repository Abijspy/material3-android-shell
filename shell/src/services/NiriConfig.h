#pragma once

#include "core/Signal.h"
#include "services/Kdl.h"
#include "services/NiriSettings.h"

#include <string>
#include <vector>

namespace m3 {

// In a niri session, the shell's Settings for niri: written as
// ~/.config/niri/material3ui-settings.kdl (services/NiriSettings) whenever
// they or the palette change; niri reads it again by itself. It comes from
// config.json: the cursor (Effects), /input (touchpad and mouse), /keyboard
// (layouts and repeat), /niri/outputs (monitors), /niri/binds (your keys),
// /niri/look (Niri Settings: layout, borders, animations, behaviour), and
// the palette for the focus ring and border.
class NiriConfig {
public:
    static NiriConfig& get();
    void apply(); // the file, when what it says changed

    static std::string file();        // ~/.config/niri/material3ui-settings.kdl
    static std::string includeFile(); // ~/.config/niri/material3ui.kdl, which includes it
    // material3ui.kdl is there and includes the settings: else they can't take
    // effect (Settings says so)
    bool included() const;
    // niri's own config sets this monitor: its block wins over ours
    bool userSetsOutput(const std::string& output) const;
    // niri couldn't read its config after our last write ("" when it could)
    const std::string& error() const { return m_error; }

    // A monitor's settings, kept (and applied live by the caller)
    static void saveOutput(const niri::Settings::Output& output);

    // Your niri config as niri reads it: config.kdl and what it includes,
    // in order, without the shell's settings. What Niri Settings shows for
    // an option it hasn't set. Without the shell's material3ui.kdl too (its
    // keys, startup and variables), but still what it includes for you
    std::vector<kdl::Node> yours(bool withShell = true) const;
    // Niri Settings' options back to your config's: these /niri/look keys
    // (all of them when empty) forgotten
    static void resetLook(const std::vector<std::string>& keys = {});

    Signal<> changed; // written, or niri's answer to it

private:
    NiriConfig();
    niri::Settings collect() const;
    void ensureIncluded();
    std::string m_written, m_error, m_cursor;
};

} // namespace m3
