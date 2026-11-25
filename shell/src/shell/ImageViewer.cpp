// The image viewer: one picture at a time in an app window, fitted to it,
// with the others in its folder a key or a click away. The wheel, + and -
// zoom (Ctrl+0 back to fitting, 1 to the picture's own pixels, a double
// click between the two); dragging moves a zoomed picture. Its bar edits it
// (the shell's image editor), makes it the wallpaper, shows it in Files or
// moves it to the trash. Favourites (F) and albums keep pictures where they
// are; a slideshow (F5) shows a folder's, an album's or the favourites,
// fullscreen. Files opens pictures here; it's also the `material3ui-viewer`
// app for picture files.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/FileOps.h"
#include "services/Wallpaper.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <GLES2/gl2.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <random>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

using Clock = std::chrono::steady_clock;

// Favourites and albums: lists of pictures where they are (nothing is
// copied or moved), in $XDG_DATA_HOME/material3ui/photos/library.json
struct Library {
    std::vector<std::string> favourites;
    std::vector<std::pair<std::string, std::vector<std::string>>> albums;

    static std::string path() {
        const char* x = std::getenv("XDG_DATA_HOME");
        return (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/material3ui/photos/library.json";
    }
    void load() {
        favourites.clear();
        albums.clear();
        std::ifstream in(path());
        if (!in) return;
        try {
            const auto j = nlohmann::json::parse(in);
            for (const auto& f : j.value("favourites", nlohmann::json::array())) favourites.push_back(f.get<std::string>());
            for (const auto& a : j.value("albums", nlohmann::json::array())) {
                std::vector<std::string> pics;
                for (const auto& p : a.value("pictures", nlohmann::json::array())) pics.push_back(p.get<std::string>());
                albums.emplace_back(a.value("name", "Album"), std::move(pics));
            }
        } catch (...) {
        }
    }
    void save() const {
        nlohmann::json j;
        j["favourites"] = favourites;
        auto arr = nlohmann::json::array();
        for (const auto& [name, pics] : albums) arr.push_back({{"name", name}, {"pictures", pics}});
        j["albums"] = arr;
        std::error_code ec;
        fs::create_directories(fs::path(path()).parent_path(), ec);
        paths::writeFile(path() + ".tmp", j.dump(1));
        fs::rename(path() + ".tmp", path(), ec);
    }
    bool favourite(const std::string& p) const { return std::ranges::find(favourites, p) != favourites.end(); }
    void toggleFavourite(const std::string& p) {
        if (favourite(p)) std::erase(favourites, p);
        else favourites.insert(favourites.begin(), p);
        save();
    }
    std::vector<std::string>* album(const std::string& name) {
        for (auto& [n, pics] : albums)
            if (n == name) return &pics;
        return nullptr;
    }
    void addTo(const std::string& name, const std::string& p) {
        if (!album(name)) albums.emplace_back(name, std::vector<std::string>{});
        auto* a = album(name);
        if (std::ranges::find(*a, p) == a->end()) a->push_back(p);
        save();
    }
    void removeFrom(const std::string& name, const std::string& p) {
        if (auto* a = album(name)) std::erase(*a, p), save();
    }
    void removeAlbum(const std::string& name) {
        std::erase_if(albums, [&](const auto& a) { return a.first == name; });
        save();
    }
    // A collection's pictures still there: "fav" or "album:<name>"
    std::vector<std::string> pictures(const std::string& collection) {
        std::vector<std::string> src;
        if (collection == "fav") src = favourites;
        else if (collection.starts_with("album:"))
            if (auto* a = album(collection.substr(6))) src = *a;
        std::error_code ec;
        std::erase_if(src, [&](const std::string& p) { return !fs::exists(p, ec); });
        return src;
    }
};

Library& library() {
    static Library lib = [] {
        Library l;
        l.load();
        return l;
    }();
    return lib;
}

std::string canonical(const std::string& p) {
    std::error_code ec;
    const auto c = fs::weakly_canonical(p, ec);
    return ec ? p : c.string();
}


// The picture, fitted or zoomed, and moved about when it's bigger than the
// room. Any format: what gdk-pixbuf reads (PNG, JPEG, WebP, AVIF, HEIC, JXL,
// TIFF, EXR, QOI…) as it is, the rest (PSD, XCF, camera RAW, JPEG 2000…)
// through ImageMagick (fileops::renderable). Animated GIF and WebP play,
// one frame at a time in a texture of its own
class ZoomView : public Node {
public:
    std::function<void()> changed; // zoomed or moved: the bar's percentage
    std::function<bool()> tapped;  // a press, first: true takes it (a slideshow ends)

    ZoomView() {
        m_note = add<Label>("");
        m_note->setSize(15)->setColor("on_surface_variant", false)->setMaxWidth(420)->setMaxLines(3)->setCentered(true);
        m_note->setVisible(false);
    }
    ~ZoomView() override {
        stopAnimation();
        *m_alive = false;
        if (m_tex) glDeleteTextures(1, &m_tex);
    }

    void show(const std::string& path) {
        stopAnimation();
        m_source = path;
        m_zoom = 0;
        m_panX = m_panY = 0;
        bool failed = false;
        m_draw = fileops::renderable(path, [this, alive = m_alive, path] {
            if (*alive && m_source == path) show(path); // converted: shown now
        }, &failed);
        if (m_draw.empty()) {
            m_note->setText(failed ? "This picture can't be opened\n" + fs::path(path).filename().string() : "Opening…")->setVisible(true);
            m_w = m_h = 1;
            requestLayout();
            requestRedraw();
            if (changed) changed();
            return;
        }
        m_note->setVisible(false);
        int w = 0, h = 0;
        gdk_pixbuf_get_file_info(m_draw.c_str(), &w, &h);
        m_w = std::max(1, w);
        m_h = std::max(1, h);
        startAnimation();
        requestRedraw();
        if (changed) changed();
    }
    bool animated() const { return m_iter != nullptr; }
    int width() const { return m_w; }
    int height() const { return m_h; }
    // What a picture's pixel is on screen: fitted (never past its own size), or the zoom
    float scale() const {
        if (m_zoom > 0) return m_zoom;
        const auto& b = rect();
        if (b.w <= 0 || b.h <= 0) return 1;
        // Small pictures at their own size, unless Settings says to fill the window
        const float fit = std::min(b.w / static_cast<float>(m_w), b.h / static_cast<float>(m_h));
        return Config::get().value<bool>("/viewer/upscale", false) ? fit : std::min(fit, 1.0f);
    }
    bool fitted() const { return m_zoom <= 0; }
    void zoomTo(float z) {
        m_zoom = z <= 0 ? 0 : std::clamp(z, 0.05f, 16.0f);
        if (m_zoom <= 0) m_panX = m_panY = 0;
        clampPan();
        requestRedraw();
        if (changed) changed();
    }
    void zoomBy(float factor) { zoomTo(scale() * factor); }
    void toggle() { zoomTo(fitted() ? 1.0f : 0); }

