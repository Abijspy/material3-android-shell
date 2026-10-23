#pragma once

#include "core/Signal.h"

#include <functional>
#include <string>
#include <vector>

namespace m3 {

// The wallpaper, stored where the QML shell keeps it
// (background.wallpaperPath in config.json), plus the folder to pick from.
class Wallpaper {
public:
    static Wallpaper& get();

    std::string path() const;     // desktop
    std::string lockPath() const; // lock screen (may be the same)
    std::string directory() const;
    std::vector<std::string> list() const; // images in the folder, sorted

    // Sets it and regenerates the Material palette with Matugen
    void set(const std::string& path);
    // Re-runs Matugen (after the scheme, mode or accent changes)
    void recolour();
    // The wallpaper's own colours, most dominant first (what Android offers
    // as "Wallpaper colours"); `done` runs once Matugen has looked
    void sourceColours(std::function<void(const std::vector<std::string>&)> done);
    // Matugen's templates (apps it themes), from ~/.config/matugen/config.toml
    std::vector<std::string> templates() const;
    // Switched off in Settings (Wallpaper & style → App theming): left out of
    // the config Matugen runs with, so those apps keep their own colours
    static bool templateOn(const std::string& name);
    static void setTemplateOn(const std::string& name, bool on);
    // The palette the shell itself draws with, and the wallpaper path it
    // records: never switched off
    static bool templateRequired(const std::string& name) { return name == "m3colors" || name == "wallpaper"; }
    void next(int step = 1);

    Signal<> changed;

private:
    // A copy of Matugen's config without the switched-off templates (empty:
    // none are off, so its own config runs)
    std::string filteredConfig() const;
    Wallpaper();
    std::string m_last;
};

} // namespace m3
