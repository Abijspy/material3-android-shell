#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

// The shell's settings for niri, as KDL: what Settings changes that niri
// itself does (the cursor, touchpad and mouse, keyboard layouts and repeat,
// the focus ring's colours from the palette, monitors, your own keys).
// Written to ~/.config/niri/material3ui-settings.kdl, which material3ui.kdl
// includes last, so they win. Apart from the service for the unit tests.
namespace m3::niri {

struct Settings {
    std::string cursorTheme; // "": niri's own
    int cursorSize = 24;

    // Touchpad and mouse: niri replaces these sections whole, so every
    // one is written (a flag left out is off)
    bool tap = true, naturalScroll = true, dwt = true, clickFinger = true, tapDrag = true, dragLock = false;
    double scrollFactor = 0.7, speed = 0; // speed: -1 … 1
    bool flat = false;                    // acceleration profile
    bool mouseNaturalScroll = false;

    std::string layouts, layoutOptions; // "us,de", "grp:win_space_toggle"; "" leaves niri's
    int repeatDelay = 0, repeatRate = 0; // 0: niri's

    std::string ringActive, ringInactive; // "#rrggbb"; "": niri's
    std::string borderActive, borderInactive;

    // Niri Settings: how windows look and behave. Only what's set is
    // written, the rest staying as your config.kdl has it: niri merges
    // these sections field by field
    struct Look {
        std::optional<int> gaps;
        std::optional<std::string> centerColumn; // never, always, on-overflow
        std::optional<bool> centerSingle;        // always centre a lone column
        std::optional<double> columnWidth;       // a new column's share of the screen
        std::optional<int> strutLeft, strutRight, strutTop, strutBottom;
        std::optional<bool> ringOn, borderOn, shadowOn;
        std::optional<int> ringWidth, borderWidth, shadowSoftness, shadowSpread, shadowOffset;
        std::optional<int> cornerRadius; // every window's, clipped to it
        std::optional<bool> animations;
        std::optional<double> slowdown;
        std::optional<bool> preferNoCsd, backAndForth, skipHotkeys;
        // These niri can only switch on (they take no "false"): written when true
        bool focusFollowsMouse = false, warpMouse = false, hotCornersOff = false;
        std::optional<double> overviewZoom;
    } look;

    struct Output {
        std::string name;
        bool off = false;
        std::string mode;      // "1920x1080@60.000"; "" niri's pick
        double scale = 0;      // 0: automatic
        std::string transform; // normal, 90, 180, 270, flipped…; "" as it is
        std::optional<std::pair<int, int>> position;
        bool vrr = false;
    };
    std::vector<Output> outputs;

    struct Bind {
        std::string keys, command, title; // "Mod+Shift+K", a shell command, the hotkey overlay's name
    };
    std::vector<Bind> binds;
};

// The file's text
std::string render(const Settings& s);
// A KDL string, quoted and escaped
std::string quote(const std::string& s);
// A key combination niri can read ("Mod+Shift+K"): modifiers and a key,
// joined by +, nothing else
bool validKeys(const std::string& keys);

} // namespace m3::niri
