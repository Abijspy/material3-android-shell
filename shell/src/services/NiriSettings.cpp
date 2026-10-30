#include "services/NiriSettings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <regex>
#include <string_view>

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

bool validAction(const std::string& action) {
    if (action.empty() || action.front() == '-' || action.back() == '-') return false;
    return std::ranges::all_of(action, [](char c) { return (c >= 'a' && c <= 'z') || c == '-'; });
}

bool validVariable(const std::string& name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    return std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

bool validRegex(const std::string& pattern) {
    // niri's regexes have no look-around or back-references, which
    // ECMAScript's (std::regex) would take
    if (pattern.empty() || pattern.find("(?=") != std::string::npos || pattern.find("(?!") != std::string::npos ||
        pattern.find("(?<") != std::string::npos)
        return false;
    for (size_t i = 0; i + 1 < pattern.size(); ++i)
        if (pattern[i] == '\\' && std::isdigit(static_cast<unsigned char>(pattern[i + 1]))) return false;
    try {
        std::regex check(pattern);
    } catch (const std::regex_error&) {
        return false;
    }
    return true;
}

const std::vector<AnimationDef>& animationDefs() {
    static const std::vector<AnimationDef> defs = {
        {"window-open", "Windows opening", false, 1, 800, 150, "ease-out-expo"},
        {"window-close", "Windows closing", false, 1, 800, 150, "ease-out-quad"},
        {"window-movement", "Windows moving", true, 1, 800, 200, "ease-out-quad"},
        {"window-resize", "Windows resizing", true, 1, 800, 200, "ease-out-quad"},
        {"horizontal-view-movement", "Scrolling the columns", true, 1, 800, 200, "ease-out-quad"},
        {"workspace-switch", "Switching workspaces", true, 1, 1000, 200, "ease-out-quad"},
        {"overview-open-close", "The overview", true, 1, 800, 200, "ease-out-quad"},
        {"screenshot-ui-open", "niri's screenshot tool", false, 1, 800, 200, "ease-out-quad"},
    };
    return defs;
}

bool validCurve(const std::string& curve) {
    return curve == "linear" || curve == "ease-out-quad" || curve == "ease-out-cubic" || curve == "ease-out-expo" || curve == "cubic-bezier";
}

std::string exactly(const std::string& text) {
    std::string out = "^";
    for (char c : text) {
        if (std::string_view("\\^$.|?*+()[]{}").find(c) != std::string_view::npos) out += '\\';
        out += c;
    }
    return out + "$";
}

std::string render(const Settings& s) {
    std::string out =
        "// Written by Material3UI from its Settings: change these there (edits here\n"
        "// are overwritten). Included at the end of material3ui.kdl, so they win.\n";
    auto flag = [](std::string& to, const char* name, bool on) {
        if (on) to += std::format("        {}\n", name);
    };

    // The cursor, niri's and the apps' it starts
    if (!s.cursorTheme.empty())
        out += std::format("\ncursor {{\n    xcursor-theme {}\n    xcursor-size {}\n}}\n", quote(s.cursorTheme), s.cursorSize);
    // One environment section (niri refuses two in a file): the cursor's,
    // then yours
    std::string env;
    if (!s.cursorTheme.empty()) env += std::format("    XCURSOR_THEME {}\n    XCURSOR_SIZE \"{}\"\n", quote(s.cursorTheme), s.cursorSize);
    for (const auto& [name, value] : s.environment)
        if (validVariable(name)) env += std::format("    {} {}\n", name, quote(value));
    if (!env.empty()) out += "\nenvironment {\n" + env + "}\n";
    for (const auto& cmd : s.startup)
        if (!cmd.empty()) out += std::format("\nspawn-sh-at-startup {}\n", quote(cmd));

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
    if (l.tabletsOff) out += "    tablet {\n        off\n    }\n";
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

    // The shell's windows (Settings, Niri Settings…): rounded, and blurred,
    // as on Hyprland; first, so a corner radius of yours below wins. They
    // don't speak xdg-decoration, so niri would draw their focus ring and
    // border as a solid fill behind them, showing through their translucent
    // background in place of the blur (all but full screen): a frame instead
    out += std::format("\nwindow-rule {{\n    match app-id=\"^material3ui-\"\n    geometry-corner-radius {}\n    clip-to-geometry true\n"
                       "    draw-border-with-background false\n",
                       std::clamp(s.shellRadius, 0, 64));
    if (s.shellBlur) out += "    background-effect {\n        blur true\n        xray false\n    }\n";
    out += "}\n";
    // Its layers ask for blur themselves; over windows it should show them
    if (s.shellBlur)
        out += "\nlayer-rule {\n    match namespace=\"^material3ui-(bar|frame|popup|toasts|osd|dock|switcher)$\"\n"
               "    background-effect {\n        xray false\n    }\n}\n";
    // The overview's backdrop: the blurred wallpaper in place of the sharp one
    if (s.blurredBackdrop)
        out += "\nlayer-rule {\n    match namespace=\"^material3ui-backdrop$\"\n    place-within-backdrop true\n}\n"
               "\nlayer-rule {\n    match namespace=\"^material3ui-wallpaper$\"\n    place-within-backdrop false\n}\n";
    std::string blur;
    if (l.blurOn) blur += *l.blurOn ? "    on\n" : "    off\n";
    if (l.blurPasses) blur += std::format("    passes {}\n", std::clamp(*l.blurPasses, 1, 8));
    if (l.blurOffset) blur += std::format("    offset {:.2f}\n", std::clamp(*l.blurOffset, 0.5, 20.0));
    if (l.blurNoise) blur += std::format("    noise {:.3f}\n", std::clamp(*l.blurNoise, 0.0, 1.0));
    if (l.blurSaturation) blur += std::format("    saturation {:.2f}\n", std::clamp(*l.blurSaturation, 0.0, 4.0));
    if (!blur.empty()) out += "\nblur {\n" + blur + "}\n";

    // Every window's corners, as a rule after yours (a later rule wins)
    if (l.cornerRadius)
        out += std::format("\nwindow-rule {{\n    geometry-corner-radius {}\n    clip-to-geometry {}\n}}\n", *l.cornerRadius, *l.cornerRadius > 0);
    // Yours, per app: only what they set, and never one niri couldn't read
    for (const auto& r : s.rules) {
        const bool app = !r.appId.empty(), title = !r.title.empty();
        if ((!app && !title) || (app && !validRegex(r.appId)) || (title && !validRegex(r.title))) continue;
        std::string body = "    match";
        if (app) body += " app-id=" + quote(r.appId);
        if (title) body += " title=" + quote(r.title);
        body += "\n";
        const size_t bare = body.size();
        if (r.floating) body += "    open-floating true\n";
        if (r.maximized) body += "    open-maximized true\n";
        if (r.fullscreen) body += "    open-fullscreen true\n";
        if (r.opacity < 0.995) body += std::format("    opacity {:.2f}\n", std::clamp(r.opacity, 0.05, 1.0));
        if (r.width > 0) body += std::format("    default-column-width {{\n        proportion {:.5f}\n    }}\n", std::clamp(r.width, 0.1, 1.0));
        if (r.hide) body += "    block-out-from \"screen-capture\"\n";
        // Its own colour, for whichever of the ring and border is on
        if (r.color.size() == 7 && r.color[0] == '#' &&
            std::all_of(r.color.begin() + 1, r.color.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
            for (const char* part : {"border", "focus-ring"})
                body += std::format("    {} {{\n        active-color {}\n        inactive-color {}\n    }}\n", part, quote(r.color), quote(r.color + "80"));
        if (body.size() > bare) out += "\nwindow-rule {\n" + body + "}\n";
    }

    // One animations section (niri refuses two in a file): the switch, the
    // speed, then each animation set here, merged into yours by niri
    std::string anims;
    if (l.animations) anims += *l.animations ? "    on\n" : "    off\n";
    if (l.slowdown) anims += std::format("    slowdown {:.2f}\n", *l.slowdown);
    for (const auto& a : l.anims) {
        if (std::ranges::none_of(animationDefs(), [&](const AnimationDef& d) { return a.name == d.name; })) continue;
        std::string body;
        if (a.off) body += "        off\n";
        if (a.kind == "spring") {
            body += std::format("        spring damping-ratio={:.2f} stiffness={} epsilon=0.0001\n", std::clamp(a.damping, 0.05, 2.0),
                                std::clamp(static_cast<int>(std::lround(a.stiffness)), 10, 5000));
        } else if (a.kind == "easing" && validCurve(a.curve)) {
            body += std::format("        duration-ms {}\n", std::clamp(a.duration, 10, 5000));
            if (a.curve == "cubic-bezier")
                body += std::format("        curve \"cubic-bezier\" {:.3f} {:.3f} {:.3f} {:.3f}\n", std::clamp(a.bezier[0], 0.0, 1.0), a.bezier[1],
                                    std::clamp(a.bezier[2], 0.0, 1.0), a.bezier[3]);
            else
                body += std::format("        curve {}\n", quote(a.curve));
        }
        if (!body.empty()) anims += std::format("    {} {{\n", a.name) + body + "    }\n";
    }
    if (!anims.empty()) out += "\nanimations {\n" + anims + "}\n";
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
            if (!validKeys(b.keys)) continue;
            const std::string title = b.title.empty() ? "" : " hotkey-overlay-title=" + quote(b.title);
            if (!b.action.empty()) {
                if (validAction(b.action)) out += std::format("    {}{} {{ {}; }}\n", b.keys, title, b.action);
            } else if (!b.command.empty()) {
                out += std::format("    {}{} {{ spawn \"sh\" \"-c\" {}; }}\n", b.keys, title, quote(b.command));
            }
        }
        out += "}\n";
    }
    return out;
}

} // namespace m3::niri
