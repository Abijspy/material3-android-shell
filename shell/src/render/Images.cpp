#include "render/Images.h"

#include "core/EventLoop.h"
#include "core/Paths.h"

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <librsvg/rsvg.h>
#include <thread>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

GLuint upload(const unsigned char* bgra, int w, int h) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bgra);
    return id;
}

std::string iconThemeName() {
    // GTK's setting is what most Wayland users configure
    std::ifstream in(paths::home() + "/.config/gtk-3.0/settings.ini");
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("gtk-icon-theme-name")) {
            auto v = line.substr(line.find('=') + 1);
            v.erase(0, v.find_first_not_of(" \t\""));
            v.erase(v.find_last_not_of(" \t\"") + 1);
            return v;
        }
    }
    return "hicolor";
}

std::vector<std::string> iconBaseDirs() {
    std::vector<std::string> dirs = {paths::home() + "/.local/share/icons", paths::home() + "/.icons"};
    const char* xdg = std::getenv("XDG_DATA_DIRS");
    std::stringstream ss(xdg && *xdg ? xdg : "/usr/local/share:/usr/share");
    std::string d;
    while (std::getline(ss, d, ':'))
        if (!d.empty()) dirs.push_back(d + "/icons");
    return dirs;
}

// Directory size from its name ("48x48", "scalable", "symbolic" …)
int dirSize(const std::string& dir) {
    if (dir.find("scalable") != std::string::npos) return 100000;
    static const std::regex size(R"(/(\d+)x\d+(@\d+)?(/|$))");
    std::smatch m;
    return std::regex_search(dir, m, size) ? std::stoi(m[1]) : 0;
}

} // namespace

Images& Images::get() {
    static Images instance;
    return instance;
}

Images::Images() {
    for (const auto& theme : {iconThemeName(), std::string("Adwaita"), std::string("breeze"), std::string("hicolor")})
        indexTheme(theme);
    m_searchDirs.push_back("/usr/share/pixmaps");
}

void Images::indexTheme(const std::string& theme) {
    for (const auto& base : iconBaseDirs()) {
        const fs::path root = fs::path(base) / theme;
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        std::vector<std::string> dirs;
        for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) dirs.push_back(it->path());
        }
        // Larger and scalable directories first: they downscale cleanly
        std::ranges::sort(dirs, [](const auto& a, const auto& b) { return dirSize(a) > dirSize(b); });
        for (auto& d : dirs)
            if (std::ranges::find(m_searchDirs, d) == m_searchDirs.end()) m_searchDirs.push_back(std::move(d));
    }
}

std::string Images::findIcon(const std::string& name, int px, const std::string& extraDir) {
    if (name.empty()) return {};
    if (name.front() == '/') return fs::exists(name) ? name : "";
    const std::string key = name + '|' + extraDir;
    if (auto it = m_lookups.find(key); it != m_lookups.end()) return it->second;

    std::string found;
    auto tryDir = [&](const std::string& dir) {
        for (const char* ext : {".svg", ".png"}) {
            const auto p = dir + "/" + name + ext;
            std::error_code ec;
            if (fs::exists(p, ec)) {
                found = p;
                return true;
            }
        }
        return false;
    };
    if (!extraDir.empty()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(extraDir, ec); found.empty() && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_directory(ec)) tryDir(it->path());
        }
        if (found.empty()) tryDir(extraDir);
    }
    for (const auto& d : m_searchDirs) {
        if (!found.empty()) break;
        tryDir(d);
    }
    (void)px;
    return m_lookups[key] = found;
}

