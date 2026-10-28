// The settings file the shell writes for niri: every touchpad flag written
// (niri replaces the section), only what's set otherwise, strings escaped
#include "services/NiriSettings.h"

#include <cstdio>
#include <fstream>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

int main(int argc, char** argv) {
    niri::Settings s;
    const std::string plain = niri::render(s);
    expect(has(plain, "    touchpad {\n        tap\n        natural-scroll\n        dwt\n        drag true\n"), "touchpad defaults");
    expect(has(plain, "        click-method \"clickfinger\"\n        scroll-factor 0.70\n"), "click and scroll");
    expect(!has(plain, "cursor {") && !has(plain, "keyboard {") && !has(plain, "layout {") && !has(plain, "output ") && !has(plain, "binds {"),
           "nothing that isn't set");

    s.tap = false;
    s.naturalScroll = false;
    s.tapDrag = false;
    s.flat = true;
    s.speed = -0.25;
    s.cursorTheme = "Bibata-Modern-Classic";
    s.cursorSize = 32;
    s.layouts = "us,de";
    s.layoutOptions = "grp:win_space_toggle";
    s.repeatDelay = 400;
    s.ringActive = "#d0bcff";
    s.ringInactive = "#49454f";
    s.outputs.push_back({"eDP-1", false, "1920x1080@60.000", 1.5, "90", std::pair{0, 0}, true});
    s.outputs.push_back({"HDMI-A-1", true, "", 0, "", std::nullopt, false});
    s.binds.push_back({"Mod+Shift+K", "notify-send \"hi\"", "Say hi"});
    s.binds.push_back({"Mod+;rm", "nope", ""});
    const std::string full = niri::render(s);
    expect(!has(full, "        tap\n") && !has(full, "    touchpad {\n        natural-scroll"), "flags left out when off");
    expect(has(full, "        drag false\n") && has(full, "accel-profile \"flat\"") && has(full, "accel-speed -0.25"), "drag, profile, speed");
    expect(has(full, "cursor {\n    xcursor-theme \"Bibata-Modern-Classic\"\n    xcursor-size 32\n}"), "the cursor");
    expect(has(full, "XCURSOR_THEME \"Bibata-Modern-Classic\"\n    XCURSOR_SIZE \"32\""), "the apps' cursor");
    expect(has(full, "xkb {\n            layout \"us,de\"\n            options \"grp:win_space_toggle\"\n        }\n        repeat-delay 400\n"),
           "layouts and repeat");
    expect(!has(full, "repeat-rate"), "repeat rate left to niri");
    expect(has(full, "focus-ring {\n        active-color \"#d0bcff\"\n        inactive-color \"#49454f\""), "the ring's colours");
    expect(has(full, "output \"eDP-1\" {\n    mode \"1920x1080@60.000\"\n    scale 1.50\n    transform \"90\"\n    position x=0 y=0\n"
                     "    variable-refresh-rate\n}"),
           "a monitor");
    expect(has(full, "output \"HDMI-A-1\" {\n    off\n}"), "a monitor off");
    expect(has(full, "Mod+Shift+K hotkey-overlay-title=\"Say hi\" { spawn \"sh\" \"-c\" \"notify-send \\\"hi\\\"\"; }"), "a key, quoted");
    expect(!has(full, "nope"), "a key niri can't read is left out");

    expect(niri::validKeys("Mod+Shift+K") && niri::validKeys("XF86AudioMute") && niri::validKeys("Ctrl+Alt+Delete"), "keys");
    expect(!niri::validKeys("") && !niri::validKeys("Mod+") && !niri::validKeys("Mod++K") && !niri::validKeys("Mod K"), "not keys");
    expect(niri::quote("a\"b\\c") == "\"a\\\"b\\\\c\"", "quoting");

    // Niri Settings: only what's set, the layout in one section, the flags
    // niri can't switch off only when on
    niri::Settings n;
    auto& l = n.look;
    l.gaps = 8;
    l.centerColumn = "on-overflow";
    l.columnWidth = 0.5;
    l.strutTop = 30;
    l.borderOn = true;
    l.borderWidth = 2;
    l.shadowOn = false;
    l.cornerRadius = 12;
    l.animations = false;
    l.slowdown = 1.5;
    l.preferNoCsd = false;
    l.focusFollowsMouse = true;
    l.backAndForth = false;
    l.hotCornersOff = true;
    l.overviewZoom = 0.4;
    n.borderActive = "#d0bcff";
    const std::string look = niri::render(n);
    expect(has(look, "\nlayout {\n    gaps 8\n    center-focused-column \"on-overflow\"\n    default-column-width {\n        proportion 0.50000\n"),
           "the layout's values");
    expect(has(look, "    struts {\n        top 30\n    }\n"), "only the strut that's set");
    expect(has(look, "    border {\n        on\n        width 2\n        active-color \"#d0bcff\"\n    }\n"), "the border");
    expect(has(look, "    shadow {\n        off\n    }\n") && !has(look, "focus-ring {"), "the shadow; no ring when nothing's set");
    expect(look.find("\nlayout {") == look.rfind("\nlayout {"), "one layout section");
    expect(has(look, "window-rule {\n    geometry-corner-radius 12\n    clip-to-geometry true\n}"), "corners");
    expect(has(look, "animations {\n    off\n    slowdown 1.50\n}") && has(look, "\nprefer-no-csd false\n"), "animations, decorations");
    expect(has(look, "    focus-follows-mouse\n") && !has(look, "warp-mouse-to-focus") && has(look, "workspace-auto-back-and-forth false"),
           "input behaviour");
    expect(has(look, "hot-corners {\n        off\n") && has(look, "overview {\n    zoom 0.40\n}") && !has(look, "hotkey-overlay"),
           "gestures and the overview");

    // For a check by niri itself: niri_settings_test <file> [<file>] writes
    // the full one, and the one with Niri Settings' options
    if (argc > 1) std::ofstream(argv[1]) << full;
    if (argc > 2) std::ofstream(argv[2]) << look;

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