    bool interactive() const override { return true; }
    bool fillsHeight() const override { return true; }
    bool onScroll(int d) override {
        zoomBy(d < 0 ? 1.15f : 1 / 1.15f);
        return true;
    }
    bool onScrollPixels(float dy, bool precise) override {
        if (!precise) return false;
        zoomBy(std::pow(1.0025f, -dy));
        return true;
    }
    void onPress(uint32_t button, float x, float y) override {
        if (tapped && tapped()) return;
        if (button != BTN_LEFT) return;
        const auto now = Clock::now();
        if (now - m_lastPress < std::chrono::milliseconds(400)) {
            m_lastPress = {};
            toggle();
            return;
        }
        m_lastPress = now;
        m_dragX = x, m_dragY = y;
    }
    void onDrag(float x, float y) override {
        m_panX += x - m_dragX;
        m_panY += y - m_dragY;
        m_dragX = x, m_dragY = y;
        clampPan();
        requestRedraw();
    }

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, std::max(200.0f, available.h)}; }
    void doArrange() override {
        clampPan();
        const auto& b = rect();
        const SizeF n = m_note->measure({b.w, b.h});
        m_note->arrange({b.x + (b.w - n.w) / 2, b.y + (b.h - n.h) / 2, n.w, n.h});
    }
    void doPaint(Renderer& r) override {
        if (m_draw.empty()) return;
        const auto& b = rect();
        const float s = scale();
        const float dw = static_cast<float>(m_w) * s, dh = static_cast<float>(m_h) * s;
        GLuint tex = 0;
        if (m_iter) {
            if (m_frameDirty) uploadFrame();
            tex = m_tex;
        } else {
            // Decoded once, at its own size (a huge photo capped): zooming
            // draws the same pixels bigger or smaller, no new decode each step
            const int want = std::min(4096, std::max(m_w, m_h));
            const auto* t = Images::get().picture(m_draw, want, want, false);
            if (t) tex = t->id;
        }
        if (!tex) return; // decoding: drawn when it's ready
        r.pushClip(b);
        r.image(tex, {b.x + (b.w - dw) / 2 + m_panX, b.y + (b.h - dh) / 2 + m_panY, dw, dh});
        r.popClip();
    }

private:
    // A picture smaller than the room stays centred; a bigger one can't be
    // pulled past its edges
    void clampPan() {
        const auto& b = rect();
        const float s = scale();
        const float mx = std::max(0.0f, (static_cast<float>(m_w) * s - b.w) / 2), my = std::max(0.0f, (static_cast<float>(m_h) * s - b.h) / 2);
        m_panX = std::clamp(m_panX, -mx, mx);
        m_panY = std::clamp(m_panY, -my, my);
    }

    // GIF and WebP may move: played frame by frame at their own pace.
    // gdk-pixbuf calls its animation API deprecated (2.44) but it's the one
    // it has, and it still decodes them
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    void startAnimation() {
        std::string ext = fs::path(m_draw).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".gif" && ext != ".webp") return;
        m_anim = gdk_pixbuf_animation_new_from_file(m_draw.c_str(), nullptr);
        if (!m_anim || gdk_pixbuf_animation_is_static_image(m_anim)) {
            if (m_anim) g_object_unref(m_anim);
            m_anim = nullptr;
            return;
        }
        m_w = gdk_pixbuf_animation_get_width(m_anim);
        m_h = gdk_pixbuf_animation_get_height(m_anim);
        m_iter = gdk_pixbuf_animation_get_iter(m_anim, nullptr);
        m_frameDirty = true;
        next();
    }
    void next() {
        const int delay = gdk_pixbuf_animation_iter_get_delay_time(m_iter);
        if (delay < 0) return; // the last frame stays
        m_timer = EventLoop::get().addTimer(std::max(20, delay), [this] {
            m_timer = 0;
            if (!m_iter) return;
            if (gdk_pixbuf_animation_iter_advance(m_iter, nullptr)) {
                m_frameDirty = true;
                requestRedraw();
            }
            next();
        });
    }
    void stopAnimation() {
        if (m_timer) EventLoop::get().cancel(m_timer);
        m_timer = 0;
        if (m_iter) g_object_unref(m_iter);
        if (m_anim) g_object_unref(m_anim);
        m_iter = nullptr;
        m_anim = nullptr;
    }
    // The frame now, as the image shader takes it: premultiplied BGRA
    void uploadFrame() {
        m_frameDirty = false;
        GdkPixbuf* pb = gdk_pixbuf_animation_iter_get_pixbuf(m_iter);
        if (!pb) return;
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), rs = gdk_pixbuf_get_rowstride(pb);
        const bool alpha = gdk_pixbuf_get_has_alpha(pb);
        const int bpp = gdk_pixbuf_get_n_channels(pb);
        const guchar* px = gdk_pixbuf_read_pixels(pb);
        m_frame.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const guchar* row = px + static_cast<size_t>(y) * rs;
            unsigned char* o = m_frame.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; ++x) {
                const guchar* p = row + x * bpp;
                const unsigned a = alpha ? p[3] : 255;
                o[x * 4 + 0] = static_cast<unsigned char>(p[2] * a / 255);
                o[x * 4 + 1] = static_cast<unsigned char>(p[1] * a / 255);
                o[x * 4 + 2] = static_cast<unsigned char>(p[0] * a / 255);
                o[x * 4 + 3] = static_cast<unsigned char>(a);
            }
        }
        if (!m_tex) {
            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_tex);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_frame.data());
    }