Images::Texture Images::loadFile(const std::string& path, int px) {
    cairo_surface_t* surface = nullptr;
    if (path.ends_with(".svg")) {
        GError* err = nullptr;
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), &err);
        if (!h) {
            if (err) g_error_free(err);
            return {};
        }
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        cairo_t* cr = cairo_create(surface);
        const RsvgRectangle viewport{0, 0, static_cast<double>(px), static_cast<double>(px)};
        rsvg_handle_render_document(h, cr, &viewport, nullptr);
        cairo_destroy(cr);
        g_object_unref(h);
    } else {
        cairo_surface_t* png = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(png) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(png);
            return {};
        }
        // Scale into a px square, keeping the aspect ratio
        const int w = cairo_image_surface_get_width(png), h = cairo_image_surface_get_height(png);
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
        cairo_t* cr = cairo_create(surface);
        const double s = std::min(px / static_cast<double>(w), px / static_cast<double>(h));
        cairo_translate(cr, (px - w * s) / 2, (px - h * s) / 2);
        cairo_scale(cr, s, s);
        cairo_set_source_surface(cr, png, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(png);
    }
    cairo_surface_flush(surface);
    Texture t{upload(cairo_image_surface_get_data(surface), px, px), px, px};
    cairo_surface_destroy(surface);
    return t;
}

const Images::Texture* Images::icon(const std::string& nameOrPath, int px, const std::string& extraDir) {
    const std::string key = "icon:" + nameOrPath + '|' + extraDir + '|' + std::to_string(px);
    if (auto it = m_cache.find(key); it != m_cache.end()) return it->second.id ? &it->second : nullptr;
    const auto path = findIcon(nameOrPath, px, extraDir);
    auto& t = m_cache[key];
    if (!path.empty()) t = loadFile(path, px);
    return t.id ? &t : nullptr;
}

const Images::Texture* Images::pixmap(const std::string& key, int w, int h, const std::vector<uint8_t>& argb) {
    if (w <= 0 || h <= 0 || argb.size() < static_cast<size_t>(w) * h * 4) return nullptr;
    auto& t = m_cache["pixmap:" + key];
    if (t.id) glDeleteTextures(1, &t.id);
    // Network-order ARGB → premultiplied BGRA, the layout the image shader expects
    std::vector<unsigned char> bgra(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < bgra.size(); i += 4) {
        const unsigned a = argb[i], r = argb[i + 1], g = argb[i + 2], b = argb[i + 3];
        bgra[i] = static_cast<unsigned char>(b * a / 255);
        bgra[i + 1] = static_cast<unsigned char>(g * a / 255);
        bgra[i + 2] = static_cast<unsigned char>(r * a / 255);
        bgra[i + 3] = static_cast<unsigned char>(a);
    }
    t = Texture{upload(bgra.data(), w, h), w, h};
    return &t;
}

} // namespace m3

namespace m3 {

namespace {

// RGB(A) rows → premultiplied BGRA, the image shader's layout
std::vector<unsigned char> toBgra(const uint8_t* src, int w, int h, int rowstride, bool alpha) {
    std::vector<unsigned char> out(static_cast<size_t>(w) * h * 4);
    const int bpp = alpha ? 4 : 3;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * rowstride;
        unsigned char* o = out.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = row + x * bpp;
            const unsigned a = alpha ? p[3] : 255;
            o[x * 4 + 0] = static_cast<unsigned char>(p[2] * a / 255);
            o[x * 4 + 1] = static_cast<unsigned char>(p[1] * a / 255);
            o[x * 4 + 2] = static_cast<unsigned char>(p[0] * a / 255);
            o[x * 4 + 3] = static_cast<unsigned char>(a);
        }
    }
    return out;
}

} // namespace

const Images::Texture* Images::raw(const std::string& key, int w, int h, int rowstride, bool alpha,
                                   const std::vector<uint8_t>& data) {
    if (w <= 0 || h <= 0 || data.size() < static_cast<size_t>(rowstride) * (h - 1) + static_cast<size_t>(w) * (alpha ? 4 : 3))
        return nullptr;
    auto& t = m_cache["raw:" + key];
    if (t.id) return &t;
    const auto bgra = toBgra(data.data(), w, h, rowstride, alpha);
    t = Texture{upload(bgra.data(), w, h), w, h};
    return &t;
}

