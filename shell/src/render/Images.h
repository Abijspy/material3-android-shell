#pragma once

#include <GLES2/gl2.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace m3 {

// Icons and pictures as GL textures: freedesktop icon-theme lookup, PNG and
// SVG files (SVG via librsvg, rasterised at the exact pixel size), and raw
// ARGB pixmaps from tray items. Textures are cached by source and size.
class Images {
public:
    struct Texture {
        GLuint id = 0;
        int width = 0, height = 0;
    };

    static Images& get();

    // Theme icon by name, or an absolute path; `extraDir` is searched first
    // (tray items may ship their own icons). Size in device pixels.
    const Texture* icon(const std::string& nameOrPath, int px, const std::string& extraDir = {});
    // ARGB32 big-endian pixmap (StatusNotifierItem IconPixmap)
    const Texture* pixmap(const std::string& key, int w, int h, const std::vector<uint8_t>& argb);

    std::string findIcon(const std::string& name, int px, const std::string& extraDir = {});

private:
    Images();
    Texture loadFile(const std::string& path, int px);
    void indexTheme(const std::string& theme);

    std::unordered_map<std::string, Texture> m_cache;
    std::unordered_map<std::string, std::string> m_lookups; // name|px → path ("" = missing)
    std::vector<std::string> m_searchDirs;                  // icon directories, in priority order
};

} // namespace m3
