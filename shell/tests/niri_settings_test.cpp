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

    // For a check by niri itself: niri_settings_test <file> writes the full one
    if (argc > 1) std::ofstream(argv[1]) << full;

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
