#include "services/Effects.h"

#include "core/Config.h"
#include "core/Process.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "services/Hyprland.h"
#include "ui/Theme.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string luaFile() {
    return paths::home() + "/.config/hypr/material3ui/effects_custom.lua";
}

std::string readAll(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The blurred layers; the wallpaper and lock never are
const char* kLayers[] = {"bar", "popup", "toasts", "osd", "dock", "switcher"};

} // namespace

Effects& Effects::get() {
    static Effects instance;
    return instance;
}

Effects::Effects() {
    apply();
    Config::get().changed.connectForever([this] { apply(); });
}

// Settings → Mouse & touchpad: Hyprland's input options, in a Lua table
// shell.lua merges into its config (legacy configs: runtime keywords, where
// two of them are spelt with dashes: tap-to-click, tap-and-drag)
void Effects::applyInput() {
    auto& h = Hyprland::get();
    const auto& c = Config::get();
    auto flag = [&](const char* key, bool fallback) { return c.value<bool>(std::string("/input/") + key, fallback); };
    const bool tap = flag("tapToClick", true), natural = flag("naturalScroll", true), dwt = flag("disableWhileTyping", true);
    const bool fingers = flag("clickFinger", true), tapDrag = flag("tapAndDrag", true), dragLock = flag("dragLock", false);
    const double scroll = std::clamp(c.value<double>("/input/scrollFactor", 0.7), 0.1, 3.0);
    const double speed = std::clamp(c.value<double>("/input/sensitivity", 0.0), -1.0, 1.0);
    const std::string accel = c.value<std::string>("/input/accelProfile", "adaptive") == "flat" ? "flat" : "adaptive";
    auto b = [](bool v) { return v ? "true" : "false"; };
    if (!h.usingLua()) {
        const auto key = std::format("{}{}{}{}{}{}{:.2f}{:.2f}{}", tap, natural, dwt, fingers, tapDrag, dragLock, scroll, speed, accel);
        if (key == m_inputApplied) return;
        m_inputApplied = key;
        for (const auto& [k, v] : std::initializer_list<std::pair<const char*, std::string>>{
                 {"input:touchpad:tap-to-click", b(tap)}, {"input:touchpad:natural_scroll", b(natural)},
                 {"input:touchpad:disable_while_typing", b(dwt)}, {"input:touchpad:clickfinger_behavior", b(fingers)},
                 {"input:touchpad:tap-and-drag", b(tapDrag)}, {"input:touchpad:drag_lock", dragLock ? "1" : "0"},
                 {"input:touchpad:scroll_factor", std::format("{:.2f}", scroll)},
                 {"input:sensitivity", std::format("{:.2f}", speed)}, {"input:accel_profile", accel}})
            h.request(std::format("keyword {} {}", k, v));
        return;
    }
    const std::string lua = std::format(
        "-- Written by Material3UI (Settings → System → Mouse & touchpad).\n"
        "-- Change these there; edits made here are overwritten.\n"
        "return {{\n\tsensitivity = {:.2f},\n\taccel_profile = \"{}\",\n\ttouchpad = {{\n"
        "\t\ttap_to_click = {},\n\t\tnatural_scroll = {},\n\t\tdisable_while_typing = {},\n"
        "\t\tclickfinger_behavior = {},\n\t\ttap_and_drag = {},\n\t\tdrag_lock = {},\n\t\tscroll_factor = {:.2f},\n\t}},\n}}\n",
        speed, accel, b(tap), b(natural), b(dwt), b(fingers), b(tapDrag), dragLock ? 1 : 0, scroll);
    const std::string file = (fs::path(luaFile()).parent_path() / "input_custom.lua").string();
    if (readAll(file) == lua) return;
    std::ofstream(file) << lua;
    // Applied live: a reload would re-apply everything (the screen flickers);
    // the file keeps it for the next one
    std::string table = lua.substr(lua.find("return ") + 7);
    std::ranges::replace(table, '\n', ' ');
    std::ranges::replace(table, '\t', ' ');
    log::info("input: touchpad and pointer settings written and applied");
    h.request("eval hl.config({ input = " + table + " })");
}

namespace {

// Colour correction as Hyprland screen shaders (GLSL ES 3.0), written by the
// shell itself so they need no install step
const char* kShaderHead = R"(#version 300 es
// Written by Material3UI (Settings → Accessibility → Colour correction)
precision highp float;
in vec2 v_texcoord;
uniform sampler2D tex;
out vec4 fragColor;
)";

