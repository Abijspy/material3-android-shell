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
const char* kLayers[] = {"bar", "popup", "toasts", "osd", "dock"};

} // namespace

Effects& Effects::get() {
    static Effects instance;
    return instance;
}

Effects::Effects() {
    apply();
    Config::get().changed.connectForever([this] { apply(); });
}

void Effects::apply() {
    auto& h = Hyprland::get();
    if (!h.available()) return;
    auto& t = Theme::get();
    const auto& c = Config::get();
    // Accessibility → cursor size: Hyprland's cursor and GTK's follow it
    if (const int cursor = c.value<int>("/accessibility/cursorSize", 0); cursor > 0 && cursor != m_cursor) {
        m_cursor = cursor;
        run({"sh", "-c", std::format("t=$(gsettings get org.gnome.desktop.interface cursor-theme | tr -d \"'\"); "
                                     "hyprctl setcursor \"${{t:-Adwaita}}\" {0}; "
                                     "gsettings set org.gnome.desktop.interface cursor-size {0}", cursor)},
            nullptr);
    }
    const int size = std::clamp(c.value<int>("/appearance/blur/size", 12), 1, 20);
    const int passes = std::clamp(c.value<int>("/appearance/blur/passes", 3), 1, 4);
    // Pixels fainter than this are left sharp: the clear space around a
    // popup's card or between bar capsules. Kept under the panels' own alpha.
    const float ignore = std::max(0.05f, std::min(t.barOpacity(), t.panelOpacity()) - 0.15f);

    if (!h.usingLua()) {
        // Legacy hyprland.conf: runtime keywords, gone after a config reload.
        // Rules can't be removed this way; turning blur off needs a reload.
        const auto key = std::format("{} {:.2f} {} {}", t.blur(), ignore, size, passes);
        if (key == m_applied) return;
        m_applied = key;
        if (t.blur())
            for (const char* l : kLayers) {
                h.request(std::format("keyword layerrule blur,material3ui-{}", l));
                h.request(std::format("keyword layerrule ignorealpha {:.2f},material3ui-{}", ignore, l));
            }
        h.request(std::format("keyword decoration:blur:size {}", size));
        h.request(std::format("keyword decoration:blur:passes {}", passes));
        return;
    }

    const std::string lua = std::format("-- Written by Material3UI (Settings → Appearance → Effects).\n"
                                        "-- Change these there; edits made here are overwritten.\n"
                                        "return {{\n\tblur = {},\n\tignore_alpha = {:.2f},\n\tsize = {},\n\tpasses = {},\n}}\n",
                                        t.blur() ? "true" : "false", ignore, size, passes);
    if (readAll(luaFile()) == lua) return; // unchanged: no reload
    std::error_code ec;
    fs::create_directories(fs::path(luaFile()).parent_path(), ec);
    std::ofstream(luaFile()) << lua;
    log::info("effects: blur {}, size {}, passes {}; reloading Hyprland", t.blur() ? "on" : "off", size, passes);
    h.request("reload");
}

} // namespace m3
