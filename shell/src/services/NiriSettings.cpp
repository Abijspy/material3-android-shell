#include "services/NiriSettings.h"

#include <cctype>
#include <format>

namespace m3::niri {

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

bool validKeys(const std::string& keys) {
    if (keys.empty() || keys.front() == '+' || keys.back() == '+' || keys.find("++") != std::string::npos) return false;
    for (char c : keys)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '_') return false;
    return true;
}

std::string render(const Settings& s) {
    std::string out =
        "// Written by Material3UI from its Settings: change these there (edits here\n"
        "// are overwritten). Included at the end of material3ui.kdl, so they win.\n";
    auto flag = [](std::string& to, const char* name, bool on) {
        if (on) to += std::format("        {}\n", name);
    };

    // The cursor, niri's and the apps' it starts
    if (!s.cursorTheme.empty()) {
        out += std::format("\ncursor {{\n    xcursor-theme {}\n    xcursor-size {}\n}}\n", quote(s.cursorTheme), s.cursorSize);
        out += std::format("\nenvironment {{\n    XCURSOR_THEME {}\n    XCURSOR_SIZE \"{}\"\n}}\n", quote(s.cursorTheme), s.cursorSize);
    }

    out += "\ninput {\n    touchpad {\n";
    flag(out, "tap", s.tap);
    flag(out, "natural-scroll", s.naturalScroll);
    flag(out, "dwt", s.dwt);
    out += std::format("        drag {}\n", s.tapDrag ? "true" : "false");
    flag(out, "drag-lock", s.dragLock);
    out += std::format("        click-method {}\n", s.clickFinger ? "\"clickfinger\"" : "\"button-areas\"");
    out += std::format("        scroll-factor {:.2f}\n", s.scrollFactor);
    out += std::format("        accel-speed {:.2f}\n", s.speed);
    out += std::format("        accel-profile {}\n", s.flat ? "\"flat\"" : "\"adaptive\"");
    out += "    }\n    mouse {\n";
    flag(out, "natural-scroll", s.mouseNaturalScroll);
    out += std::format("        accel-speed {:.2f}\n", s.speed);
    out += std::format("        accel-profile {}\n", s.flat ? "\"flat\"" : "\"adaptive\"");
    out += "    }\n";
    if (!s.layouts.empty() || s.repeatDelay > 0 || s.repeatRate > 0) {
        out += "    keyboard {\n";
        if (!s.layouts.empty()) {
            out += "        xkb {\n";
            out += std::format("            layout {}\n", quote(s.layouts));
            if (!s.layoutOptions.empty()) out += std::format("            options {}\n", quote(s.layoutOptions));
            out += "        }\n";
        }
        if (s.repeatDelay > 0) out += std::format("        repeat-delay {}\n", s.repeatDelay);
        if (s.repeatRate > 0) out += std::format("        repeat-rate {}\n", s.repeatRate);
        out += "    }\n";
    }
    const auto& l = s.look;
    if (l.focusFollowsMouse) out += "    focus-follows-mouse\n";
    if (l.warpMouse) out += "    warp-mouse-to-focus\n";
    if (l.backAndForth) out += std::format("    workspace-auto-back-and-forth {}\n", *l.backAndForth);
    out += "}\n";

    // The layout: what Niri Settings sets, and the focus ring and border
    // in the wallpaper's colours
    std::string layout;
    if (l.gaps) layout += std::format("    gaps {}\n", *l.gaps);
    if (l.centerColumn) layout += std::format("    center-focused-column {}\n", quote(*l.centerColumn));
    if (l.centerSingle) layout += std::format("    always-center-single-column {}\n", *l.centerSingle);
    if (l.columnWidth) layout += std::format("    default-column-width {{\n        proportion {:.5f}\n    }}\n", *l.columnWidth);
    std::string struts;
    const std::pair<const char*, std::optional<int>> edges[] = {
        {"left", l.strutLeft}, {"right", l.strutRight}, {"top", l.strutTop}, {"bottom", l.strutBottom}};
    for (const auto& [name, v] : edges)
        if (v) struts += std::format("        {} {}\n", name, *v);
    if (!struts.empty()) layout += "    struts {\n" + struts + "    }\n";
    auto section = [&](const char* name, std::optional<bool> on, std::optional<int> width, const std::string& active,
                       const std::string& inactive) {
        std::string body;
        if (on) body += *on ? "        on\n" : "        off\n";
        if (width) body += std::format("        width {}\n", *width);
        if (!active.empty()) body += std::format("        active-color {}\n", quote(active));
        if (!inactive.empty()) body += std::format("        inactive-color {}\n", quote(inactive));
        if (!body.empty()) layout += std::format("    {} {{\n", name) + body + "    }\n";
    };
    section("focus-ring", l.ringOn, l.ringWidth, s.ringActive, s.ringInactive);
    section("border", l.borderOn, l.borderWidth, s.borderActive, s.borderInactive);
    std::string shadow;
    if (l.shadowOn) shadow += *l.shadowOn ? "        on\n" : "        off\n";
    if (l.shadowSoftness) shadow += std::format("        softness {}\n", *l.shadowSoftness);
    if (l.shadowSpread) shadow += std::format("        spread {}\n", *l.shadowSpread);
    if (l.shadowOffset) shadow += std::format("        offset x=0 y={}\n", *l.shadowOffset);
    if (!shadow.empty()) layout += "    shadow {\n" + shadow + "    }\n";
    if (!layout.empty()) out += "\nlayout {\n" + layout + "}\n";

    // Every window's corners, as a rule after yours (a later rule wins)
    if (l.cornerRadius)
        out += std::format("\nwindow-rule {{\n    geometry-corner-radius {}\n    clip-to-geometry {}\n}}\n", *l.cornerRadius, *l.cornerRadius > 0);

    if (l.animations || l.slowdown) {
        out += "\nanimations {\n";
        if (l.animations) out += *l.animations ? "    on\n" : "    off\n";
        if (l.slowdown) out += std::format("    slowdown {:.2f}\n", *l.slowdown);
        out += "}\n";
    }
    if (l.preferNoCsd) out += std::format("\nprefer-no-csd {}\n", *l.preferNoCsd);
    if (l.skipHotkeys) out += std::format("\nhotkey-overlay {{\n    skip-at-startup {}\n}}\n", *l.skipHotkeys);
    if (l.overviewZoom) out += std::format("\noverview {{\n    zoom {:.2f}\n}}\n", *l.overviewZoom);
    if (l.hotCornersOff) out += "\ngestures {\n    hot-corners {\n        off\n    }\n}\n";

    for (const auto& o : s.outputs) {
        out += std::format("\noutput {} {{\n", quote(o.name));
        if (o.off) {
            out += "    off\n}\n";
            continue;
        }
        if (!o.mode.empty()) out += std::format("    mode {}\n", quote(o.mode));
        if (o.scale > 0) out += std::format("    scale {:.2f}\n", o.scale);
        if (!o.transform.empty()) out += std::format("    transform {}\n", quote(o.transform));
        if (o.position) out += std::format("    position x={} y={}\n", o.position->first, o.position->second);
        if (o.vrr) out += "    variable-refresh-rate\n";
        out += "}\n";
    }

    if (!s.binds.empty()) {
        out += "\nbinds {\n";
        for (const auto& b : s.binds) {
            if (!validKeys(b.keys) || b.command.empty()) continue;
            out += std::format("    {}{} {{ spawn \"sh\" \"-c\" {}; }}\n", b.keys,
                               b.title.empty() ? "" : " hotkey-overlay-title=" + quote(b.title), quote(b.command));
        }
        out += "}\n";
    }
    return out;
}

} // namespace m3::niri
