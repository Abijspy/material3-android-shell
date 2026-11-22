// The image viewer: one picture at a time in an app window, fitted to it,
// with the others in its folder a key or a click away. The wheel, + and -
// zoom (Ctrl+0 back to fitting, 1 to the picture's own pixels, a double
// click between the two); dragging moves a zoomed picture. Its bar edits it
// (the shell's image editor), makes it the wallpaper, shows it in Files or
// moves it to the trash. Files opens pictures here; it's also the
// `material3ui-viewer` app for picture files.
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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

using Clock = std::chrono::steady_clock;

// The picture, fitted or zoomed, and moved about when it's bigger than the
// room. Any format: what gdk-pixbuf reads (PNG, JPEG, WebP, AVIF, HEIC, JXL,
// TIFF, EXR, QOI…) as it is, the rest (PSD, XCF, camera RAW, JPEG 2000…)
// through ImageMagick (fileops::renderable). Animated GIF and WebP play,
// one frame at a time in a texture of its own
class ZoomView : public Node {
public:
    std::function<void()> changed; // zoomed or moved: the bar's percentage

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
        return std::min({b.w / static_cast<float>(m_w), b.h / static_cast<float>(m_h), 1.0f});
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

void showAt(const std::shared_ptr<ViewerState>& st, std::string file) {
    std::error_code ec;
    if (fs::is_directory(file, ec)) {
        st->galleryDir = file;
        if (st->showGallery) st->showGallery();
        return;
    }
    if (!fs::exists(file, ec)) return;
    viewing(st, true);
    file = fs::weakly_canonical(file, ec).string();
    st->pictures = siblings(file);
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
    st->refresh();
}

// Into the trash, and on to the next
void trashCurrent(const std::shared_ptr<ViewerState>& st, Popup& popup) {
    if (st->pictures.empty()) return;
    const std::string file = st->pictures[st->index];
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
        m_st->view = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<ViewerState> m_st;
};

} // namespace

std::function<void(const std::string&)> viewerGo;

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

void buildGallery(const std::shared_ptr<ViewerState>& st) {
    auto* g = st->grid;
    g->clearChildren();
    std::error_code ec;
    if (st->galleryDir.empty()) {
        const auto places = galleryPlaces();
        st->galleryDir = places.empty() ? paths::home() : places.front().second;
    }
    st->galleryTitle->setText(fileops::displayPath(st->galleryDir));
    struct Pic {
        std::string path;
        std::time_t when;
    };
    std::vector<Pic> pics;
    for (const auto& e : fileops::list(st->galleryDir, false))
        if (!e.dir && fileops::isPicture(e.path)) pics.push_back({e.path, e.modified});
    if (Config::get().value<std::string>("/viewer/sort", "date") == "name")
        std::ranges::sort(pics, [](const Pic& a, const Pic& b) { return strverscmp(fs::path(a.path).filename().c_str(), fs::path(b.path).filename().c_str()) < 0; });
    else
        std::ranges::sort(pics, std::greater{}, &Pic::when);
    if (pics.size() > 600) pics.resize(600);
    auto* flow = g->add<Flow>();
    flow->spacing = 8;
    for (const auto& pic : pics) {
        auto* b = flow->add<Button>();
        b->setPadding(Insets(0))->setRadius(Radii(14));
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
        b->onClicked = [st, path](uint32_t button) {
            if (button == BTN_LEFT) showAt(st, path);
        };
    }
    if (pics.empty()) caption(g, "No pictures in this folder");
}

void viewer(Flex* card, Popup& popup, const std::string& file) {
    auto st = std::make_shared<ViewerState>();
    card->setPadding(Insets(10))->setSpacing(8);

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
                buildGallery(st);
            };
        }
        iconButton(head, "folder_open", [st] {
            if (open) open("files " + st->galleryDir);
        }, 22)->tooltip = "Open this folder in Files";
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
    iconButton(bar, "brush", [st] {
        if (!st->pictures.empty() && open) open("imageeditor " + st->pictures[st->index]);
    }, 22)->tooltip = "Edit";
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
    prev->tooltip = "Previous (←)";
    st->view = stage->add<ZoomView>();
    st->view->grow = true;
    auto* next = iconButton(stage, "chevron_right", [st] { step(st, 1); }, 30);
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
    };
    st->view->changed = [st] {
        if (st->refresh) st->zoom->setText(st->view->fitted() ? "Fit" : std::format("{:.0f}%", st->view->scale() * 100));
    };
    card->add<ViewerKeeper>(st);

    popup.onKey = [st, &p](const KeyEvent& e) {
        // The gallery: Escape closes, nothing else is the viewer's
        if (st->gallery->visible()) {
            if (e.sym == XKB_KEY_Escape) return p.close(), true;
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
        case XKB_KEY_i:
        case XKB_KEY_I: st->infoBar->setVisible(!st->infoBar->visible()); return true;
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
    auto* g = group(page, "Gallery");
    choiceRow<std::string>(g, "sort", "Order", "How the gallery lists a folder's pictures", "/viewer/sort", "date", {{"date", "Newest first"}, {"name", "By name"}});
    auto* v = group(page, "Viewing", "From the next picture you open");
    switchRow(v, "info", "Show the details bar", "Size, dimensions and date under the picture (I)", "/viewer/info", false);
    switchRow(v, "repeat", "Wrap around", "Past the last picture, back to the first", "/viewer/loop", true);
    choiceRow<std::string>(v, "contrast", "Backdrop", "Behind the picture", "/viewer/backdrop", "theme", {{"theme", "Theme"}, {"black", "Black"}});
}

} // namespace m3::shell::panels