// Daltonize: simulate what the eye misses (in LMS), then shift that
// difference into colours it can tell apart
const char* kDaltonize = R"(
vec3 daltonize(vec3 c, int kind) {
    float L = 17.8824 * c.r + 43.5161 * c.g + 4.11935 * c.b;
    float M = 3.45565 * c.r + 27.1554 * c.g + 3.86714 * c.b;
    float S = 0.0299566 * c.r + 0.184309 * c.g + 1.46709 * c.b;
    float l = L, m = M, s = S;
    if (kind == 0) l = 2.02344 * M - 2.52581 * S;          // protanopia
    else if (kind == 1) m = 0.494207 * L + 1.24827 * S;    // deuteranopia
    else s = -0.395913 * L + 0.801109 * M;                 // tritanopia
    vec3 sim = vec3(0.0809444479 * l - 0.130504409 * m + 0.116721066 * s,
                    -0.0102485335 * l + 0.0540193266 * m - 0.113614708 * s,
                    -0.000365296938 * l - 0.00412161469 * m + 0.693511405 * s);
    vec3 err = c - sim;
    return clamp(c + vec3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b), 0.0, 1.0);
}
)";

std::string shaderSource(const std::string& filter) {
    std::string body;
    if (filter == "grayscale") body = "vec3 o = vec3(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)));";
    else if (filter == "invert") body = "vec3 o = vec3(1.0) - c.rgb;";
    else if (filter == "dim") body = "vec3 o = c.rgb * 0.55;";
    else if (filter == "protanopia") body = "vec3 o = daltonize(c.rgb, 0);";
    else if (filter == "deuteranopia") body = "vec3 o = daltonize(c.rgb, 1);";
    else if (filter == "tritanopia") body = "vec3 o = daltonize(c.rgb, 2);";
    else return "";
    return std::string(kShaderHead) + (filter.find("anopia") != std::string::npos ? kDaltonize : "") +
           "\nvoid main() {\n    vec4 c = texture(tex, v_texcoord);\n    " + body + "\n    fragColor = vec4(o, c.a);\n}\n";
}

} // namespace

// The magnifier (Hyprland's cursor zoom) and a colour filter: applied at
// once with eval, and kept in a11y_custom.lua for the next reload or login
void Effects::applyAccessibility() {
    auto& h = Hyprland::get();
    const auto& c = Config::get();
    const double zoom = std::clamp(c.value<double>("/accessibility/zoom", 1.0), 1.0, 4.0);
    const std::string filter = c.value<std::string>("/accessibility/colorFilter", "none");
    std::string shader;
    if (const std::string src = shaderSource(filter); !src.empty()) {
        const std::string dir = paths::stateDir() + "/shaders";
        std::error_code ec;
        fs::create_directories(dir, ec);
        shader = dir + "/" + filter + ".frag";
        if (readAll(shader) != src) std::ofstream(shader) << src;
    }
    const std::string key = std::format("{:.2f}|{}", zoom, shader);
    if (key == m_a11yApplied) return;
    m_a11yApplied = key;
    if (!h.usingLua()) {
        h.request(std::format("keyword cursor:zoom_factor {:.2f}", zoom));
        h.request("keyword decoration:screen_shader " + (shader.empty() ? std::string("[[EMPTY]]") : shader));
        return;
    }
    h.request(std::format("eval hl.config({{ cursor = {{ zoom_factor = {:.2f} }}, decoration = {{ screen_shader = \"{}\" }} }})", zoom, shader));
    const std::string lua = std::format("-- Written by Material3UI (Settings → Accessibility).\n"
                                        "-- Change these there; edits made here are overwritten.\n"
                                        "return {{\n\tzoom_factor = {:.2f},\n\tscreen_shader = \"{}\",\n}}\n",
                                        zoom, shader);
    const std::string file = (fs::path(luaFile()).parent_path() / "a11y_custom.lua").string();
    if (readAll(file) != lua) std::ofstream(file) << lua;
}

