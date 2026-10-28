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
    out += "}\n";

    // The focus ring in the wallpaper's colours
    if (!s.ringActive.empty() || !s.ringInactive.empty()) {
        out += "\nlayout {\n    focus-ring {\n";
        if (!s.ringActive.empty()) out += std::format("        active-color {}\n", quote(s.ringActive));
        if (!s.ringInactive.empty()) out += std::format("        inactive-color {}\n", quote(s.ringInactive));
        out += "    }\n}\n";
    }

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
