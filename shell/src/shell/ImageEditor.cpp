// The shell's image editor: a screenshot (or any picture) to draw on,
// highlight, cover up, add text to and crop, then copy or save. Drawn
// with Cairo into the picture itself, so what's shown is what's saved
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <filesystem>
#include <format>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <linux/input-event-codes.h>
#include <memory>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell::panels {

using namespace m3::ui;

namespace {

namespace fs = std::filesystem;

enum class Tool { Pen, Arrow, Box, Highlight, Text, Pixelate, Crop };

struct Pt {
    float x = 0, y = 0;
};

struct Shape {
    Tool tool = Tool::Pen;
    Color colour{1, 0, 0, 1};
    float width = 4;
    std::vector<Pt> pts; // the pen's path; else its two corners (or the text's place)
    std::string text;
};

// 8 bits a channel, premultiplied ARGB: what the pixelate tool reads.
// Cairo loads 16-bit PNGs as floating point (RGBA128F)
cairo_surface_t* argb32(cairo_surface_t* s) {
    if (cairo_image_surface_get_format(s) == CAIRO_FORMAT_ARGB32) return s;
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
    cairo_t* cr = cairo_create(out);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return out;
}

// The picture as a Cairo surface: PNG directly, anything else through
// gdk-pixbuf (premultiplied into Cairo's layout)
cairo_surface_t* load(const std::string& path) {
    if (path.ends_with(".png") || path.ends_with(".PNG")) {
        cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return argb32(s);
        cairo_surface_destroy(s);
    }
    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file(path.c_str(), &err);
    if (!pb) {
        if (err) g_error_free(err);
        return nullptr;
    }
    const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), n = gdk_pixbuf_get_n_channels(pb);
    const int rs = gdk_pixbuf_get_rowstride(pb);
    const bool alpha = gdk_pixbuf_get_has_alpha(pb);
    const guchar* src = gdk_pixbuf_read_pixels(pb);
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(s);
    unsigned char* dst = cairo_image_surface_get_data(s);
    const int ds = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const guchar* p = src + static_cast<ptrdiff_t>(y) * rs + x * n;
            const unsigned a = alpha ? p[3] : 255;
            const unsigned r = p[0] * a / 255, g = p[1] * a / 255, b = p[2] * a / 255;
            *reinterpret_cast<uint32_t*>(dst + static_cast<ptrdiff_t>(y) * ds + x * 4) = (a << 24) | (r << 16) | (g << 8) | b;
        }
    cairo_surface_mark_dirty(s);
    g_object_unref(pb);
    return s;
}

RectF corners(const Shape& s) {
    const Pt a = s.pts.front(), b = s.pts.back();
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

void setColour(cairo_t* cr, const Color& c, float alpha = 1) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha);
}

