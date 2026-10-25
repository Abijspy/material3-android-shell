#include "services/Wallpaper.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string stripUrl(std::string p) {
    if (p.starts_with("file://")) p = p.substr(7);
    if (p.starts_with("~/")) p = paths::home() + p.substr(1);
    return p;
}

bool isImage(const fs::path& p) {
    auto ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".bmp" || ext == ".gif";
}

} // namespace

Wallpaper& Wallpaper::get() {
    static Wallpaper instance;
    return instance;
}

Wallpaper::Wallpaper() {
    m_last = path();
    Config::get().changed.connectForever([this] {
        if (path() != m_last) {
            m_last = path();
            changed.emit();
        }
    });
}

std::string Wallpaper::path() const {
    auto p = stripUrl(Config::get().value<std::string>("/background/wallpaperPath", ""));
    // v3 (Quickshell) kept it under appearance.background
    if (p.empty()) p = stripUrl(Config::get().value<std::string>("/appearance/background/wallpaperPath", ""));
    // A fresh install starts on the shipped wallpaper
    return p.empty() ? paths::dataFile("wallpapers/default_wallpaper.png") : p;
}

std::string Wallpaper::lockPath() const {
    // The greeter shows the wallpaper synced for it
    if (const char* forced = std::getenv("M3_WALLPAPER"); forced && *forced) return forced;
    const auto& c = Config::get();
    if (c.value<bool>("/lock/useSeparateWallpaper", false)) {
        const auto p = stripUrl(c.value<std::string>("/lock/wallpaperPath", ""));
        if (!p.empty()) return p;
    }
    return path();
}

std::string Wallpaper::directory() const {
    return stripUrl(Config::get().value<std::string>("/background/autoCycleDirectory", paths::home() + "/Pictures/Wallpapers"));
}

std::vector<std::string> Wallpaper::list() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(directory(), ec))
        if (e.is_regular_file(ec) && isImage(e.path())) out.push_back(e.path());
    std::ranges::sort(out);
    // Then the ones the shell ships, so there is always something to pick
    std::vector<std::string> shipped;
    if (const auto dir = paths::dataFile("wallpapers"); !dir.empty())
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && isImage(e.path()) && std::ranges::find(out, e.path().string()) == out.end())
                shipped.push_back(e.path());
    std::ranges::sort(shipped);
    out.insert(out.end(), shipped.begin(), shipped.end());
    return out;
}

void Wallpaper::set(const std::string& p) {
    if (p.empty()) return;
    // A wallpaper colour belonged to the old picture: back to the new one's own
    if (Config::get().value<std::string>("/appearance/accent", "").starts_with("wallpaper:")) Config::get().set("/appearance/accent", "");
    Config::get().set("/background/wallpaperPath", "file://" + p);
    m_last = p;
    changed.emit();
    recolour();
}

void Wallpaper::recolour() {
    // New palette for the whole desktop, written where Theme watches
    if (!Config::get().value<bool>("/background/matugen", true)) return;
    const bool dark = Config::get().value<bool>("/background/darkmode", true);
    const auto scheme = Config::get().value<std::string>("/background/matugenScheme", "scheme-content");
    // A chosen accent colour seeds the palette instead of the wallpaper
    const auto accent = Config::get().value<std::string>("/appearance/accent", "");
    std::vector<std::string> cmd = {"matugen"};
    // "wallpaper:N": the wallpaper's Nth colour (Android's wallpaper colours)
    const bool fromWallpaper = accent.starts_with("wallpaper:");
    if (accent.size() == 7 && accent[0] == '#') cmd.insert(cmd.end(), {"color", "hex", accent});
    else if (!path().empty()) cmd.insert(cmd.end(), {"image", path()});
    else return;
    cmd.insert(cmd.end(), {"--mode", dark ? "dark" : "light", "--type", scheme});
    // Templates switched off: Matugen runs a copy of its config without them
    if (const auto filtered = filteredConfig(); !filtered.empty()) cmd.insert(cmd.end(), {"--config", filtered});
    // Matugen 4 asks which colour to use when an image offers several, and
    // fails without a terminal to ask on: the chosen one, or (picked for
    // you, as Android does) its first, the best scored
    if (cmd[1] == "image")
        cmd.insert(cmd.end(), {"--source-color-index", fromWallpaper ? std::to_string(std::clamp(std::atoi(accent.c_str() + 10), 0, 3)) : "0"});
    run(cmd, [](int code, const std::string&) {
        if (code != 0) {
            log::warn("matugen failed ({}); the palette is unchanged", code);
            return;
        }
        // The login screen follows: copy the new wallpaper and palette to it
        if (Config::get().value<bool>("/lock/syncGreeter", true) && access("/var/lib/material3ui-greeter", W_OK) == 0)
            run({"material3ui-greeter", "sync", "--quiet"}, nullptr);
    });
}