#pragma GCC diagnostic pop

    std::string m_source, m_draw; // the file, and what's drawn of it (a conversion, for some formats)
    Label* m_note = nullptr;      // "Opening…", "can't be opened"
    int m_w = 1, m_h = 1;
    float m_zoom = 0, m_panX = 0, m_panY = 0, m_dragX = 0, m_dragY = 0;
    Clock::time_point m_lastPress;
    GdkPixbufAnimation* m_anim = nullptr;
    GdkPixbufAnimationIter* m_iter = nullptr;
    GLuint m_tex = 0;
    bool m_frameDirty = false;
    std::vector<unsigned char> m_frame;
    EventLoop::TimerId m_timer = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

struct ViewerState {
    std::vector<std::string> pictures; // the folder's, in order
    size_t index = 0;
    ZoomView* view = nullptr;
    Label *name = nullptr, *count = nullptr, *zoom = nullptr, *info = nullptr;
    Flex* infoBar = nullptr;
    std::function<void()> refresh;
    // The gallery: what opens with no picture asked for (from the app list),
    // a folder's pictures as a grid, newest first
    Flex *gallery = nullptr, *grid = nullptr, *bar = nullptr, *stage = nullptr;
    Label* galleryTitle = nullptr;
    std::string galleryDir;
    std::function<void()> showGallery;
    EventLoop::TimerId thumbTimer = 0;
    // Favourites and albums: what the gallery shows when not a folder
    std::string collection; // "" (a folder), "fav", "album:<name>"
    std::string shownGallery; // the gallery last shown: a new one's pictures enter in turn
    std::vector<std::string> galleryPics; // the gallery's, in its order
    Flex *albumsRow = nullptr, *ask = nullptr;
    Icon* favIcon = nullptr;
    Button *prev = nullptr, *next = nullptr;
    Popup* popup = nullptr;
    Flex* card = nullptr;
    bool sliding = false, paused = false; // the slideshow
    EventLoop::TimerId slideTimer = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

// The pictures beside it, in name order (as Files shows them)
std::vector<std::string> siblings(const std::string& file) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(file).parent_path(), fs::directory_options::skip_permission_denied, ec)) {
        const std::string p = e.path().string();
        if (e.is_regular_file(ec) && fileops::isPicture(p) && !e.path().filename().string().starts_with(".")) out.push_back(p);
    }
    std::ranges::sort(out);
    if (std::ranges::find(out, file) == out.end()) out.push_back(file);
    return out;
}

void viewing(const std::shared_ptr<ViewerState>& st, bool on) {
    if (!st->gallery) return;
    st->gallery->setVisible(!on);
    st->bar->setVisible(on);
    st->stage->setVisible(on);
    st->infoBar->setVisible(on && Config::get().value<bool>("/viewer/info", false));
}

// `within`: the pictures to step through (an album's), else the folder's
void showAt(const std::shared_ptr<ViewerState>& st, std::string file, const std::vector<std::string>* within = nullptr) {
    std::error_code ec;
    if (fs::is_directory(file, ec)) {
        st->galleryDir = file;
        if (st->showGallery) st->showGallery();
        return;
    }
    if (!fs::exists(file, ec)) return;
    const bool fromGallery = st->gallery && st->gallery->visible();
    viewing(st, true);
    if (fromGallery) pageIn(st->stage, 0);
    file = fs::weakly_canonical(file, ec).string();
    if (within && std::ranges::find(*within, file) != within->end()) st->pictures = *within;
    else st->pictures = siblings(file);
    st->index = static_cast<size_t>(std::ranges::find(st->pictures, file) - st->pictures.begin());
    st->view->show(file);
    st->refresh();
}

void step(const std::shared_ptr<ViewerState>& st, int by) {
    if (st->pictures.size() < 2) return;
    const auto n = static_cast<long>(st->pictures.size());
    // At an end: round to the other, or stay (Settings)
    const long to = static_cast<long>(st->index) + by;
    if ((to < 0 || to >= n) && !Config::get().value<bool>("/viewer/loop", true)) return;
    st->index = static_cast<size_t>(((static_cast<long>(st->index) + by) % n + n) % n);
    st->view->show(st->pictures[st->index]);
    if (!st->sliding) pageIn(st->view, by > 0 ? 1 : -1);
    else pageIn(st->view, 0);
    st->refresh();
}

// Into the trash, and on to the next
void trashCurrent(const std::shared_ptr<ViewerState>& st, Popup& popup, bool asked = false);