// Everything drawn over the picture, in order (crops aren't drawn)
void paintShapes(cairo_t* cr, cairo_surface_t* base, const std::vector<const Shape*>& shapes) {
    cairo_set_source_surface(cr, base, 0, 0);
    cairo_paint(cr);
    const int iw = cairo_image_surface_get_width(base), ih = cairo_image_surface_get_height(base);
    for (const Shape* s : shapes) {
        if (s->pts.empty()) continue;
        cairo_save(cr);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_width(cr, s->width);
        switch (s->tool) {
        case Tool::Pen:
            setColour(cr, s->colour);
            cairo_move_to(cr, s->pts[0].x, s->pts[0].y);
            for (const auto& p : s->pts) cairo_line_to(cr, p.x, p.y);
            if (s->pts.size() == 1) cairo_line_to(cr, s->pts[0].x + 0.1, s->pts[0].y); // a dot
            cairo_stroke(cr);
            break;
        case Tool::Arrow: {
            const Pt a = s->pts.front(), b = s->pts.back();
            const double angle = std::atan2(b.y - a.y, b.x - a.x), head = std::max(14.0, s->width * 4.5);
            setColour(cr, s->colour);
            // The shaft ends inside the head, so its round end doesn't show past the point
            cairo_move_to(cr, a.x, a.y);
            cairo_line_to(cr, b.x - std::cos(angle) * head * 0.6, b.y - std::sin(angle) * head * 0.6);
            cairo_stroke(cr);
            cairo_move_to(cr, b.x, b.y);
            cairo_line_to(cr, b.x - head * std::cos(angle - 0.45), b.y - head * std::sin(angle - 0.45));
            cairo_line_to(cr, b.x - head * std::cos(angle + 0.45), b.y - head * std::sin(angle + 0.45));
            cairo_close_path(cr);
            cairo_fill(cr);
            break;
        }
        case Tool::Box: {
            const RectF r = corners(*s);
            setColour(cr, s->colour);
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            cairo_stroke(cr);
            break;
        }
        case Tool::Highlight: {
            // A marker's tint: the colour multiplied into what's under it
            const RectF r = corners(*s);
            cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
            setColour(cr, s->colour, 0.55f);
            cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            cairo_fill(cr);
            break;
        }
        case Tool::Pixelate: {
            // Each block the average of what's under it: what was there can't be read
            const RectF r = corners(*s);
            const int block = std::max(8, std::max(iw, ih) / 100);
            cairo_surface_flush(base);
            const unsigned char* data = cairo_image_surface_get_data(base);
            const int stride = cairo_image_surface_get_stride(base);
            for (float y = r.y; y < r.y + r.h; y += static_cast<float>(block))
                for (float x = r.x; x < r.x + r.w; x += static_cast<float>(block)) {
                    const float bw = std::min(static_cast<float>(block), r.x + r.w - x), bh = std::min(static_cast<float>(block), r.y + r.h - y);
                    const int x0 = std::clamp(static_cast<int>(x), 0, iw - 1), y0 = std::clamp(static_cast<int>(y), 0, ih - 1);
                    const int x1 = std::clamp(static_cast<int>(x + bw), x0 + 1, iw), y1 = std::clamp(static_cast<int>(y + bh), y0 + 1, ih);
                    double sr = 0, sg = 0, sb = 0;
                    for (int py = y0; py < y1; ++py)
                        for (int px = x0; px < x1; ++px) {
                            const uint32_t v = *reinterpret_cast<const uint32_t*>(data + static_cast<ptrdiff_t>(py) * stride + px * 4);
                            sr += (v >> 16) & 255;
                            sg += (v >> 8) & 255;
                            sb += v & 255;
                        }
                    const double n = static_cast<double>((x1 - x0) * (y1 - y0)) * 255.0;
                    cairo_set_source_rgb(cr, sr / n, sg / n, sb / n);
                    cairo_rectangle(cr, x, y, bw, bh);
                    cairo_fill(cr);
                }
            break;
        }
        case Tool::Text: {
            // In bold, with an outline of the opposite lightness so it reads anywhere
            PangoLayout* layout = pango_cairo_create_layout(cr);
            PangoFontDescription* font = pango_font_description_from_string("Sans Bold");
            pango_font_description_set_absolute_size(font, s->width * 6 * PANGO_SCALE);
            pango_layout_set_font_description(layout, font);
            pango_layout_set_text(layout, s->text.c_str(), -1);
            cairo_move_to(cr, s->pts[0].x, s->pts[0].y);
            pango_cairo_layout_path(cr, layout);
            const float light = 0.299f * s->colour.r + 0.587f * s->colour.g + 0.114f * s->colour.b;
            cairo_set_source_rgba(cr, light > 0.5f ? 0 : 1, light > 0.5f ? 0 : 1, light > 0.5f ? 0 : 1, 0.85);
            cairo_set_line_width(cr, std::max(2.0f, s->width * 0.7f));
            cairo_stroke_preserve(cr);
            setColour(cr, s->colour);
            cairo_fill(cr);
            pango_font_description_free(font);
            g_object_unref(layout);
            break;
        }
        case Tool::Crop: break;
        }
        cairo_restore(cr);
    }
}