namespace {

std::string matugenConfig() {
    return paths::home() + "/.config/matugen/config.toml";
}

std::vector<std::string> disabledTemplates() {
    std::vector<std::string> out;
    const auto j = Config::get().value<nlohmann::json>("/theming/disabled", nlohmann::json::array());
    if (j.is_array())
        for (const auto& n : j)
            if (n.is_string() && !Wallpaper::templateRequired(n.get<std::string>())) out.push_back(n.get<std::string>());
    return out;
}

// [templates.NAME] → NAME, else empty
std::string templateHeader(const std::string& line) {
    auto l = line;
    l.erase(0, l.find_first_not_of(" \t"));
    if (!l.starts_with("[templates.")) return {};
    const auto end = l.find(']');
    return end == std::string::npos ? std::string() : l.substr(11, end - 11);
}

} // namespace

std::vector<std::string> Wallpaper::templates() const {
    std::vector<std::string> out;
    std::ifstream in(matugenConfig());
    for (std::string line; std::getline(in, line);)
        if (auto name = templateHeader(line); !name.empty() && std::ranges::find(out, name) == out.end()) out.push_back(name);
    return out;
}

bool Wallpaper::templateOn(const std::string& name) {
    const auto off = disabledTemplates();
    return std::ranges::find(off, name) == off.end();
}

void Wallpaper::setTemplateOn(const std::string& name, bool on) {
    if (templateRequired(name)) return;
    auto off = disabledTemplates();
    std::erase(off, name);
    if (!on) off.push_back(name);
    Config::get().set("/theming/disabled", off);
    // Back on: themed with the colours there are now
    if (on) get().recolour();
}

std::string Wallpaper::filteredConfig() const {
    const auto off = disabledTemplates();
    if (off.empty()) return {};
    std::ifstream in(matugenConfig());
    if (!in) return {};
    // Every line but the switched-off templates' sections (paths there are
    // all ~/…, so the copy works from anywhere)
    std::string out, line;
    bool skipping = false;
    while (std::getline(in, line)) {
        auto l = line;
        l.erase(0, l.find_first_not_of(" \t"));
        if (l.starts_with("[")) skipping = std::ranges::find(off, templateHeader(line)) != off.end();
        if (!skipping) out += line + "\n";
    }
    const std::string path = paths::stateDir() + "/matugen.toml";
    std::error_code ec;
    fs::create_directories(paths::stateDir(), ec);
    std::ofstream(path) << out;
    return path;
}

void Wallpaper::sourceColours(std::function<void(const std::vector<std::string>&)> done) {
    static std::string cachedFor;
    static std::vector<std::string> cached;
    const std::string image = path();
    if (image == cachedFor && !cached.empty()) return done(cached);
    run({"matugen", "image", image, "--show-source-colors", "--dry-run"}, [image, done](int, const std::string& out) {
        std::vector<std::string> colours;
        std::stringstream ss(out);
        for (std::string l; std::getline(ss, l);) {
            const auto at = l.find('#');
            if (at != std::string::npos && l.size() >= at + 7) colours.push_back(l.substr(at, 7));
            if (colours.size() == 4) break;
        }
        cachedFor = image;
        cached = colours;
        done(colours);
    });
}

void Wallpaper::swatch(const std::string& seed, const std::string& scheme, std::function<void(const std::vector<Color>&)> done) {
    static std::map<std::string, std::vector<Color>> known;
    static std::map<std::string, std::vector<std::function<void(const std::vector<Color>&)>>> waiting;
    const std::string key = seed + " " + scheme;
    if (const auto it = known.find(key); it != known.end()) return done(it->second);
    auto& queue = waiting[key];
    queue.push_back(std::move(done));
    if (queue.size() > 1) return; // already asked
    // One answer holds both modes' roles
    run({"matugen", "color", "hex", seed, "--dry-run", "--json", "hex", "--type", scheme}, [key](int, const std::string& out) {
        std::vector<Color> colours;
        try {
            // Its roles, not its palettes (those Matugen gives for the seed
            // whatever the scheme). As ThemePicker draws them: the primary at
            // tone 90 (primary_fixed), the tertiary at 85 (between tertiary,
            // 80, and its fixed, 90) and the primary at about 50
            const auto roles = nlohmann::json::parse(out).at("colors");
            const auto role = [&](const char* name, const char* mode) {
                return Color::fromHex(roles.at(name).at(mode).at("color").get<std::string>()).value_or(Color{});
            };
            colours = {role("primary_fixed", "dark"), Color::mix(role("tertiary", "dark"), role("tertiary_fixed", "dark"), 0.5f),
                       Color::mix(role("primary", "light"), role("primary", "dark"), 0.25f)};
        } catch (const std::exception&) {
            colours.clear();
        }
        known[key] = colours;
        auto callbacks = std::move(waiting[key]);
        waiting.erase(key);
        for (auto& cb : callbacks) cb(colours);
    });
}

void Wallpaper::next(int step) {
    const auto all = list();
    if (all.empty()) return;
    auto it = std::ranges::find(all, path());
    const long i = it == all.end() ? 0 : (it - all.begin() + step + static_cast<long>(all.size())) % static_cast<long>(all.size());
    set(all[static_cast<size_t>(i)]);
}

} // namespace m3
