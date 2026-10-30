#pragma once

#include <array>
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
        bool focusFollowsMouse = false, warpMouse = false, hotCornersOff = false, tabletsOff = false;
        std::optional<double> overviewZoom;
        // niri's blur (26.04), for everything blurred
        std::optional<bool> blurOn;
        std::optional<int> blurPasses;
        std::optional<double> blurOffset, blurNoise, blurSaturation;

        // Each animation's own: off (niri takes no "on" there), or its
        // motion, a spring or a curve; "" leaves your config's
        struct Animation {
            std::string name; // one of animationDefs()
            bool off = false;
            std::string kind; // "spring", "easing"
            double damping = 1, stiffness = 800;
            int duration = 200;                      // ms
            std::string curve = "ease-out-quad";     // linear, ease-out-quad/-cubic/-expo, cubic-bezier
            std::array<double, 4> bezier{0.05, 0.7, 0.1, 1};
        };
        std::vector<Animation> anims;
    } look;

    // The shell's own surfaces, as Hyprland shows them: its windows rounded
    // (and blurred, with `shellBlur`), its bar, panels and notifications
    // blurring the windows behind them rather than only the wallpaper (niri's
    // default "xray" blur), and with `blurredBackdrop` the overview's
    // backdrop its blurred copy of the wallpaper (material3ui-backdrop)
    bool shellBlur = false;
    int shellRadius = 18;
    bool blurredBackdrop = false;

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
        std::string action;               // a niri action ("maximize-column") in place of the command
    };
    std::vector<Bind> binds;

    // Window rules, after yours (a later rule wins): the windows an app id
    // and/or title regex matches, and what they open as. Unset (or false,
    // 1, 0) leaves your config's
    struct Rule {
        std::string appId, title;
        bool floating = false, maximized = false, fullscreen = false, hide = false; // hide: from screen sharing
        double opacity = 1;                                                        // 1: as it is
        double width = 0;                                                          // a new column's share; 0: as it is
        std::string color; // "#rrggbb": its border and focus ring (dimmer when unfocused); "": as they are
    };
    std::vector<Rule> rules;

    std::vector<std::string> startup;                              // commands, run by sh when niri starts
    std::vector<std::pair<std::string, std::string>> environment; // yours, after the cursor's
};

// niri's animations Niri Settings sets, with their defaults (26.04's)
struct AnimationDef {
    const char* name;  // workspace-switch…
    const char* title; // "Switching workspaces"
    bool spring;       // its default motion, else a curve
    double damping, stiffness;
    int duration;
    const char* curve;
};
const std::vector<AnimationDef>& animationDefs();
// The curves niri knows (cubic-bezier with its four numbers)
bool validCurve(const std::string& curve);

// The file's text
std::string render(const Settings& s);
// A KDL string, quoted and escaped
std::string quote(const std::string& s);
// A key combination niri can read ("Mod+Shift+K"): modifiers and a key,
// joined by +, nothing else
bool validKeys(const std::string& keys);
// A niri action's name ("focus-column-left"): lowercase words and hyphens
bool validAction(const std::string& action);
// A variable's name: letters, digits and _, not starting with a digit
bool validVariable(const std::string& name);
// A regex niri can read (else the whole file would be refused)
bool validRegex(const std::string& pattern);
// The regex matching exactly this app id
std::string exactly(const std::string& text);

} // namespace m3::niri