// Turned a quarter, and saved so (ImageMagick when it's there, which keeps
// any format; else gdk-pixbuf for PNG and JPEG)
void rotateCurrent(const std::shared_ptr<ViewerState>& st, bool clockwise) {
    if (st->pictures.empty()) return;
    const std::string file = st->pictures[st->index];
    auto done = [st, file](bool ok, const std::string& why) {
        if (!*st->alive) return;
        if (!ok) return void(m3::run({"notify-send", "-a", "Photos", "-i", "image-x-generic", "Couldn't turn the picture", why}, nullptr));
        Images::get().forget(file);
        if (!st->pictures.empty() && st->pictures[st->index] == file) {
            st->view->show(file);
            st->refresh();
        }
    };
    if (haveCommand("magick")) {
        m3::run({"magick", "mogrify", "-rotate", clockwise ? "90" : "-90", file}, [done](int code, const std::string& out) { done(code == 0, out.substr(0, 200)); });
        return;
    }
    std::string ext = fs::path(file).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char* type = ext == ".png" ? "png" : ext == ".jpg" || ext == ".jpeg" ? "jpeg" : nullptr;
    if (!type) return done(false, "Turning this kind of picture needs ImageMagick: sudo pacman -S imagemagick");
    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(file.c_str(), &err);
    if (!pb) return done(false, err ? err->message : "It couldn't be read");
    GdkPixbuf* turned = gdk_pixbuf_rotate_simple(pb, clockwise ? GDK_PIXBUF_ROTATE_CLOCKWISE : GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
    g_object_unref(pb);
    const std::string tmp = file + ".turning";
    const bool ok = std::string(type) == "jpeg" ? gdk_pixbuf_save(turned, tmp.c_str(), type, &err, "quality", "95", nullptr)
                                               : gdk_pixbuf_save(turned, tmp.c_str(), type, &err, nullptr);
    g_object_unref(turned);
    std::error_code ec;
    if (ok) fs::rename(tmp, file, ec);
    else fs::remove(tmp, ec);
    done(ok && !ec, err ? err->message : ec.message());
    if (err) g_error_free(err);
}

// The picture itself onto the clipboard, to paste into a chat or a document
void copyCurrent(const std::shared_ptr<ViewerState>& st) {
    if (st->pictures.empty()) return;
    const std::string file = st->pictures[st->index];
    std::string ext = fs::path(file).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string mime = ext == ".png" ? "image/png" : ext == ".jpg" || ext == ".jpeg" ? "image/jpeg" : ext == ".webp" ? "image/webp" : ext == ".gif" ? "image/gif" : "";
    if (mime.empty()) {
        // Another format: as PNG
        m3::run({"sh", "-c", "magick \"$1\" png:- | wl-copy --type image/png", "sh", file}, nullptr);
        return;
    }
    m3::run({"sh", "-c", "wl-copy --type \"$2\" < \"$1\"", "sh", file, mime}, nullptr);
}

void trashCurrent(const std::shared_ptr<ViewerState>& st, Popup& popup, bool asked) {
    if (st->pictures.empty()) return;
    const std::string file = st->pictures[st->index];
    // Asked first, when Settings says so
    if (!asked && Config::get().value<bool>("/viewer/confirmTrash", false) && st->bar) {
        floatingMenu(st->bar, {MenuItem("delete", "Move " + fs::path(file).filename().string() + " to the trash", "", [st, &popup] { trashCurrent(st, popup, true); }, false, true)});
        return;
    }
    fileops::trash({file});
    st->pictures.erase(st->pictures.begin() + static_cast<long>(st->index));
    if (st->pictures.empty()) {
        (void)popup;
        if (st->showGallery) st->showGallery();
        return;
    }
    if (st->index >= st->pictures.size()) st->index = 0;
    st->view->show(st->pictures[st->index]);
    st->refresh();
}

class ViewerKeeper : public Node {
public:
    explicit ViewerKeeper(std::shared_ptr<ViewerState> st) : m_st(std::move(st)) { floating = true; }
    // The window's nodes go in order: the picture's view may be gone
    // already, so only the state is touched here
    ~ViewerKeeper() override {
        *m_st->alive = false;
        m_st->refresh = nullptr;
        m_st->showGallery = nullptr;
        if (m_st->thumbTimer) EventLoop::get().cancel(m_st->thumbTimer);
        if (m_st->slideTimer) EventLoop::get().cancel(m_st->slideTimer);
        m_st->popup = nullptr;
        m_st->view = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<ViewerState> m_st;
};

} // namespace

std::function<void(const std::string&)> viewerGo;

void buildGallery(const std::shared_ptr<ViewerState>& st);
void buildAlbumsRow(std::shared_ptr<ViewerState> st);
void pictureMenu(const std::shared_ptr<ViewerState>& st, Node* from, const std::string& path);

// The folders a gallery offers: the usual places pictures gather
std::vector<std::pair<std::string, std::string>> galleryPlaces() {
    const std::string home = paths::home();
    auto xdg = [&](const char* var, const std::string& fallback) {
        const char* v = std::getenv(var);
        return v && *v ? std::string(v) : home + "/" + fallback;
    };
    const std::string pics = xdg("XDG_PICTURES_DIR", "Pictures");
    std::vector<std::pair<std::string, std::string>> all = {
        {"Pictures", pics}, {"Screenshots", pics + "/Screenshots"}, {"Wallpapers", pics + "/Wallpapers"},
        {"Camera", pics + "/Camera"}, {"Downloads", xdg("XDG_DOWNLOAD_DIR", "Downloads")}, {"Desktop", xdg("XDG_DESKTOP_DIR", "Desktop")}};
    std::vector<std::pair<std::string, std::string>> out;
    std::error_code ec;
    for (auto& p : all)
        if (fs::is_directory(p.second, ec)) out.push_back(p);
    return out;
}

// A gallery picture, with a heart in its corner when it's a favourite
class Thumb : public Button {
public:
    Icon* heart = nullptr;

protected:
    void doArrange() override {
        Button::doArrange();
        if (!heart) return;
        const auto& b = rect();
        const SizeF m = heart->measure({b.w, b.h});
        heart->arrange({b.x + b.w - m.w - 8, b.y + 8, m.w, m.h});
    }
};

void buildGallery(const std::shared_ptr<ViewerState>& st) {
    auto* g = st->grid;
    g->clearChildren();
    std::error_code ec;
    if (st->galleryDir.empty()) {
        const auto places = galleryPlaces();
        st->galleryDir = places.empty() ? paths::home() : places.front().second;
    }
    struct Pic {
        std::string path;
        std::time_t when;
    };
    std::vector<Pic> pics;
    if (!st->collection.empty()) {
        // Favourites (newest first) or an album (in the order they were added)
        st->galleryTitle->setText(st->collection == "fav" ? "Favourites" : st->collection.substr(6));
        for (const auto& p : library().pictures(st->collection)) pics.push_back({p, 0});
    } else {
        st->galleryTitle->setText(fileops::displayPath(st->galleryDir));
        for (const auto& e : fileops::list(st->galleryDir, false))
            if (!e.dir && fileops::isPicture(e.path)) pics.push_back({canonical(e.path), e.modified});
    }
    if (!st->collection.empty()) {
    } else if (Config::get().value<std::string>("/viewer/sort", "date") == "name")
        std::ranges::sort(pics, [](const Pic& a, const Pic& b) { return strverscmp(fs::path(a.path).filename().c_str(), fs::path(b.path).filename().c_str()) < 0; });
    else
        std::ranges::sort(pics, std::greater{}, &Pic::when);
    if (pics.size() > 600) pics.resize(600);
    st->galleryPics.clear();
    for (const auto& pic : pics) st->galleryPics.push_back(pic.path);
    auto* flow = g->add<Flow>();
    flow->spacing = 8;
    for (const auto& pic : pics) {
        auto* b = flow->add<Thumb>();
        b->setPadding(Insets(0))->setRadius(Radii(14));
        b->hoverLift = true;
        b->tooltip = fs::path(pic.path).filename().string();
        std::string shown = pic.path.ends_with(".svg") ? pic.path : "";
        if (shown.empty()) {
            auto alive = st->alive;
            shown = fileops::renderable(pic.path, [st, alive] {
                if (!*alive) return;
                EventLoop::get().debounce(st->thumbTimer, 400, [st, alive] {
                    if (*alive && st->gallery->visible()) buildGallery(st);
                });
            });
        }
        constexpr float W = 172, H = 128;
        if (shown.empty()) {
            auto* box = b->add<Flex>(Flex::Direction::Row);
            box->fixedWidth = W, box->fixedHeight = H;
            box->setAlignItems(Align::Center)->setJustify(Align::Center)->setRadius(Radii(14))->setBackground("surface_container_high", false);
            box->add<Icon>("image", 32)->setColor("on_surface_variant", false);
        } else {
            auto* v = b->add<ImageView>(W, H, [shown](float scale, int& tw, int& th) -> GLuint {
                auto* t = Images::get().picture(shown, static_cast<int>(W * scale), static_cast<int>(H * scale), true);
                if (!t) return 0;
                tw = t->width, th = t->height;
                return t->id;
            });
            v->cover = true;
            v->radius = 14;
        }
        const std::string path = pic.path;
        if (library().favourite(path)) {
            // A heart in its corner
            auto* heart = b->add<Icon>("favorite", 18);
            heart->setFill(true)->setColor(ColorRef(Color{1, 1, 1, 0.95f}), false);
            heart->floating = true;
            b->heart = heart;
        }
        b->dragFiles = [path] { return std::vector<std::string>{path}; }; // to Files, another app, a note
        b->onClicked = [st, path, b](uint32_t button) {
            if (button == BTN_LEFT) showAt(st, path, st->collection.empty() ? nullptr : &st->galleryPics);
            if (button == BTN_RIGHT) pictureMenu(st, b, path);
        };
    }
    const std::string view = st->collection + "|" + st->galleryDir + "|" + Config::get().value<std::string>("/viewer/sort", "date");
    if (view != st->shownGallery) {
        st->shownGallery = view;
        stagger(flow);
    }
    if (pics.empty())
        caption(g, st->collection == "fav"             ? "No favourites yet: open a picture and press F, or right click one"
                   : !st->collection.empty()            ? "This album is empty: right click a picture, or use the album button over one"
                                                        : "No pictures in this folder");
}

// ── Favourites, albums and the slideshow ──

static ColorRef stageColor() {
    return Config::get().value<std::string>("/viewer/backdrop", "theme") == "black" ? ColorRef(Color{0, 0, 0, 1}) : ColorRef("surface_container_lowest", 0.6f);
}

void albumsChanged(std::shared_ptr<ViewerState> st) {
    if (st->gallery->visible()) buildGallery(st);
    if (st->refresh) st->refresh();
    buildAlbumsRow(st);
}

// A name for a new album, asked for over the window; the picture (if any) goes in it
void askAlbum(const std::shared_ptr<ViewerState>& st, const std::string& picture) {
    auto* a = st->ask;
    a->clearChildren();
    a->setVisible(true);
    a->add<Icon>("photo_album", 20);
    auto* f = a->add<TextField>();
    f->bare = true;
    f->grow = true;
    f->setPlaceholder(picture.empty() ? "The new album's name" : "The new album's name (this picture goes in it)");
    auto done = [st, f, picture] {
        std::string name = f->text();
        while (!name.empty() && name.back() == ' ') name.pop_back();
        st->ask->setVisible(false);
        if (name.empty()) return;
        auto& lib = library();
        if (!lib.album(name)) lib.albums.emplace_back(name, std::vector<std::string>{});
        if (!picture.empty()) lib.addTo(name, picture);
        else lib.save();
        if (picture.empty()) st->collection = "album:" + name; // a new, empty album: shown
        albumsChanged(st);
    };
    f->onSubmit = [done](const std::string&) { done(); };
    pillButton(a, "Create", true, done);
    iconButton(a, "close", [st] { st->ask->setVisible(false); }, 18);
    EventLoop::get().defer([st, f] {
        if (*st->alive) f->focus();
    });
}

std::vector<MenuItem> albumItems(const std::shared_ptr<ViewerState>& st, const std::string& path) {
    std::vector<MenuItem> items;
    auto& lib = library();
    bool first = true;
    for (const auto& [name, pics] : lib.albums) {
        const bool in = std::ranges::find(pics, path) != pics.end();
        items.emplace_back("photo_album", (in ? "In " : "Add to ") + name, "", [st, path, name, in] {
            if (in) library().removeFrom(name, path);
            else library().addTo(name, path);
            albumsChanged(st);
        }, in, false, true, first);
        first = false;
    }
    items.emplace_back("add", "New album…", "", [st, path] { askAlbum(st, path); }, false, false, true, first);
    return items;
}

void pictureMenu(const std::shared_ptr<ViewerState>& st, Node* from, const std::string& path) {
    auto& lib = library();
    std::vector<MenuItem> items;
    const bool fav = lib.favourite(path);
    items.emplace_back(fav ? "heart_minus" : "favorite", fav ? "Remove from favourites" : "Add to favourites", "F", [st, path] {
        library().toggleFavourite(path);
        albumsChanged(st);
    });
    for (auto& i : albumItems(st, path)) items.push_back(std::move(i));
    if (st->collection.starts_with("album:")) {
        const std::string name = st->collection.substr(6);
        items.emplace_back("remove", "Take out of " + name, "", [st, path, name] {
            library().removeFrom(name, path);
            albumsChanged(st);
        }, false, true, true, true);
    }
    items.emplace_back("folder_open", "Show in Files", "", [path] {
        if (open) open("files " + path);
    }, false, false, true, true);
    floatingMenu(from, items);
}

void buildAlbumsRow(std::shared_ptr<ViewerState> st) {
    auto* row = st->albumsRow;
    if (!row) return;
    row->clearChildren();
    auto chip = [&](const std::string& icon, const std::string& label, const std::string& collection) {
        auto* c = row->add<Button>(Flex::Direction::Row);
        const bool on = st->collection == collection && !collection.empty();
        pill(c->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_high"), false))->setPadding({10, 6, 12, 6})->setSpacing(6)->setAlignItems(Align::Center);
        c->add<Icon>(icon, 16)->setFill(on);
        c->add<Label>(label)->setSize(13)->setWeight(500);
        return c;
    };
    auto& lib = library();
    auto* fav = chip("favorite", std::format("Favourites  {}", lib.pictures("fav").size()), "fav");
    fav->onClicked = [st](uint32_t b) {
        if (b != BTN_LEFT) return;
        st->collection = "fav";
        buildGallery(st);
        buildAlbumsRow(st); // last: it rebuilds this chip (st is copied in)
    };
    for (const auto& [name, pics] : lib.albums) {
        auto* c = chip("photo_album", std::format("{}  {}", name, pics.size()), "album:" + name);
        c->onClicked = [st, name, c](uint32_t b) {
            if (b == BTN_LEFT) {
                st->collection = "album:" + name;
                buildGallery(st);
                buildAlbumsRow(st); // last: it rebuilds this chip
            } else if (b == BTN_RIGHT) {
                floatingMenu(c, {MenuItem("delete", "Delete the album", "Its pictures stay where they are", [st, name] {
                                     library().removeAlbum(name);
                                     if (st->collection == "album:" + name) st->collection.clear();
                                     albumsChanged(st);
                                 }, false, true)});
            }
        };
    }
    auto* add = chip("add", "New album", "");
    add->onClicked = [st](uint32_t b) {
        if (b == BTN_LEFT) askAlbum(st, "");
    };
}

void stopSlideshow(const std::shared_ptr<ViewerState>& st) {
    if (!st->sliding) return;
    st->sliding = false;
    if (st->slideTimer) EventLoop::get().cancel(st->slideTimer), st->slideTimer = 0;
    if (st->popup) st->popup->setFullscreen(false);
    st->bar->setVisible(true);
    st->prev->setVisible(true);
    st->next->setVisible(true);
    st->stage->setBackground(stageColor(), false);
    st->stage->setRadius(Radii(22));
    st->card->setPadding(Insets(10));
    st->infoBar->setVisible(Config::get().value<bool>("/viewer/info", false));
    if (st->refresh) st->refresh();
}

void slideNext(const std::shared_ptr<ViewerState>& st) {
    if (st->slideTimer) EventLoop::get().cancel(st->slideTimer), st->slideTimer = 0;
    if (!st->sliding || st->paused) return;
    const int secs = std::max(1, Config::get().value<int>("/viewer/slideSeconds", 5));
    st->slideTimer = EventLoop::get().addTimer(secs * 1000, [st] {
        st->slideTimer = 0;
        if (!*st->alive || !st->sliding) return;
        // The end, with wrapping off: the show's over
        if (st->index + 1 >= st->pictures.size() && !Config::get().value<bool>("/viewer/loop", true)) return stopSlideshow(st);
        step(st, 1);
        slideNext(st);
    });
}

// The pictures one after another, the whole screen, nothing else
void startSlideshow(const std::shared_ptr<ViewerState>& st, const std::vector<std::string>& from = {}) {
    if (!from.empty()) showAt(st, from.front(), &from);
    if (st->pictures.empty() || st->sliding) return;
    if (Config::get().value<bool>("/viewer/slideShuffle", false) && st->pictures.size() > 2) {
        const std::string now = st->pictures[st->index];
        static std::mt19937 rng{std::random_device{}()};
        std::ranges::shuffle(st->pictures, rng);
        std::iter_swap(st->pictures.begin(), std::ranges::find(st->pictures, now));
        st->index = 0;
    }
    st->sliding = true;
    st->paused = false;
    viewing(st, true);
    st->bar->setVisible(false);
    st->infoBar->setVisible(false);
    st->ask->setVisible(false);
    st->prev->setVisible(false);
    st->next->setVisible(false);
    st->stage->setBackground(ColorRef(Color{0, 0, 0, 1}), false);
    st->stage->setRadius(Radii(0));
    st->card->setPadding(Insets(0));
    if (st->popup) st->popup->setFullscreen(true);
    st->view->zoomTo(0);
    slideNext(st);
}

void viewer(Flex* card, Popup& popup, const std::string& file) {
    auto st = std::make_shared<ViewerState>();
    st->popup = &popup;
    st->card = card;
    card->setPadding(Insets(10))->setSpacing(8);
    // A new album's name, asked for over the gallery or the picture
    st->ask = card->add<Flex>(Flex::Direction::Row);
    pill(st->ask->setBackground("surface_container_high", false))->setPadding({14, 4, 6, 4})->setSpacing(8)->setAlignItems(Align::Center);
    st->ask->setVisible(false);

    // The bar: which picture, and what to do with it
    // The gallery (no picture yet, or the grid button)
    st->gallery = card->add<Flex>(Flex::Direction::Column);
    st->gallery->setSpacing(10)->setAlignItems(Align::Stretch);
    st->gallery->grow = true;
    {
        auto* head = st->gallery->add<Flex>(Flex::Direction::Row);
        head->setSpacing(8)->setAlignItems(Align::Center)->setPadding({8, 0});
        auto* titles = head->add<Flex>(Flex::Direction::Column);
        titles->setAlignItems(Align::Start)->setSpacing(1);
        titles->grow = true;
        titles->givesWay = true;
        titles->add<Label>("Photos")->setSize(22)->setWeight(500);
        st->galleryTitle = titles->add<Label>("");
        st->galleryTitle->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420);
        for (const auto& [name, dir] : galleryPlaces()) {
            auto* chip = head->add<Button>();
            pill(chip->setBackground("surface_container_high", false))->setPadding({12, 6});
            chip->add<Label>(name)->setSize(13)->setWeight(500);
            chip->onClicked = [st, d = dir](uint32_t b) {
                if (b != BTN_LEFT) return;
                st->galleryDir = d;
                st->collection.clear();
                buildAlbumsRow(st);
                buildGallery(st);
            };
        }
        iconButton(head, "slideshow", [st] { startSlideshow(st, st->galleryPics); }, 22)->tooltip = "Slideshow (F5)";
        iconButton(head, "folder_open", [st] {
            if (open) open("files " + st->galleryDir);
        }, 22)->tooltip = "Open this folder in Files";
        st->albumsRow = st->gallery->add<Flex>(Flex::Direction::Row);
        st->albumsRow->setSpacing(6)->setAlignItems(Align::Center)->setPadding({8, 0});
        buildAlbumsRow(st);
        auto* scroll = st->gallery->add<ScrollView>();
        scroll->maxHeight = 1e6f;
        scroll->grow = true;
        scroll->blurEdges = true;
        st->grid = scroll->add<Flex>(Flex::Direction::Column);
        st->grid->setAlignItems(Align::Stretch)->setPadding({8, 4, 8, 12});
    }
    st->showGallery = [st] {
        viewing(st, false);
        buildGallery(st);
    };

    auto* bar = card->add<Flex>(Flex::Direction::Row);
    st->bar = bar;
    bar->setSpacing(4)->setAlignItems(Align::Center)->setPadding({8, 0});
    iconButton(bar, "grid_view", [st] {
        if (!st->pictures.empty()) st->galleryDir = fs::path(st->pictures[st->index]).parent_path().string();
        st->showGallery();
    }, 22)->tooltip = "All the folder's pictures (Escape)";
    auto* names = bar->add<Flex>(Flex::Direction::Column);
    names->setAlignItems(Align::Start)->setSpacing(1);
    names->grow = true;
    names->givesWay = true;
    st->name = names->add<Label>("");
    st->name->setSize(15)->setWeight(600)->setMaxWidth(520);
    st->count = names->add<Label>("");
    st->count->setSize(12)->setColor("on_surface_variant", false);
    auto& p = popup;
    iconButton(bar, "zoom_out", [st] { st->view->zoomBy(1 / 1.25f); }, 22)->tooltip = "Zoom out (−)";
    auto* zoomPill = bar->add<Button>();
    pill(zoomPill->setBackground("surface_container_high", false))->setPadding({12, 6});
    st->zoom = zoomPill->add<Label>("Fit");
    st->zoom->setSize(13)->setWeight(500);
    st->zoom->fixedWidth = 48;
    zoomPill->tooltip = "Fit, or the picture's own size (double click the picture)";
    zoomPill->onClicked = [st](uint32_t b) {
        if (b == BTN_LEFT) st->view->toggle();
    };
    iconButton(bar, "zoom_in", [st] { st->view->zoomBy(1.25f); }, 22)->tooltip = "Zoom in (+)";
    bar->add<Node>()->fixedWidth = 8;
    auto* fav = iconButton(bar, "favorite", [st] {
        if (st->pictures.empty()) return;
        library().toggleFavourite(st->pictures[st->index]);
        albumsChanged(st);
    }, 22);
    fav->tooltip = "Favourite (F)";
    for (auto& ch : fav->children())
        if (auto* i = dynamic_cast<Icon*>(ch.get())) st->favIcon = i;
    Button* alb = iconButton(bar, "photo_album", [] {}, 22);
    alb->onClicked = [st, alb](uint32_t b) {
        if (b == BTN_LEFT && !st->pictures.empty()) floatingMenu(alb, albumItems(st, st->pictures[st->index]));
    };
    alb->tooltip = "Add to an album";
    iconButton(bar, "slideshow", [st] { startSlideshow(st); }, 22)->tooltip = "Slideshow (F5)";
    iconButton(bar, "brush", [st] {
        if (!st->pictures.empty() && open) open("imageeditor " + st->pictures[st->index]);
    }, 22)->tooltip = "Edit";
    iconButton(bar, "rotate_left", [st] { rotateCurrent(st, false); }, 22)->tooltip = "Turn left (Shift+R)";
    iconButton(bar, "rotate_right", [st] { rotateCurrent(st, true); }, 22)->tooltip = "Turn right (R)";
    iconButton(bar, "content_copy", [st] { copyCurrent(st); }, 22)->tooltip = "Copy the picture (Ctrl+C)";
    iconButton(bar, "wallpaper", [st] {
        if (!st->pictures.empty() && open) open("wallpaper " + st->pictures[st->index]);
    }, 22)->tooltip = "Set as wallpaper";
    iconButton(bar, "folder_open", [st] {
        if (!st->pictures.empty() && open) open("files " + st->pictures[st->index]);
    }, 22)->tooltip = "Show in Files";
    iconButton(bar, "info", [st] { st->infoBar->setVisible(!st->infoBar->visible()); }, 22)->tooltip = "Details (I)";
    iconButton(bar, "delete", [st, &p] { trashCurrent(st, p); }, 22)->tooltip = "Move to the trash (Delete)";

    // The picture, with back and next over its sides
    auto* stage = card->add<Flex>(Flex::Direction::Row);
    st->stage = stage;
    stage->setAlignItems(Align::Center)->setSpacing(6)->setRadius(Radii(22));
    stage->setBackground(Config::get().value<std::string>("/viewer/backdrop", "theme") == "black" ? ColorRef(Color{0, 0, 0, 1}) : ColorRef("surface_container_lowest", 0.6f), false);
    stage->grow = true;
    auto* prev = iconButton(stage, "chevron_left", [st] { step(st, -1); }, 30);
    st->prev = prev;
    prev->tooltip = "Previous (←)";
    st->view = stage->add<ZoomView>();
    st->view->grow = true;
    auto* next = iconButton(stage, "chevron_right", [st] { step(st, 1); }, 30);
    st->next = next;
    next->tooltip = "Next (→)";

    st->infoBar = card->add<Flex>(Flex::Direction::Row);
    st->infoBar->setPadding({16, 6});
    st->info = st->infoBar->add<Label>("");
    st->info->setSize(13)->setColor("on_surface_variant", false)->setMaxLines(2);
    st->infoBar->setVisible(Config::get().value<bool>("/viewer/info", false));

    st->refresh = [st, prev, next] {
        if (st->pictures.empty()) return;
        const std::string f = st->pictures[st->index];
        st->name->setText(fs::path(f).filename().string());
        st->count->setText(st->pictures.size() > 1 ? std::format("{} of {}  ·  {}", st->index + 1, st->pictures.size(), fileops::displayPath(fs::path(f).parent_path().string()))
                                                   : fileops::displayPath(fs::path(f).parent_path().string()));
        prev->setOpacity(st->pictures.size() > 1 ? 1.0f : 0.3f);
        next->setOpacity(st->pictures.size() > 1 ? 1.0f : 0.3f);
        const int w = st->view->width(), h = st->view->height();
        std::error_code ec;
        const auto size = fs::file_size(f, ec);
        struct stat s {};
        std::string when;
        if (::stat(f.c_str(), &s) == 0) when = fileops::humanTime(s.st_mtime);
        st->info->setText(std::format("{}  ·  {} × {} px{}  ·  {}  ·  {}", fileops::kindName(fileops::Kind::Picture, f), w, h,
                                      st->view->animated() ? "  ·  animated" : "", fileops::humanSize(size), when));
        st->zoom->setText(st->view->fitted() ? "Fit" : std::format("{:.0f}%", st->view->scale() * 100));
        if (st->favIcon) st->favIcon->setFill(library().favourite(f))->setColor(library().favourite(f) ? "error" : "on_surface", false);
    };
    st->view->tapped = [st] {
        if (!st->sliding) return false;
        stopSlideshow(st);
        return true;
    };
    st->view->changed = [st] {
        if (st->refresh) st->zoom->setText(st->view->fitted() ? "Fit" : std::format("{:.0f}%", st->view->scale() * 100));
    };
    card->add<ViewerKeeper>(st);

    popup.onKey = [st, &p](const KeyEvent& e) {
        // The slideshow: arrows step, space pauses, anything else ends it
        if (st->sliding) {
            switch (e.sym) {
            case XKB_KEY_Left: step(st, -1); slideNext(st); return true;
            case XKB_KEY_Right: step(st, 1); slideNext(st); return true;
            case XKB_KEY_space:
                st->paused = !st->paused;
                slideNext(st);
                return true;
            default: stopSlideshow(st); return true;
            }
        }
        if (e.sym == XKB_KEY_Escape && st->ask->visible()) return st->ask->setVisible(false), true;
        // The gallery: Escape closes, F5 plays it, nothing else is the viewer's
        if (st->gallery->visible()) {
            if (e.sym == XKB_KEY_Escape) return p.close(), true;
            if (e.sym == XKB_KEY_F5) return startSlideshow(st, st->galleryPics), true;
            return false;
        }
        if (e.sym == XKB_KEY_Escape) {
            if (!st->pictures.empty()) st->galleryDir = fs::path(st->pictures[st->index]).parent_path().string();
            st->showGallery();
            return true;
        }
        switch (e.sym) {
        case XKB_KEY_Left:
        case XKB_KEY_Page_Up: step(st, -1); return true;
        case XKB_KEY_Right:
        case XKB_KEY_Page_Down:
        case XKB_KEY_space: step(st, 1); return true;
        case XKB_KEY_plus:
        case XKB_KEY_equal:
        case XKB_KEY_KP_Add: st->view->zoomBy(1.25f); return true;
        case XKB_KEY_minus:
        case XKB_KEY_KP_Subtract: st->view->zoomBy(1 / 1.25f); return true;
        case XKB_KEY_0: st->view->zoomTo(0); return true;
        case XKB_KEY_1: st->view->zoomTo(1); return true;
        case XKB_KEY_Delete: trashCurrent(st, p); return true;
        case XKB_KEY_r:
        case XKB_KEY_R: rotateCurrent(st, !e.shift()); return true;
        case XKB_KEY_c:
        case XKB_KEY_C:
            if (!e.ctrl()) return false;
            copyCurrent(st);
            return true;
        case XKB_KEY_i:
        case XKB_KEY_I: st->infoBar->setVisible(!st->infoBar->visible()); return true;
        case XKB_KEY_f:
        case XKB_KEY_F:
            if (!st->pictures.empty()) library().toggleFavourite(st->pictures[st->index]), albumsChanged(st);
            return true;
        case XKB_KEY_s:
        case XKB_KEY_S:
        case XKB_KEY_F5: startSlideshow(st); return true;
        case XKB_KEY_Escape: p.close(); return true;
        default: return false;
        }
    };

    viewerGo = [st](const std::string& f) {
        if (*st->alive) showAt(st, f);
    };
    // Laid out first: fitting needs the room it has
    if (file.empty()) st->showGallery();
    else {
        viewing(st, true);
        EventLoop::get().defer([st, file] {
            if (*st->alive) showAt(st, file);
        });
    }
}

void viewerClosed() {
    viewerGo = nullptr;
}

// Settings → Photos
void viewerSettings(Flex* page) {
    auto* p = group(page, "Pictures");
    switchRow(p, "fit_screen", "Fit small pictures to the window", "Enlarged to fill it; off, shown at their own size", "/viewer/upscale", false);
    switchRow(p, "delete", "Ask before moving to the trash", "Delete and the trash button ask first", "/viewer/confirmTrash", false);
    auto* g = group(page, "Gallery");
    choiceRow<std::string>(g, "sort", "Order", "How the gallery lists a folder's pictures", "/viewer/sort", "date", {{"date", "Newest first"}, {"name", "By name"}});
    auto* v = group(page, "Viewing", "From the next picture you open");
    switchRow(v, "info", "Show the details bar", "Size, dimensions and date under the picture (I)", "/viewer/info", false);
    switchRow(v, "repeat", "Wrap around", "Past the last picture, back to the first", "/viewer/loop", true);
    choiceRow<std::string>(v, "contrast", "Backdrop", "Behind the picture", "/viewer/backdrop", "theme", {{"theme", "Theme"}, {"black", "Black"}});
    auto* s = group(page, "Slideshow", "F5 or S; arrows step, space pauses, a click or any other key ends it");
    choiceRow<int>(s, "timer", "Each picture for", "Before the next one", "/viewer/slideSeconds", 5, {{3, "3 seconds"}, {5, "5 seconds"}, {10, "10 seconds"}, {20, "20 seconds"}});
    switchRow(s, "shuffle", "Shuffle", "The pictures in a random order", "/viewer/slideShuffle", false);
}

} // namespace m3::shell::panels
