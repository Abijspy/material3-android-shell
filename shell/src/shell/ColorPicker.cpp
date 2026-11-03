#include "shell/ColorPicker.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "ui/Node.h"
#include "ui/Theme.h"
#include "wayland/Display.h"
#include "wayland/LayerSurface.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell::colorpicker {

using namespace m3::ui;

namespace {

constexpr int kGrid = 11;     // pixels across the loupe
constexpr float kCell = 12;   // each drawn this big
constexpr size_t kRecent = 10;

std::string hex(uint8_t r, uint8_t g, uint8_t b) {
    return std::format("#{:02x}{:02x}{:02x}", r, g, b);
}

// The frozen screen, the pointer over it, and the loupe beside the pointer
class View : public Node {
public:
    std::function<void(const std::string&)> onPick;
    std::function<void()> onCancel;

    explicit View(cairo_surface_t* shot) : m_shot(shot) {
        m_w = cairo_image_surface_get_width(shot);
        m_h = cairo_image_surface_get_height(shot);
        m_stride = cairo_image_surface_get_stride(shot);
        m_data = cairo_image_surface_get_data(shot);
        m_label = add<Label>();
        m_label->setSize(14)->setWeight(600)->setFamily(Theme::get().monoFamily());
    }
    ~View() override {
        if (m_tex) glDeleteTextures(1, &m_tex);
        cairo_surface_destroy(m_shot);
    }
    bool interactive() const override { return true; }
    void onPointerMove(float x, float y) override {
        m_x = x;
        m_y = y;
        m_seen = true;
        m_label->setText(hexAt(x, y));
        requestLayout();
    }
    void onPress(uint32_t button, float x, float y) override {
        if (button == BTN_LEFT && onPick) onPick(hexAt(x, y));
        else if (onCancel) onCancel();
    }

protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doArrange() override {
        // The hex in a pill under the loupe
        const SizeF s = m_label->measure({300, 40});
        const RectF l = loupe();
        m_label->arrange({l.x + (l.w - s.w) / 2, l.bottom() + 14, s.w, s.h});
    }
    void doPaint(Renderer& r) override {
        if (!m_tex) {
            // Cairo's ARGB32 is the BGRA the image program takes
            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_stride / 4, m_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_data);
        }
        const RectF b = rect();
        r.image(m_tex, {b.x, b.y, b.w * static_cast<float>(m_stride / 4) / static_cast<float>(m_w), b.h});
        if (!m_seen) return;
        // The loupe: the pixels round the pointer, each a square, the one
        // under it outlined; framed in the colour picked
        const RectF l = loupe();
        int px = 0, py = 0;
        pixelAt(m_x, m_y, px, py);
        const Color here = colourAt(px, py);
        // A rounded square (clips are rectangles: a round frame would leave
        // the grid's corners outside it)
        r.rect({l.x - 6, l.y - 6, l.w + 12, l.h + 12}, here, Radii(12), 3, Color{1, 1, 1, 0.9f});
        r.pushClip(l);
        for (int gy = 0; gy < kGrid; ++gy)
            for (int gx = 0; gx < kGrid; ++gx)
                r.rect({l.x + gx * kCell, l.y + gy * kCell, kCell, kCell}, colourAt(px + gx - kGrid / 2, py + gy - kGrid / 2));
        r.popClip();
        const float c = l.x + (kGrid / 2) * kCell, d = l.y + (kGrid / 2) * kCell;
        r.rect({c, d, kCell, kCell}, Color{0, 0, 0, 0}, Radii(2), 2, Color{1, 1, 1, 1});
        r.rect({c - 1, d - 1, kCell + 2, kCell + 2}, Color{0, 0, 0, 0}, Radii(3), 1, Color{0, 0, 0, 0.6f});
        // The label's pill
        const RectF t = m_label->rect();
        r.rect({t.x - 12, t.y - 6, t.w + 24, t.h + 12}, ColorRef("surface_container_highest").resolve(), Radii((t.h + 12) / 2));
    }

private:
    RectF loupe() const {
        const float size = kGrid * kCell;
        // Beside the pointer, kept on screen
        float x = m_x + 28, y = m_y + 28;
        if (x + size > rect().right() - 8) x = m_x - 28 - size;
        if (y + size + 50 > rect().bottom() - 8) y = m_y - 28 - size - 50;
        return {x, y, size, size};
    }
    void pixelAt(float x, float y, int& px, int& py) const {
        px = std::clamp(static_cast<int>(x / rect().w * static_cast<float>(m_w)), 0, m_w - 1);
        py = std::clamp(static_cast<int>(y / rect().h * static_cast<float>(m_h)), 0, m_h - 1);
    }
    void rgbAt(int px, int py, uint8_t& r, uint8_t& g, uint8_t& b) const {
        px = std::clamp(px, 0, m_w - 1);
        py = std::clamp(py, 0, m_h - 1);
        const uint32_t v = *reinterpret_cast<const uint32_t*>(m_data + static_cast<ptrdiff_t>(py) * m_stride + px * 4);
        r = static_cast<uint8_t>(v >> 16);
        g = static_cast<uint8_t>(v >> 8);
        b = static_cast<uint8_t>(v);
    }
    Color colourAt(int px, int py) const {
        uint8_t r, g, b;
        rgbAt(px, py, r, g, b);
        return {r / 255.0f, g / 255.0f, b / 255.0f, 1};
    }
    std::string hexAt(float x, float y) const {
        int px = 0, py = 0;
        pixelAt(x, y, px, py);
        uint8_t r, g, b;
        rgbAt(px, py, r, g, b);
        return hex(r, g, b);
    }