const Images::Texture* Images::picture(const std::string& path, int w, int h, bool cover, std::function<void()> ready) {
    const std::string key = "pic:" + path + '|' + std::to_string(w) + 'x' + std::to_string(h) + (cover ? "c" : "f");
    if (auto it = m_cache.find(key); it != m_cache.end()) return it->second.id ? &it->second : nullptr;
    auto [pending, first] = m_pending.try_emplace(key);
    if (ready) pending->second.push_back(std::move(ready));
    if (!first) return nullptr; // already decoding
    {
        // Decode off the event loop: a 4K JPEG takes long enough to drop frames
        std::thread([this, key, path, w, h, cover] {
            int iw = 0, ih = 0;
            gdk_pixbuf_get_file_info(path.c_str(), &iw, &ih);
            GdkPixbuf* pb = nullptr;
            if (iw > 0 && ih > 0) {
                const double sx = static_cast<double>(w) / iw, sy = static_cast<double>(h) / ih;
                const double s = cover ? std::max(sx, sy) : std::min(sx, sy);
                // Never upscale while decoding; the GPU stretches if it must
                const double f = std::min(s, 1.0);
                const int tw = std::max(1, static_cast<int>(iw * f + 0.5));
                const int th = std::max(1, static_cast<int>(ih * f + 0.5));
                pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), tw, th, FALSE, nullptr);
                if (pb) {
                    // Honour camera orientation
                    GdkPixbuf* rotated = gdk_pixbuf_apply_embedded_orientation(pb);
                    g_object_unref(pb);
                    pb = rotated;
                }
            }
            std::vector<unsigned char> bgra;
            int pw = 0, ph = 0;
            if (!pb && path.ends_with(".png")) {
                // gdk-pixbuf's glycin loaders need a sandbox that some setups
                // (containers) can't provide; Cairo reads PNG directly
                cairo_surface_t* png = cairo_image_surface_create_from_png(path.c_str());
                if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
                    const int iw2 = cairo_image_surface_get_width(png), ih2 = cairo_image_surface_get_height(png);
                    const double sx = static_cast<double>(w) / iw2, sy = static_cast<double>(h) / ih2;
                    const double f = std::min(cover ? std::max(sx, sy) : std::min(sx, sy), 1.0);
                    pw = std::max(1, static_cast<int>(iw2 * f + 0.5));
                    ph = std::max(1, static_cast<int>(ih2 * f + 0.5));
                    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
                    cairo_t* cr = cairo_create(out);
                    cairo_scale(cr, static_cast<double>(pw) / iw2, static_cast<double>(ph) / ih2);
                    cairo_set_source_surface(cr, png, 0, 0);
                    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
                    cairo_paint(cr);
                    cairo_destroy(cr);
                    cairo_surface_flush(out);
                    // Already premultiplied BGRA: copy row by row (stride may pad)
                    const int stride = cairo_image_surface_get_stride(out);
                    const unsigned char* data = cairo_image_surface_get_data(out);
                    bgra.resize(static_cast<size_t>(pw) * ph * 4);
                    for (int y = 0; y < ph; ++y)
                        std::copy_n(data + static_cast<size_t>(y) * stride, pw * 4, bgra.data() + static_cast<size_t>(y) * pw * 4);
                    cairo_surface_destroy(out);
                }
                cairo_surface_destroy(png);
            }
            if (pb) {
                pw = gdk_pixbuf_get_width(pb);
                ph = gdk_pixbuf_get_height(pb);
                bgra = toBgra(gdk_pixbuf_get_pixels(pb), pw, ph, gdk_pixbuf_get_rowstride(pb), gdk_pixbuf_get_has_alpha(pb));
                g_object_unref(pb);
            }
            EventLoop::get().post([this, key, bgra = std::move(bgra), pw, ph] {
                auto& t = m_cache[key];
                if (!bgra.empty()) t = Texture{upload(bgra.data(), pw, ph), pw, ph};
                auto waiters = std::move(m_pending[key]);
                m_pending.erase(key);
                for (auto& fn : waiters)
                    if (fn) fn();
                this->ready.emit();
            });
        }).detach();
    }
    return nullptr;
}

} // namespace m3