// The picture with what's drawn on it, the last crop, the pen and the rest
class Canvas : public Node {
public:
    Tool tool = Tool::Arrow;
    Color colour{0.96f, 0.26f, 0.21f, 1};
    int size = 1; // 0 thin, 1 medium, 2 thick
    std::function<void(Pt)> onTextAt;

    explicit Canvas(cairo_surface_t* base) : m_base(base) {
        m_w = cairo_image_surface_get_width(base);
        m_h = cairo_image_surface_get_height(base);
        m_view = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, m_w, m_h);
        m_unit = std::max(2.0f, static_cast<float>(std::max(m_w, m_h)) / 640.0f);
    }
    ~Canvas() override {
        if (m_tex) glDeleteTextures(1, &m_tex);
        cairo_surface_destroy(m_view);
        cairo_surface_destroy(m_base);
    }

    float width() const { return m_unit * (size == 0 ? 1.0f : size == 1 ? 2.0f : 3.5f); }
    void addText(Pt at, const std::string& text) {
        if (text.empty()) return;
        push({Tool::Text, colour, width(), {at}, text});
    }
    void undo() {
        if (m_shapes.empty()) return;
        m_undone.push_back(m_shapes.back());
        m_shapes.pop_back();
        changed();
    }
    void redo() {
        if (m_undone.empty()) return;
        m_shapes.push_back(m_undone.back());
        m_undone.pop_back();
        changed();
    }
    bool edited() const { return !m_shapes.empty(); }

    // As a PNG at `path`, cropped as the last crop says
    bool save(const std::string& path) const {
        RectF c{0, 0, static_cast<float>(m_w), static_cast<float>(m_h)};
        if (const Shape* crop = lastCrop(nullptr)) c = corners(*crop);
        const int cx = static_cast<int>(std::floor(c.x)), cy = static_cast<int>(std::floor(c.y));
        const int cw = std::max(1, static_cast<int>(std::ceil(c.w))), ch = std::max(1, static_cast<int>(std::ceil(c.h)));
        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
        cairo_t* cr = cairo_create(out);
        cairo_translate(cr, -cx, -cy);
        paintShapes(cr, m_base, drawn(nullptr));
        cairo_destroy(cr);
        const bool ok = cairo_surface_write_to_png(out, path.c_str()) == CAIRO_STATUS_SUCCESS;
        cairo_surface_destroy(out);
        return ok;
    }

    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float y) override {
        if (button != BTN_LEFT) return;
        const Pt p = toImage(x, y);
        if (tool == Tool::Text) {
            if (onTextAt) onTextAt(p);
            return;
        }
        m_drawing = std::make_unique<Shape>(Shape{tool, colour, width(), {p, p}, {}});
        if (tool == Tool::Pen) m_drawing->pts.resize(1);
        m_dirty = true;
        requestRedraw();
    }
    void onDrag(float x, float y) override {
        if (!m_drawing) return;
        const Pt p = toImage(x, y);
        if (m_drawing->tool == Tool::Pen) {
            const Pt last = m_drawing->pts.back();
            if (std::hypot(p.x - last.x, p.y - last.y) < 1.5f) return;
            m_drawing->pts.push_back(p);
        } else {
            m_drawing->pts.back() = p;
        }
        m_dirty = true;
        requestRedraw();
    }
    void onRelease() override {
        if (!m_drawing) return;
        auto s = std::move(m_drawing);
        // A slip of a click draws nothing (but the pen's dot)
        const RectF r = corners(*s);
        const bool tiny = s->tool != Tool::Pen && r.w < 4 && r.h < 4;
        if (!tiny) push(*s);
        m_dirty = true;
        requestRedraw();
    }

protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doPaint(Renderer& r) override {
        m_scale = r.scale();
        const RectF b = rect();
        const RectF f = fitted();
        if (m_dirty || !m_tex) render();
        r.rect(b, ColorRef("surface_container_lowest").resolve(), Radii(16));
        r.image(m_tex, f);
        // A crop being made or made: outside it dimmed, its edge marked
        RectF crop{0, 0, 0, 0};
        if (const Shape* c = lastCrop(m_drawing.get())) {
            const RectF ci = corners(*c);
            const float k = f.w / static_cast<float>(m_w);
            crop = {f.x + ci.x * k, f.y + ci.y * k, ci.w * k, ci.h * k};
            const Color dim{0, 0, 0, 0.55f};
            r.rect({f.x, f.y, f.w, crop.y - f.y}, dim);
            r.rect({f.x, crop.bottom(), f.w, f.bottom() - crop.bottom()}, dim);
            r.rect({f.x, crop.y, crop.x - f.x, crop.h}, dim);
            r.rect({crop.right(), crop.y, f.right() - crop.right(), crop.h}, dim);
            r.rect(crop, Color{0, 0, 0, 0}, Radii(0), 2, Color{1, 1, 1, 0.95f});
        }
    }

private:
    void push(const Shape& s) {
        m_shapes.push_back(s);
        m_undone.clear();
        changed();
    }
    void changed() {
        m_dirty = true;
        requestRedraw();
    }
    const Shape* lastCrop(const Shape* drawing) const {
        if (drawing && drawing->tool == Tool::Crop) return drawing;
        for (auto it = m_shapes.rbegin(); it != m_shapes.rend(); ++it)
            if (it->tool == Tool::Crop) return &*it;
        return nullptr;
    }
    std::vector<const Shape*> drawn(const Shape* drawing) const {
        std::vector<const Shape*> out;
        for (const auto& s : m_shapes)
            if (s.tool != Tool::Crop) out.push_back(&s);
        if (drawing && drawing->tool != Tool::Crop) out.push_back(drawing);
        return out;
    }
    // Fitted to the room, never larger than its own pixels
    RectF fitted() const {
        const RectF b = rect();
        const float k = std::min({b.w / static_cast<float>(m_w), b.h / static_cast<float>(m_h), 1.0f / std::max(m_scale, 0.5f)});
        const float w = static_cast<float>(m_w) * k, h = static_cast<float>(m_h) * k;
        return {b.x + (b.w - w) / 2, b.y + (b.h - h) / 2, w, h};
    }
    Pt toImage(float x, float y) const {
        const RectF f = fitted();
        const float k = static_cast<float>(m_w) / std::max(f.w, 1.0f);
        return {std::clamp((x - f.x) * k, 0.0f, static_cast<float>(m_w)), std::clamp((y - f.y) * k, 0.0f, static_cast<float>(m_h))};
    }
    void render() {
        cairo_t* cr = cairo_create(m_view);
        paintShapes(cr, m_base, drawn(m_drawing.get()));
        cairo_destroy(cr);
        cairo_surface_flush(m_view);
        const unsigned char* data = cairo_image_surface_get_data(m_view);
        // Cairo's ARGB32 is the BGRA the image program takes
        if (!m_tex) {
            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_w, m_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_w, m_h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
        m_dirty = false;
    }

    cairo_surface_t* m_base;
    cairo_surface_t* m_view;
    int m_w = 0, m_h = 0;
    float m_unit = 3, m_scale = 1;
    GLuint m_tex = 0;
    bool m_dirty = true;
    std::vector<Shape> m_shapes, m_undone;
    std::unique_ptr<Shape> m_drawing;
};

// Where an edited picture goes: a PNG over itself, anything else beside it as a PNG
std::string savePath(const std::string& path) {
    if (path.ends_with(".png") || path.ends_with(".PNG")) return path;
    const fs::path p(path);
    return (p.parent_path() / (p.stem().string() + "-edited.png")).string();
}

} // namespace