    cairo_surface_t* m_shot;
    unsigned char* m_data = nullptr;
    int m_w = 0, m_h = 0, m_stride = 0;
    GLuint m_tex = 0;
    float m_x = 0, m_y = 0;
    bool m_seen = false;
    Label* m_label;
};

std::unique_ptr<wl::LayerSurface> g_surface;
bool g_starting = false;

void finish() {
    // Not inside its own event: the view goes with the surface
    EventLoop::get().defer([] { g_surface.reset(); });
}

void picked(const std::string& colour) {
    run({"wl-copy", "--", colour}, nullptr);
    auto list = Config::get().value<nlohmann::json>("/tools/colours", nlohmann::json::array());
    nlohmann::json out = nlohmann::json::array({colour});
    for (const auto& c : list)
        if (c.is_string() && c.get<std::string>() != colour && out.size() < kRecent) out.push_back(c);
    Config::get().set("/tools/colours", out);
    unsigned r = 0, g = 0, b = 0;
    std::sscanf(colour.c_str(), "#%02x%02x%02x", &r, &g, &b);
    run({"notify-send", "-a", "Colour picker", "-i", "color-select", "Colour copied", std::format("{} · rgb({}, {}, {})", colour, r, g, b)},
        nullptr);
    finish();
}

} // namespace

bool active() {
    return g_surface != nullptr || g_starting;
}

std::vector<std::string> recent() {
    std::vector<std::string> out;
    for (const auto& c : Config::get().value<nlohmann::json>("/tools/colours", nlohmann::json::array()))
        if (c.is_string()) out.push_back(c.get<std::string>());
    return out;
}

void start(wl::Output* output) {
    if (active() || !output) return;
    if (!haveCommand("grim")) {
        run({"notify-send", "-a", "Colour picker", "grim isn't installed", "Settings → Tools installs it"}, nullptr);
        return;
    }
    g_starting = true;
    const std::string file = paths::runtimeDir() + "/colour-pick.png";
    std::error_code ec;
    std::filesystem::create_directories(paths::runtimeDir(), ec);
    const std::string name = output->name;
    run({"grim", "-o", name, file}, [file, name](int code, const std::string&) {
        g_starting = false;
        cairo_surface_t* shot = code == 0 ? cairo_image_surface_create_from_png(file.c_str()) : nullptr;
        std::error_code rm;
        std::filesystem::remove(file, rm);
        const bool usable = shot && cairo_surface_status(shot) == CAIRO_STATUS_SUCCESS &&
                            (cairo_image_surface_get_format(shot) == CAIRO_FORMAT_ARGB32 || cairo_image_surface_get_format(shot) == CAIRO_FORMAT_RGB24);
        if (!usable) {
            if (shot) cairo_surface_destroy(shot);
            log::warn("colour picker: couldn't capture {}", name);
            return;
        }
        // The output may have gone while grim ran
        wl::Output* out = nullptr;
        for (const auto& o : wl::Display::get().outputs())
            if (o->name == name) out = o.get();
        if (!out) {
            cairo_surface_destroy(shot);
            return;
        }
        wl::LayerSurface::Options o;
        o.ns = "material3ui-colourpicker";
        o.layer = wl::LayerSurface::Layer::Overlay;
        o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
        o.exclusiveZone = -1; // over the bar too: the whole screen, as captured
        o.keyboard = wl::LayerSurface::Keyboard::Exclusive;
        g_surface = std::make_unique<wl::LayerSurface>(out, o);
        auto view = std::make_unique<View>(shot);
        view->onPick = picked;
        view->onCancel = finish;
        g_surface->setRoot(std::move(view));
        g_surface->onKey = [](const KeyEvent& e) {
            if (e.sym != XKB_KEY_Escape) return false;
            finish();
            return true;
        };
    });
}

} // namespace m3::shell::colorpicker
