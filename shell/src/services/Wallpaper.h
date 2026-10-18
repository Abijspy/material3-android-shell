#pragma once

#include "core/Signal.h"

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
    void next(int step = 1);

    Signal<> changed;

private:
    Wallpaper();
    std::string m_last;
};

} // namespace m3