void imageEditor(Flex* card, Popup& popup, const std::string& path) {
    card->setSpacing(12);
    cairo_surface_t* base = path.empty() ? nullptr : load(path);
    if (!base) {
        auto* head = card->add<Flex>(Flex::Direction::Row);
        head->setSpacing(12);
        head->add<Label>(path.empty() ? "No picture to edit" : "Couldn't open " + fs::path(path).filename().string())->setSize(16);
        pillButton(head, "Close", false, [&popup] { popup.close(); }, "close");
        return;
    }

    auto* bar = card->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(8)->setAlignItems(Align::Center);
    bar->wrap = true;
    // The text tool's field: where it was clicked, what to write
    auto* textRow = card->add<Flex>(Flex::Direction::Row);
    textRow->setSpacing(10);
    textRow->setVisible(false);
    auto* canvas = card->add<Canvas>(base);
    canvas->grow = true;

    // Tools: one chosen at a time
    struct ToolDef {
        Tool tool;
        const char* glyph;
        const char* tip;
    };
    static const ToolDef tools[] = {{Tool::Pen, "draw", "Pen"},           {Tool::Arrow, "north_east", "Arrow"},
                                    {Tool::Box, "rectangle", "Box"},      {Tool::Highlight, "ink_highlighter", "Highlighter"},
                                    {Tool::Text, "title", "Text"},        {Tool::Pixelate, "blur_on", "Hide (pixelate)"},
                                    {Tool::Crop, "crop", "Crop"}};
    auto toolButtons = std::make_shared<std::vector<std::pair<Tool, Button*>>>();
    auto paintTools = [toolButtons, canvas] {
        for (auto& [t, b] : *toolButtons) {
            const bool on = t == canvas->tool;
            b->setBackground(on ? ColorRef("secondary_container") : ColorRef::none());
            for (auto& c : b->children())
                if (auto* i = dynamic_cast<Icon*>(c.get())) i->setFill(on)->setColor(on ? "on_secondary_container" : "on_surface");
        }
    };
    for (const auto& d : tools) {
        auto* b = bar->add<Button>();
        pill(b)->setPadding(Insets(9));
        b->pressedRadius = 10;
        b->tooltip = d.tip;
        b->add<Icon>(d.glyph, 22);
        const Tool t = d.tool;
        b->onClicked = [canvas, t, paintTools, textRow](uint32_t button) {
            if (button != BTN_LEFT) return;
            canvas->tool = t;
            textRow->setVisible(false);
            paintTools();
        };
        toolButtons->push_back({t, b});
    }
    paintTools();

    // Colours, then sizes
    auto* sep = bar->add<Flex>(Flex::Direction::Row);
    sep->fixedWidth = 1;
    sep->fixedHeight = 28;
    sep->setBackground("outline_variant", false);
    static const char* colours[] = {"#f44336", "#ff9800", "#ffeb3b", "#4caf50", "#2196f3", "#ffffff", "#000000"};
    auto swatches = std::make_shared<std::vector<std::pair<Color, Button*>>>();
    auto paintSwatches = [swatches, canvas] {
        for (auto& [c, b] : *swatches) {
            const bool on = c.r == canvas->colour.r && c.g == canvas->colour.g && c.b == canvas->colour.b;
            b->setBorder(on ? 3 : 0, "on_surface");
        }
    };
    for (const char* hex : colours) {
        const Color c = Color::fromHex(hex).value_or(Color{1, 0, 0, 1});
        auto* b = bar->add<Button>();
        pill(b)->setPadding(Insets(4));
        auto* dot = b->add<Flex>(Flex::Direction::Row);
        pill(dot->setBackground(ColorRef(c), false));
        dot->setBorder(1, ColorRef("outline", 0.6f));
        dot->fixedWidth = dot->fixedHeight = 22;
        b->onClicked = [canvas, c, paintSwatches](uint32_t button) {
            if (button != BTN_LEFT) return;
            canvas->colour = c;
            paintSwatches();
        };
        swatches->push_back({c, b});
    }
    paintSwatches();
    auto sizes = std::make_shared<std::vector<Button*>>();
    auto paintSizes = [sizes, canvas] {
        for (size_t i = 0; i < sizes->size(); ++i)
            (*sizes)[i]->setBackground(static_cast<int>(i) == canvas->size ? ColorRef("secondary_container") : ColorRef::none());
    };
    for (int i = 0; i < 3; ++i) {
        auto* b = bar->add<Button>();
        pill(b)->setPadding(Insets(8))->setJustify(Align::Center);
        b->fixedWidth = b->fixedHeight = 38;
        b->tooltip = i == 0 ? "Thin" : i == 1 ? "Medium" : "Thick";
        auto* dot = b->add<Flex>(Flex::Direction::Row);
        pill(dot->setBackground("on_surface", false));
        dot->fixedWidth = dot->fixedHeight = 6.0f + 5.0f * static_cast<float>(i);
        b->onClicked = [canvas, i, paintSizes](uint32_t button) {
            if (button != BTN_LEFT) return;
            canvas->size = i;
            paintSizes();
        };
        sizes->push_back(b);
    }
    paintSizes();

    auto* gap = bar->add<Node>();
    gap->grow = true;
    kit::iconButton(bar, "undo", [canvas] { canvas->undo(); }, 22)->tooltip = "Undo (Ctrl+Z)";
    kit::iconButton(bar, "redo", [canvas] { canvas->redo(); }, 22)->tooltip = "Redo (Ctrl+Shift+Z)";
    // Copy, or save (over the screenshot; copied as well when screenshots are); then done
    auto copy = [canvas, &popup] {
        const std::string tmp = paths::runtimeDir() + "/edited.png";
        std::error_code ec;
        fs::create_directories(paths::runtimeDir(), ec);
        if (!canvas->save(tmp)) return;
        m3::run({"sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", tmp}, nullptr);
        m3::run({"notify-send", "-a", "Screenshot", "Copied to the clipboard"}, nullptr);
        popup.close();
    };
    auto save = [canvas, path, &popup] {
        const std::string out = savePath(path);
        if (!canvas->save(out)) {
            m3::run({"notify-send", "-a", "Screenshot", "Couldn't save", out}, nullptr);
            return;
        }
        if (Config::get().value<bool>("/screenshot/copy", true))
            m3::run({"sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", out}, nullptr);
        m3::run({"notify-send", "-a", "Screenshot", "-i", out, "Saved", fs::path(out).filename().string()}, nullptr);
        popup.close();
    };
    pillButton(bar, "Copy", false, copy, "content_copy");
    pillButton(bar, "Save", true, save, "check");
    kit::iconButton(bar, "close", [&popup] { popup.close(); }, 22)->tooltip = "Close without saving (Esc)";

    // The text tool: a field where it was clicked; Enter puts the text there
    auto* field = textRow->add<TextField>();
    field->setPlaceholder("Type the text, then Enter");
    field->grow = true;
    auto at = std::make_shared<Pt>();
    canvas->onTextAt = [textRow, field, at](Pt p) {
        *at = p;
        textRow->setVisible(true);
        field->setText("");
        field->focus();
    };
    field->onSubmit = [canvas, textRow, field, at](const std::string& t) {
        canvas->addText(*at, t);
        field->setText("");
        textRow->setVisible(false);
    };
    field->onCancel = [textRow] { textRow->setVisible(false); };

    // Keys: undo, redo, copy, save (Esc closes, as any panel)
    popup.onKey = [canvas, copy, save](const KeyEvent& e) {
        if (!e.ctrl()) return false;
        if (e.baseSym == XKB_KEY_z && e.shift()) canvas->redo();
        else if (e.baseSym == XKB_KEY_z) canvas->undo();
        else if (e.baseSym == XKB_KEY_y) canvas->redo();
        else if (e.baseSym == XKB_KEY_c) copy();
        else if (e.baseSym == XKB_KEY_s) save();
        else return false;
        return true;
    };
}

} // namespace m3::shell::panels