// Screen sharing: xdg-desktop-portal-hyprland's config, with the shell's
// picker (unless switched off), whether apps may share again without
// asking, and the frame rate. A file someone wrote by hand is left alone.
void Effects::applySharing() {
    const auto& c = Config::get();
    const std::string file = paths::home() + "/.config/hypr/xdph.conf";
    constexpr const char* kMarker = "# Written by Material3UI";
    const std::string now = readAll(file);
    if (!now.empty() && now.find(kMarker) == std::string::npos) return;
    const bool picker = c.value<bool>("/share/picker", true) && access("/usr/lib/material3ui/material3ui-share-picker", X_OK) == 0;
    std::string conf = std::string(kMarker) + " (Settings → Tools → Screen sharing).\n"
                       "# Change it there; edits made here are overwritten. Delete this\n"
                       "# comment to keep your own.\nscreencopy {\n";
    if (picker) conf += "    custom_picker_binary = /usr/lib/material3ui/material3ui-share-picker\n";
    conf += std::format("    allow_token_by_default = {}\n    max_fps = {}\n}}\n", c.value<bool>("/share/remember", false) ? "true" : "false",
                        std::clamp(c.value<int>("/share/maxFps", 60), 15, 240));
    if (conf == now) return;
    std::ofstream(file) << conf;
    // The portal reads it when it starts
    log::info("screen sharing: xdph.conf written; restarting the portal");
    run({"systemctl", "--user", "try-restart", "xdg-desktop-portal-hyprland.service"}, nullptr);
}

void Effects::apply() {
    auto& h = Hyprland::get();
    if (!h.available()) return;
    auto& t = Theme::get();
    const auto& c = Config::get();
    applyInput();
    applyAccessibility();
    applySharing();
    // The cursor: its theme (Hyprland Settings → Cursor) and size
    // (Accessibility). Hyprland's own, GTK's, and the variables apps
    // started from now on read
    const std::string theme = c.value<std::string>("/accessibility/cursorTheme", "");
    // Back from a chosen theme to the system's: "default" again, not
    // whatever GTK was last told
    const bool cleared = theme.empty() && !m_cursorTheme.empty();
    if (const int cursor = c.value<int>("/accessibility/cursorSize", 0);
        (cursor > 0 || !theme.empty() || cleared) && (cursor != m_cursor || theme != m_cursorTheme)) {
        m_cursor = cursor;
        m_cursorTheme = theme;
        const int px = cursor > 0 ? cursor : 24;
        const std::string set = cleared ? "default" : theme;
        std::string script = set.empty() ? "t=$(gsettings get org.gnome.desktop.interface cursor-theme | tr -d \"'\"); t=${t:-Adwaita}; "
                                         : "t='" + set + "'; gsettings set org.gnome.desktop.interface cursor-theme \"$t\"; ";
        script += std::format("hyprctl setcursor \"$t\" {0}; gsettings set org.gnome.desktop.interface cursor-size {0}; "
                              "hyprctl eval \"hl.env('XCURSOR_THEME', '$t') hl.env('XCURSOR_SIZE', '{0}') "
                              "hl.env('HYPRCURSOR_THEME', '$t') hl.env('HYPRCURSOR_SIZE', '{0}')\"",
                              px);
        run({"sh", "-c", script}, nullptr);
    }
    // How the blur looks (size, passes, noise…) is Hyprland Settings' alone:
    // only what the shell's own layers get is written here
    const bool barBlur = c.value<bool>("/statusBar/blur", true), dockBlur = c.value<bool>("/dock/blur", true);
    // Pixels fainter than this are left sharp: the clear space around a
    // popup's card or between bar capsules. Kept under the panels' own alpha.
    const float ignore = std::max(0.05f, std::min(t.barOpacity(), t.panelOpacity()) - 0.15f);

    if (!h.usingLua()) {
        // Legacy hyprland.conf: runtime keywords, gone after a config reload.
        // Rules can't be removed this way; turning blur off needs a reload.
        const auto key = std::format("{} {:.2f} {} {}", t.blur(), ignore, barBlur, dockBlur);
        if (key == m_applied) return;
        m_applied = key;
        if (t.blur())
            for (const char* l : kLayers) {
                if ((l == std::string("bar") && !barBlur) || (l == std::string("dock") && !dockBlur)) continue;
                h.request(std::format("keyword layerrule blur,material3ui-{}", l));
                h.request(std::format("keyword layerrule ignorealpha {:.2f},material3ui-{}", ignore, l));
            }
        return;
    }

    const std::string lua = std::format("-- Written by Material3UI (Settings → Appearance → Effects).\n"
                                        "-- Change these there; edits made here are overwritten.\n"
                                        "return {{\n\tblur = {},\n\tignore_alpha = {:.2f},\n\tbar = {},\n\tdock = {},\n}}\n",
                                        t.blur() ? "true" : "false", ignore, barBlur ? "true" : "false", dockBlur ? "true" : "false");
    if (readAll(luaFile()) == lua) return; // unchanged: no reload
    std::error_code ec;
    fs::create_directories(fs::path(luaFile()).parent_path(), ec);
    std::ofstream(luaFile()) << lua;
    // Layer rules change only with a reload
    log::info("effects: blur {}; reloading Hyprland", t.blur() ? "on" : "off");
    h.request("reload");
}

} // namespace m3
