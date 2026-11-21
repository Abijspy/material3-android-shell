#include "services/Effects.h"

#include "core/Config.h"
#include "core/Paths.h"
#include "services/Niri.h"
#include "services/NiriConfig.h"
#include "services/Packages.h"

#include <algorithm>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace m3 {

Effects& Effects::get() {
    static Effects instance;
    return instance;
}

Effects::Effects() {
    apply();
    Config::get().changed.connectForever([this] { apply(); });
    // Bibata installed (from Settings → Accessibility): in use at once
    Packages::get().changed.connectForever([this] {
        if (!Packages::get().busy()) apply();
    });
}

bool Effects::cursorInstalled(const std::string& theme) {
    std::error_code ec;
    const std::string home = paths::home();
    for (const std::string& dir : {home + "/.local/share/icons/", home + "/.icons/", std::string("/usr/share/icons/")})
        if (fs::is_directory(dir + theme + "/cursors", ec) || fs::exists(dir + theme + "/manifest.hl", ec)) return true;
    return false;
}

std::string Effects::cursorTheme() {
    // Never chosen: Bibata. Chosen: that, or null for "System default"
    const auto j = Config::get().value<nlohmann::json>("/accessibility/cursorTheme", nlohmann::json::object());
    if (j.is_object()) return cursorInstalled(kDefaultCursor) ? kDefaultCursor : "";
    return j.is_string() ? j.get<std::string>() : "";
}

std::vector<std::string> Effects::blurredApps() {
    std::vector<std::string> out;
    const auto apps = Config::get().value<nlohmann::json>("/appearance/appBlur/apps", nlohmann::json::array());
    for (const auto& a : apps) {
        if (!a.is_object() || !a.contains("classes") || !a["classes"].is_array()) continue;
        std::vector<std::string> classes;
        for (const auto& c : a["classes"])
            if (c.is_string() && !c.get<std::string>().empty()) classes.push_back(c.get<std::string>());
        if (!classes.empty()) out.push_back(classRegex(classes));
    }
    return out;
}

double Effects::appOpacity() {
    return std::clamp(Config::get().value<double>("/appearance/appBlur/opacity", 0.92), 0.5, 1.0);
}

std::string Effects::classRegex(const std::vector<std::string>& classes) {
    std::string out = "^(";
    for (size_t i = 0; i < classes.size(); ++i) {
        if (i) out += '|';
        for (char ch : classes[i]) {
            if (std::string_view("\\^$.|?*+()[]{}").find(ch) != std::string_view::npos) out += '\\';
            out += ch;
        }
    }
    return out + ")$";
}

void Effects::apply() {
    // What Settings changes, as niri's own config (NiriConfig)
    if (Niri::get().available()) NiriConfig::get().apply();
}

} // namespace m3
