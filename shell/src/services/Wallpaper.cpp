#include "services/Wallpaper.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
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
    return out;
}

void Wallpaper::set(const std::string& p) {
    if (p.empty()) return;
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
    if (accent.size() == 7 && accent[0] == '#') cmd.insert(cmd.end(), {"color", "hex", accent});
    else if (!path().empty()) cmd.insert(cmd.end(), {"image", path()});
    else return;
    cmd.insert(cmd.end(), {"--mode", dark ? "dark" : "light", "--type", scheme});
    // Matugen 4 asks which colour to use when an image offers several, and
    // fails without a terminal to ask on: take the most colourful one
    cmd.insert(cmd.end(), {"--prefer", "saturation"});
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

void Wallpaper::next(int step) {
    const auto all = list();
    if (all.empty()) return;
    auto it = std::ranges::find(all, path());
    const long i = it == all.end() ? 0 : (it - all.begin() + step + static_cast<long>(all.size())) % static_cast<long>(all.size());
    set(all[static_cast<size_t>(i)]);
}

} // namespace m3
