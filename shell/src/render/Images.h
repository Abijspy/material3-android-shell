#pragma once

#include "core/Signal.h"

#include <GLES2/gl2.h>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <map>
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
    // Index the icon themes again and forget icons that were missing, for
    // apps installed since (their icons arrive with them)
    void rescan();
    // A file changed (a new ~/.face): its pictures are decoded again
    void forget(const std::string& path);
    // A themed icon was rewritten (Settings' follows the wallpaper): drawn
    // and looked up afresh
    void reloadIcon(const std::string& name);
    // ARGB32 big-endian pixmap (StatusNotifierItem IconPixmap)
    const Texture* pixmap(const std::string& key, int w, int h, const std::vector<uint8_t>& argb);

    // Raw RGB/RGBA rows (notification image-data hints)
    const Texture* raw(const std::string& key, int w, int h, int rowstride, bool alpha, const std::vector<uint8_t>& data);

    // A picture file (JPEG, PNG, WebP… via gdk-pixbuf), decoded on a worker
    // thread at a size that covers (or fits) w×h device px. Returns null
    // until ready; `ready` runs on the event loop once it is.
    const Texture* picture(const std::string& path, int w, int h, bool cover, std::function<void()> ready = {});

    std::string findIcon(const std::string& name, int px, const std::string& extraDir = {});

    // A picture finished decoding: surfaces repaint to show it
    Signal<> ready;

private:
    Images();
    Texture loadFile(const std::string& path, int px);
    void indexTheme(const std::string& theme);

    std::unordered_map<std::string, Texture> m_cache;
    std::unordered_map<std::string, std::string> m_lookups; // name|px → path ("" = missing)
    std::vector<std::string> m_searchDirs;                  // icon directories, in priority order
    std::map<std::string, std::vector<std::function<void()>>> m_pending; // picture key → waiters
};

} // namespace m3
