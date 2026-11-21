// The image viewer: one picture at a time in an app window, fitted to it,
// with the others in its folder a key or a click away. The wheel, + and -
// zoom (Ctrl+0 back to fitting, 1 to the picture's own pixels, a double
// click between the two); dragging moves a zoomed picture. Its bar edits it
// (the shell's image editor), makes it the wallpaper, shows it in Files or
// moves it to the trash. Files opens pictures here; it's also the
// `material3ui-viewer` app for picture files.
#include "shell/Panels.h"

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

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <algorithm>
#include <chrono>
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

// The picture, fitted or zoomed, and moved about when it's bigger than the room
class ZoomView : public Node {
public:
    std::function<void()> changed; // zoomed or moved: the bar's percentage

    void show(const std::string& path) {
        m_path = path;
        m_zoom = 0;
        m_panX = m_panY = 0;
        int w = 0, h = 0;
        gdk_pixbuf_get_file_info(path.c_str(), &w, &h);
        m_w = std::max(1, w);
        m_h = std::max(1, h);
        requestRedraw();
        if (changed) changed();
    }
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
    void doPaint(Renderer& r) override {
        if (m_path.empty()) return;
        const auto& b = rect();
        const float s = scale();
        const float dw = static_cast<float>(m_w) * s, dh = static_cast<float>(m_h) * s;
        // Decoded once, at its own size (a huge photo capped): zooming
        // draws the same pixels bigger or smaller, no new decode each step
        const int want = std::min(4096, std::max(m_w, m_h));
        const auto* t = Images::get().picture(m_path, want, want, false);
        if (!t) return; // decoding: drawn when it's ready
        r.pushClip(b);
        r.image(t->id, {b.x + (b.w - dw) / 2 + m_panX, b.y + (b.h - dh) / 2 + m_panY, dw, dh});
        r.popClip();
    }
    void doArrange() override { clampPan(); }

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

    std::string m_path;
    int m_w = 1, m_h = 1;
    float m_zoom = 0, m_panX = 0, m_panY = 0, m_dragX = 0, m_dragY = 0;
    Clock::time_point m_lastPress;
};

struct ViewerState {
    std::vector<std::string> pictures; // the folder's, in order
    size_t index = 0;
    ZoomView* view = nullptr;
    Label *name = nullptr, *count = nullptr, *zoom = nullptr, *info = nullptr;
    Flex* infoBar = nullptr;
    std::function<void()> refresh;
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

void showAt(const std::shared_ptr<ViewerState>& st, std::string file) {
    std::error_code ec;
    if (fs::is_directory(file, ec) || !fs::exists(file, ec)) return;
    file = fs::weakly_canonical(file, ec).string();
    st->pictures = siblings(file);
    st->index = static_cast<size_t>(std::ranges::find(st->pictures, file) - st->pictures.begin());
    st->view->show(file);
    st->refresh();
}

void step(const std::shared_ptr<ViewerState>& st, int by) {
    if (st->pictures.size() < 2) return;
    const auto n = static_cast<long>(st->pictures.size());
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
    if (st->pictures.empty()) return popup.close();
    if (st->index >= st->pictures.size()) st->index = 0;
    st->view->show(st->pictures[st->index]);
    st->refresh();
}

class ViewerKeeper : public Node {
public:
    explicit ViewerKeeper(std::shared_ptr<ViewerState> st) : m_st(std::move(st)) { floating = true; }
    ~ViewerKeeper() override {
        *m_st->alive = false;
        m_st->refresh = nullptr;
        m_st->view->changed = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<ViewerState> m_st;
};

} // namespace

std::function<void(const std::string&)> viewerGo;

void viewer(Flex* card, Popup& popup, const std::string& file) {
    auto st = std::make_shared<ViewerState>();
    card->setPadding(Insets(10))->setSpacing(8);

    // The bar: which picture, and what to do with it
    auto* bar = card->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(4)->setAlignItems(Align::Center)->setPadding({8, 0});
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
        if (!st->pictures.empty()) Wallpaper::get().set(st->pictures[st->index]);
    }, 22)->tooltip = "Set as wallpaper";
    iconButton(bar, "folder_open", [st] {
        if (!st->pictures.empty() && open) open("files " + st->pictures[st->index]);
    }, 22)->tooltip = "Show in Files";
    iconButton(bar, "info", [st] { st->infoBar->setVisible(!st->infoBar->visible()); }, 22)->tooltip = "Details (I)";
    iconButton(bar, "delete", [st, &p] { trashCurrent(st, p); }, 22)->tooltip = "Move to the trash (Delete)";

    // The picture, with back and next over its sides
    auto* stage = card->add<Flex>(Flex::Direction::Row);
    stage->setAlignItems(Align::Center)->setSpacing(6)->setRadius(Radii(22))->setBackground(ColorRef("surface_container_lowest", 0.6f), false);
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
    st->infoBar->setVisible(false);

    st->refresh = [st, prev, next] {
        if (st->pictures.empty()) return;
        const std::string f = st->pictures[st->index];
        st->name->setText(fs::path(f).filename().string());
        st->count->setText(st->pictures.size() > 1 ? std::format("{} of {}  ·  {}", st->index + 1, st->pictures.size(), fileops::displayPath(fs::path(f).parent_path().string()))
                                                   : fileops::displayPath(fs::path(f).parent_path().string()));
        prev->setOpacity(st->pictures.size() > 1 ? 1.0f : 0.3f);
        next->setOpacity(st->pictures.size() > 1 ? 1.0f : 0.3f);
        int w = 0, h = 0;
        gdk_pixbuf_get_file_info(f.c_str(), &w, &h);
        std::error_code ec;
        const auto size = fs::file_size(f, ec);
        struct stat s {};
        std::string when;
        if (::stat(f.c_str(), &s) == 0) when = fileops::humanTime(s.st_mtime);
        st->info->setText(std::format("{}  ·  {} × {} px  ·  {}  ·  {}", fileops::kindName(fileops::Kind::Picture, f), w, h, fileops::humanSize(size), when));
        st->zoom->setText(st->view->fitted() ? "Fit" : std::format("{:.0f}%", st->view->scale() * 100));
    };
    st->view->changed = [st] {
        if (st->refresh) st->zoom->setText(st->view->fitted() ? "Fit" : std::format("{:.0f}%", st->view->scale() * 100));
    };
    card->add<ViewerKeeper>(st);

    popup.onKey = [st, &p](const KeyEvent& e) {
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
    EventLoop::get().defer([st, file] {
        if (*st->alive) showAt(st, file);
    });
}

void viewerClosed() {
    viewerGo = nullptr;
}

} // namespace m3::shell::panels
