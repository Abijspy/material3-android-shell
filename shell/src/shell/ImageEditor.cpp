// The shell's image editor: a screenshot or any picture (every format the
// viewer opens) to draw on, mark up, adjust and transform, then copy or
// save in the format you choose.
//
// What's drawn (pen, line, arrow, box, ellipse, highlighter, text, numbered
// steps, pixelate, blur) sits over the picture as shapes until it's saved;
// rotating, flipping, resizing and applying a crop bake them into the
// picture. Adjustments (light, colour, detail, a vignette) and filters are
// worked out from the picture as it is. Each change is a step to undo:
// the whole state (picture, shapes, adjustments) is kept for each, the
// pictures shared until a change makes a new one. Drawn with Cairo into the
// picture itself, so what's shown is what's saved.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <array>
#include <cairo.h>
#include <cmath>
#include <deque>
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

enum class Tool { Pen, Line, Arrow, Box, Ellipse, Highlight, Text, Number, Pixelate, Blur, Crop };

struct Pt {
    float x = 0, y = 0;
};

struct Shape {
    Tool tool = Tool::Pen;
    Color colour{1, 0, 0, 1};
    float width = 4;
    std::vector<Pt> pts; // the pen's path; else its two corners (or the text's, the number's place)
    std::string text;    // the text tool's; the number tool's number
    bool filled = false; // boxes and ellipses
};

// Light, colour and detail, as the Adjust panel sets them (0 each: as it is)
struct Adjust {
    float exposure = 0, brightness = 0, contrast = 0, saturation = 0, warmth = 0, vignette = 0, blur = 0, sharpen = 0;
    std::string filter = "none"; // mono, noir, sepia, vivid, warm, cool, fade, invert
    bool operator==(const Adjust&) const = default;
    bool neutral() const { return *this == Adjust{}; }
};

using Surface = std::shared_ptr<cairo_surface_t>;
Surface wrap(cairo_surface_t* s) {
    return Surface(s, [](cairo_surface_t* p) {
        if (p) cairo_surface_destroy(p);
    });
}
Surface blank(int w, int h) {
    return wrap(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, w), std::max(1, h)));
}
int widthOf(const Surface& s) {
    return cairo_image_surface_get_width(s.get());
}
int heightOf(const Surface& s) {
    return cairo_image_surface_get_height(s.get());
}
Surface copyOf(const Surface& s) {
    auto out = blank(widthOf(s), heightOf(s));
    cairo_t* cr = cairo_create(out.get());
    cairo_set_source_surface(cr, s.get(), 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    return out;
}

// 8 bits a channel, premultiplied ARGB: what the pixel tools read.
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
// gdk-pixbuf (turned as the camera held it, premultiplied into Cairo's layout)
cairo_surface_t* load(const std::string& path) {
    if (path.ends_with(".png") || path.ends_with(".PNG")) {
        cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
        if (cairo_surface_status(s) == CAIRO_STATUS_SUCCESS) return argb32(s);
        cairo_surface_destroy(s);
    }
    GError* err = nullptr;
    GdkPixbuf* loaded = gdk_pixbuf_new_from_file(path.c_str(), &err);
    if (!loaded) {
        if (err) g_error_free(err);
        return nullptr;
    }
    GdkPixbuf* pb = gdk_pixbuf_apply_embedded_orientation(loaded);
    g_object_unref(loaded);
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

// ── Pixels ──

// A box blur over premultiplied ARGB, three passes each way (as good as a
// Gaussian to the eye), in a rectangle of the surface
void boxBlur(cairo_surface_t* s, int radius, int rx, int ry, int rw, int rh) {
    if (radius < 1) return;
    cairo_surface_flush(s);
    const int W = cairo_image_surface_get_width(s), H = cairo_image_surface_get_height(s), stride = cairo_image_surface_get_stride(s);
    rx = std::clamp(rx, 0, W), ry = std::clamp(ry, 0, H);
    rw = std::clamp(rw, 0, W - rx), rh = std::clamp(rh, 0, H - ry);
    if (rw < 2 || rh < 2) return;
    unsigned char* data = cairo_image_surface_get_data(s);
    std::vector<uint32_t> line(static_cast<size_t>(std::max(rw, rh))), out(line.size());
    auto pass = [&](int n, auto get, auto set) {
        // One run of `n` pixels, averaged over a window 2r+1 wide (edges repeated)
        for (int k = 0; k < 3; ++k) {
            for (int i = 0; i < n; ++i) line[i] = get(i);
            uint32_t sum[4] = {0, 0, 0, 0};
            auto add = [&](uint32_t v, int sign) {
                for (int c = 0; c < 4; ++c) sum[c] = static_cast<uint32_t>(static_cast<int>(sum[c]) + sign * static_cast<int>((v >> (c * 8)) & 255));
            };
            for (int i = -radius; i <= radius; ++i) add(line[std::clamp(i, 0, n - 1)], 1);
            const uint32_t div = static_cast<uint32_t>(2 * radius + 1);
            for (int i = 0; i < n; ++i) {
                out[i] = (sum[0] / div) | ((sum[1] / div) << 8) | ((sum[2] / div) << 16) | ((sum[3] / div) << 24);
                add(line[std::clamp(i + radius + 1, 0, n - 1)], 1);
                add(line[std::clamp(i - radius, 0, n - 1)], -1);
            }
            for (int i = 0; i < n; ++i) set(i, out[i]);
        }
    };
    auto px = [&](int x, int y) -> uint32_t& { return *reinterpret_cast<uint32_t*>(data + static_cast<ptrdiff_t>(y) * stride + x * 4); };
    for (int y = ry; y < ry + rh; ++y) pass(rw, [&](int i) { return px(rx + i, y); }, [&](int i, uint32_t v) { px(rx + i, y) = v; });
    for (int x = rx; x < rx + rw; ++x) pass(rh, [&](int i) { return px(x, ry + i); }, [&](int i, uint32_t v) { px(x, ry + i) = v; });
    cairo_surface_mark_dirty(s);
}

// The picture with the Adjust panel's changes: tone through a table for
// each channel, then colour (saturation, the filter), a vignette, blur,
// sharpening (an unsharp mask). Works on straight colour: what's
// see-through stays as see-through
Surface adjusted(const Surface& src, Adjust a) {
    // Filters as settings, and as a colour mode
    int mode = 0; // 1 mono, 2 sepia, 3 invert
    if (a.filter == "mono") mode = 1;
    else if (a.filter == "noir") mode = 1, a.contrast += 0.35f;
    else if (a.filter == "sepia") mode = 2;
    else if (a.filter == "invert") mode = 3;
    else if (a.filter == "vivid") a.saturation += 0.45f, a.contrast += 0.12f;
    else if (a.filter == "warm") a.warmth += 0.45f, a.saturation += 0.1f;
    else if (a.filter == "cool") a.warmth -= 0.45f;
    const bool fade = a.filter == "fade";
    if (fade) a.saturation -= 0.25f, a.contrast -= 0.15f;

    auto out = copyOf(src);
    cairo_surface_flush(out.get());
    const int W = widthOf(out), H = heightOf(out), stride = cairo_image_surface_get_stride(out.get());
    unsigned char* data = cairo_image_surface_get_data(out.get());
    // Tone: exposure (stops), brightness, contrast, warmth (red up, blue down)
    std::array<std::array<uint8_t, 256>, 3> lut{};
    const double gain = std::pow(2.0, a.exposure);
    for (int c = 0; c < 3; ++c)
        for (int v = 0; v < 256; ++v) {
            double x = v * gain + a.brightness * 96.0;
            x = (x - 128.0) * (1.0 + a.contrast) + 128.0;
            if (c == 0) x *= 1.0 + 0.18 * a.warmth;
            if (c == 2) x *= 1.0 - 0.18 * a.warmth;
            if (fade) x = 28.0 + x * 0.86;
            lut[static_cast<size_t>(c)][static_cast<size_t>(v)] = static_cast<uint8_t>(std::clamp(std::lround(x), 0L, 255L));
        }
    const float cx = static_cast<float>(W) / 2, cy = static_cast<float>(H) / 2;
    for (int y = 0; y < H; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<ptrdiff_t>(y) * stride);
        const float dy = (static_cast<float>(y) - cy) / cy;
        for (int x = 0; x < W; ++x) {
            const uint32_t v = row[x];
            const unsigned al = v >> 24;
            if (al == 0) continue;
            // Straight colour, then back
            float r = static_cast<float>(lut[0][((v >> 16) & 255) * 255 / al]), g = static_cast<float>(lut[1][((v >> 8) & 255) * 255 / al]),
                  b = static_cast<float>(lut[2][(v & 255) * 255 / al]);
            if (a.saturation != 0 || mode == 1 || mode == 2) {
                const float l = 0.299f * r + 0.587f * g + 0.114f * b;
                if (mode == 1) r = g = b = l;
                else if (mode == 2) r = l * 1.07f + 18, g = l * 0.88f + 10, b = l * 0.68f;
                else r = l + (r - l) * (1 + a.saturation), g = l + (g - l) * (1 + a.saturation), b = l + (b - l) * (1 + a.saturation);
            }
            if (mode == 3) r = 255 - r, g = 255 - g, b = 255 - b;
            if (a.vignette > 0) {
                const float dx = (static_cast<float>(x) - cx) / cx;
                const float d = std::clamp((std::sqrt(dx * dx + dy * dy) - 0.45f) / 0.95f, 0.0f, 1.0f);
                const float f = 1 - a.vignette * d * d * 1.3f;
                r *= f, g *= f, b *= f;
            }
            const auto put = [al](float c) { return static_cast<uint32_t>(std::clamp(c, 0.0f, 255.0f) * static_cast<float>(al) / 255.0f + 0.5f); };
            row[x] = (al << 24) | (put(r) << 16) | (put(g) << 8) | put(b);
        }
    }
    cairo_surface_mark_dirty(out.get());
    const int unit = std::max(1, std::max(W, H) / 400);
    if (a.blur > 0) boxBlur(out.get(), std::max(1, static_cast<int>(std::lround(a.blur * 8 * unit))), 0, 0, W, H);
    if (a.sharpen > 0) {
        // The picture plus how much it differs from a blur of itself
        auto soft = copyOf(out);
        boxBlur(soft.get(), std::max(1, unit), 0, 0, W, H);
        cairo_surface_flush(out.get());
        const unsigned char* sd = cairo_image_surface_get_data(soft.get());
        const float k = a.sharpen * 1.6f;
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(data + static_cast<ptrdiff_t>(y) * stride);
            const auto* srow = reinterpret_cast<const uint32_t*>(sd + static_cast<ptrdiff_t>(y) * stride);
            for (int x = 0; x < W; ++x) {
                const uint32_t v = row[x], s = srow[x], al = v >> 24;
                uint32_t o = al << 24;
                for (int c = 0; c < 3; ++c) {
                    const float cv = static_cast<float>((v >> (c * 8)) & 255), sv = static_cast<float>((s >> (c * 8)) & 255);
                    o |= static_cast<uint32_t>(std::clamp(cv + k * (cv - sv), 0.0f, static_cast<float>(al))) << (c * 8);
                }
                row[x] = o;
            }
        }
        cairo_surface_mark_dirty(out.get());
    }
    return out;
}

// Turned a quarter (clockwise, or not), mirrored (left to right, or top to
// bottom), or scaled
Surface rotated(const Surface& s, bool clockwise) {
    const int w = widthOf(s), h = heightOf(s);
    auto out = blank(h, w);
    cairo_t* cr = cairo_create(out.get());
    if (clockwise) cairo_translate(cr, h, 0), cairo_rotate(cr, M_PI / 2);
    else cairo_translate(cr, 0, w), cairo_rotate(cr, -M_PI / 2);
    cairo_set_source_surface(cr, s.get(), 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return out;
}
Surface flipped(const Surface& s, bool horizontal) {
    const int w = widthOf(s), h = heightOf(s);
    auto out = blank(w, h);
    cairo_t* cr = cairo_create(out.get());
    if (horizontal) cairo_translate(cr, w, 0), cairo_scale(cr, -1, 1);
    else cairo_translate(cr, 0, h), cairo_scale(cr, 1, -1);
    cairo_set_source_surface(cr, s.get(), 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return out;
}
Surface scaled(const Surface& s, int w, int h) {
    auto out = blank(w, h);
    cairo_t* cr = cairo_create(out.get());
    cairo_scale(cr, static_cast<double>(w) / widthOf(s), static_cast<double>(h) / heightOf(s));
    cairo_set_source_surface(cr, s.get(), 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_destroy(cr);
    return out;
}

RectF corners(const Shape& s) {
    const Pt a = s.pts.front(), b = s.pts.back();
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

void setColour(cairo_t* cr, const Color& c, float alpha = 1) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a * alpha);
}

float lightness(const Color& c) {
    return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
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
        case Tool::Line:
            setColour(cr, s->colour);
            cairo_move_to(cr, s->pts.front().x, s->pts.front().y);
            cairo_line_to(cr, s->pts.back().x, s->pts.back().y);
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
        case Tool::Box:
        case Tool::Ellipse: {
            const RectF r = corners(*s);
            setColour(cr, s->colour);
            if (s->tool == Tool::Box) {
                cairo_rectangle(cr, r.x, r.y, r.w, r.h);
            } else if (r.w > 0.5f && r.h > 0.5f) {
                cairo_save(cr);
                cairo_translate(cr, r.x + r.w / 2, r.y + r.h / 2);
                cairo_scale(cr, r.w / 2, r.h / 2);
                cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
                cairo_restore(cr);
            }
            if (s->filled) cairo_fill(cr);
            else cairo_stroke(cr);
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
        case Tool::Blur: {
            // What's under it, blurred past reading (a softer hide than pixelate)
            const RectF r = corners(*s);
            const int x0 = static_cast<int>(r.x), y0 = static_cast<int>(r.y), w = static_cast<int>(std::ceil(r.w)), h = static_cast<int>(std::ceil(r.h));
            if (w < 2 || h < 2) break;
            auto patch = blank(w, h);
            cairo_t* pc = cairo_create(patch.get());
            cairo_set_source_surface(pc, base, -x0, -y0);
            cairo_paint(pc);
            cairo_destroy(pc);
            boxBlur(patch.get(), std::max(6, std::max(iw, ih) / 90), 0, 0, w, h);
            cairo_set_source_surface(cr, patch.get(), x0, y0);
            cairo_rectangle(cr, x0, y0, w, h);
            cairo_fill(cr);
            break;
        }
        case Tool::Number: {
            // A numbered step: a filled circle, the number on it in the opposite lightness
            const Pt c = s->pts.front();
            const double radius = std::max(12.0, s->width * 4.2);
            setColour(cr, s->colour);
            cairo_arc(cr, c.x, c.y, radius, 0, 2 * M_PI);
            cairo_fill(cr);
            PangoLayout* layout = pango_cairo_create_layout(cr);
            PangoFontDescription* font = pango_font_description_from_string("Sans Bold");
            pango_font_description_set_absolute_size(font, radius * 1.15 * PANGO_SCALE);
            pango_layout_set_font_description(layout, font);
            pango_layout_set_text(layout, s->text.c_str(), -1);
            int tw = 0, th = 0;
            pango_layout_get_pixel_size(layout, &tw, &th);
            const float l = lightness(s->colour) > 0.55f ? 0 : 1;
            cairo_set_source_rgb(cr, l, l, l);
            cairo_move_to(cr, c.x - tw / 2.0, c.y - th / 2.0);
            pango_cairo_show_layout(cr, layout);
            pango_font_description_free(font);
            g_object_unref(layout);
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
            const float light = lightness(s->colour);
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

// What's being edited, as one step of the history
struct State {
    Surface base;              // the picture, with what's been baked into it
    std::vector<Shape> shapes; // over it
    Adjust adjust;
};

// The picture with what's drawn on it, the crop being made, the pen and the rest
class Canvas : public Node {
public:
    Tool tool = Tool::Arrow;
    Color colour{0.96f, 0.26f, 0.21f, 1};
    int size = 1;       // 0 thin, 1 medium, 2 thick
    bool fill = false;  // boxes and ellipses filled
    float aspect = 0;   // the crop's width ÷ height (0: free)
    std::function<void(Pt)> onTextAt;
    std::function<void()> onChange; // any step: the bars follow (undo, the crop, the size)

    explicit Canvas(Surface base) { m_state.base = std::move(base); }
    ~Canvas() override {
        if (m_tex) glDeleteTextures(1, &m_tex);
    }

    int imageWidth() const { return widthOf(m_state.base); }
    int imageHeight() const { return heightOf(m_state.base); }
    float width() const { return unit() * (size == 0 ? 1.0f : size == 1 ? 2.0f : 3.5f); }
    const Adjust& adjust() const { return m_live ? *m_live : m_state.adjust; }
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    bool edited() const { return !m_undo.empty(); }
    bool cropPending() const { return lastCrop(nullptr) != nullptr; }

    void addText(Pt at, const std::string& text) {
        if (text.empty()) return;
        Shape s{Tool::Text, colour, width(), {at}, text};
        step([&](State& st) { st.shapes.push_back(s); });
    }
    void undo() {
        if (m_undo.empty()) return;
        m_redo.push_back(m_state);
        m_state = m_undo.back();
        m_undo.pop_back();
        changed();
    }
    void redo() {
        if (m_redo.empty()) return;
        m_undo.push_back(m_state);
        m_state = m_redo.back();
        m_redo.pop_back();
        changed();
    }

    // The Adjust panel: shown as it's dragged, a step once it's let go
    void previewAdjust(const Adjust& a) {
        m_live = std::make_unique<Adjust>(a);
        changed();
    }
    void commitAdjust(const Adjust& a) {
        m_live.reset();
        if (a == m_state.adjust) return changed();
        step([&](State& st) { st.adjust = a; });
    }

    // Changes to the picture itself: what's drawn and adjusted is baked in first
    void rotate(bool clockwise) {
        bakeThen([clockwise](const Surface& s) { return rotated(s, clockwise); });
    }
    void flip(bool horizontal) {
        bakeThen([horizontal](const Surface& s) { return flipped(s, horizontal); });
    }
    void resize(int w, int h) {
        if (w < 1 || h < 1 || (w == imageWidth() && h == imageHeight())) return;
        bakeThen([w, h](const Surface& s) { return scaled(s, w, h); });
    }
    void applyCrop() {
        const Shape* c = lastCrop(nullptr);
        if (!c) return;
        const RectF r = corners(*c);
        const int cx = static_cast<int>(std::floor(r.x)), cy = static_cast<int>(std::floor(r.y));
        const int cw = std::max(1, static_cast<int>(std::ceil(r.w))), ch = std::max(1, static_cast<int>(std::ceil(r.h)));
        bakeThen([cx, cy, cw, ch](const Surface& s) {
            auto out = blank(cw, ch);
            cairo_t* cr = cairo_create(out.get());
            cairo_set_source_surface(cr, s.get(), -cx, -cy);
            cairo_paint(cr);
            cairo_destroy(cr);
            return out;
        });
    }
    void clearCrop() {
        if (!cropPending()) return;
        step([](State& st) { std::erase_if(st.shapes, [](const Shape& s) { return s.tool == Tool::Crop; }); });
    }

    // What's saved: the picture adjusted, what's drawn on it, cut to the crop
    Surface finished() const {
        auto base = m_state.adjust.neutral() ? m_state.base : adjusted(m_state.base, m_state.adjust);
        RectF c{0, 0, static_cast<float>(imageWidth()), static_cast<float>(imageHeight())};
        if (const Shape* crop = lastCrop(nullptr)) c = corners(*crop);
        const int cx = static_cast<int>(std::floor(c.x)), cy = static_cast<int>(std::floor(c.y));
        const int cw = std::max(1, static_cast<int>(std::ceil(c.w))), ch = std::max(1, static_cast<int>(std::ceil(c.h)));
        auto out = blank(cw, ch);
        cairo_t* cr = cairo_create(out.get());
        cairo_translate(cr, -cx, -cy);
        paintShapes(cr, base.get(), drawn(nullptr));
        cairo_destroy(cr);
        return out;
    }

    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float y) override {
        if (button != BTN_LEFT) return;
        const Pt p = toImage(x, y);
        if (tool == Tool::Text) {
            if (onTextAt) onTextAt(p);
            return;
        }
        if (tool == Tool::Number) {
            int n = 1;
            for (const auto& s : m_state.shapes)
                if (s.tool == Tool::Number) ++n;
            Shape s{Tool::Number, colour, width(), {p}, std::to_string(n)};
            step([&](State& st) { st.shapes.push_back(s); });
            return;
        }
        m_drawing = std::make_unique<Shape>(Shape{tool, colour, width(), {p, p}, {}, fill});
        if (tool == Tool::Pen) m_drawing->pts.resize(1);
        m_dirty = true;
        requestRedraw();
    }
    void onDrag(float x, float y) override {
        if (!m_drawing) return;
        Pt p = toImage(x, y);
        if (m_drawing->tool == Tool::Pen) {
            const Pt last = m_drawing->pts.back();
            if (std::hypot(p.x - last.x, p.y - last.y) < 1.5f) return;
            m_drawing->pts.push_back(p);
        } else {
            // The crop held to its shape, from where it began
            if (m_drawing->tool == Tool::Crop && aspect > 0) {
                const Pt a = m_drawing->pts.front();
                float w = std::abs(p.x - a.x), h = std::abs(p.y - a.y);
                if (w / std::max(h, 1.0f) > aspect) w = h * aspect;
                else h = w / aspect;
                w = std::min(w, p.x >= a.x ? static_cast<float>(imageWidth()) - a.x : a.x);
                h = std::min(h, w / aspect);
                w = h * aspect;
                p = {a.x + (p.x >= a.x ? w : -w), a.y + (p.y >= a.y ? h : -h)};
            }
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
        if (!tiny)
            step([&](State& st) {
                if (s->tool == Tool::Crop) std::erase_if(st.shapes, [](const Shape& x) { return x.tool == Tool::Crop; }); // one crop at a time
                st.shapes.push_back(*s);
            });
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
        // A crop being made or made: outside it dimmed, its edge and thirds marked
        if (const Shape* c = lastCrop(m_drawing.get())) {
            const RectF ci = corners(*c);
            const float k = f.w / static_cast<float>(imageWidth());
            const RectF crop{f.x + ci.x * k, f.y + ci.y * k, ci.w * k, ci.h * k};
            const Color dim{0, 0, 0, 0.55f};
            r.rect({f.x, f.y, f.w, crop.y - f.y}, dim);
            r.rect({f.x, crop.bottom(), f.w, f.bottom() - crop.bottom()}, dim);
            r.rect({f.x, crop.y, crop.x - f.x, crop.h}, dim);
            r.rect({crop.right(), crop.y, f.right() - crop.right(), crop.h}, dim);
            const Color third{1, 1, 1, 0.35f};
            for (int i = 1; i < 3; ++i) {
                r.rect({crop.x + crop.w * static_cast<float>(i) / 3, crop.y, 1, crop.h}, third);
                r.rect({crop.x, crop.y + crop.h * static_cast<float>(i) / 3, crop.w, 1}, third);
            }
            r.rect(crop, Color{0, 0, 0, 0}, Radii(0), 2, Color{1, 1, 1, 0.95f});
        }
    }

private:
    float unit() const { return std::max(2.0f, static_cast<float>(std::max(imageWidth(), imageHeight())) / 640.0f); }

    // A change as a step: the state before it kept to undo
    void step(const std::function<void(State&)>& change) {
        m_undo.push_back(m_state);
        while (m_undo.size() > 40) m_undo.pop_front();
        m_redo.clear();
        change(m_state);
        changed();
    }
    void bakeThen(const std::function<Surface(const Surface&)>& op) {
        // What's drawn (not the crop) and the adjustments, into the picture
        auto base = m_state.adjust.neutral() ? m_state.base : adjusted(m_state.base, m_state.adjust);
        std::vector<const Shape*> shapes = drawn(nullptr);
        if (!shapes.empty()) {
            auto flat = blank(widthOf(base), heightOf(base));
            cairo_t* cr = cairo_create(flat.get());
            paintShapes(cr, base.get(), shapes);
            cairo_destroy(cr);
            base = flat;
        }
        auto next = op(base);
        step([&](State& st) {
            st.base = next;
            st.shapes.clear();
            st.adjust = {};
        });
    }
    void changed() {
        m_dirty = true;
        requestRedraw();
        if (onChange) onChange();
    }
    const Shape* lastCrop(const Shape* drawing) const {
        if (drawing && drawing->tool == Tool::Crop) return drawing;
        for (auto it = m_state.shapes.rbegin(); it != m_state.shapes.rend(); ++it)
            if (it->tool == Tool::Crop) return &*it;
        return nullptr;
    }
    std::vector<const Shape*> drawn(const Shape* drawing) const {
        std::vector<const Shape*> out;
        for (const auto& s : m_state.shapes)
            if (s.tool != Tool::Crop) out.push_back(&s);
        if (drawing && drawing->tool != Tool::Crop) out.push_back(drawing);
        return out;
    }
    // Fitted to the room, never larger than its own pixels
    RectF fitted() const {
        const RectF b = rect();
        const float iw = static_cast<float>(imageWidth()), ih = static_cast<float>(imageHeight());
        const float k = std::min({b.w / iw, b.h / ih, 1.0f / std::max(m_scale, 0.5f)});
        const float w = iw * k, h = ih * k;
        return {b.x + (b.w - w) / 2, b.y + (b.h - h) / 2, w, h};
    }
    Pt toImage(float x, float y) const {
        const RectF f = fitted();
        const float k = static_cast<float>(imageWidth()) / std::max(f.w, 1.0f);
        return {std::clamp((x - f.x) * k, 0.0f, static_cast<float>(imageWidth())), std::clamp((y - f.y) * k, 0.0f, static_cast<float>(imageHeight()))};
    }
    // The adjusted picture, worked out again only when it or the adjustments change
    cairo_surface_t* adjustedBase() {
        const Adjust& a = adjust();
        if (a.neutral()) return m_state.base.get();
        if (!m_adjusted || m_adjustedFrom != m_state.base.get() || m_adjustedWith != a) {
            m_adjusted = adjusted(m_state.base, a);
            m_adjustedFrom = m_state.base.get();
            m_adjustedWith = a;
        }
        return m_adjusted.get();
    }
    void render() {
        const int w = imageWidth(), h = imageHeight();
        if (!m_view || widthOf(m_view) != w || heightOf(m_view) != h) {
            m_view = blank(w, h);
            if (m_tex) glDeleteTextures(1, &m_tex);
            m_tex = 0;
        }
        cairo_t* cr = cairo_create(m_view.get());
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        paintShapes(cr, adjustedBase(), drawn(m_drawing.get()));
        cairo_destroy(cr);
        cairo_surface_flush(m_view.get());
        const unsigned char* data = cairo_image_surface_get_data(m_view.get());
        // Cairo's ARGB32 is the BGRA the image program takes
        if (!m_tex) {
            glGenTextures(1, &m_tex);
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
        m_dirty = false;
    }

    State m_state;
    std::deque<State> m_undo, m_redo;
    std::unique_ptr<Adjust> m_live; // the panel being dragged
    Surface m_view, m_adjusted;
    cairo_surface_t* m_adjustedFrom = nullptr;
    Adjust m_adjustedWith;
    float m_scale = 1;
    GLuint m_tex = 0;
    bool m_dirty = true;
    std::unique_ptr<Shape> m_drawing;
};

// ── Saving ──

std::string lowerExt(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

// gdk-pixbuf's name for a format it can write, by extension ("" when it can't)
std::string writer(const std::string& ext) {
    std::string want = ext == ".jpg" || ext == ".jpeg" ? "jpeg" : ext == ".tif" ? "tiff" : ext.empty() ? "" : ext.substr(1);
    GSList* formats = gdk_pixbuf_get_formats();
    std::string found;
    for (GSList* l = formats; l; l = l->next) {
        auto* f = static_cast<GdkPixbufFormat*>(l->data);
        gchar* name = gdk_pixbuf_format_get_name(f);
        if (want == name && gdk_pixbuf_format_is_writable(f)) found = name;
        g_free(name);
    }
    g_slist_free(formats);
    return found;
}

// The surface into `path` in the format its extension names: PNG by Cairo,
// what gdk-pixbuf writes (JPEG, WebP, AVIF, TIFF…) by it, the rest by ImageMagick
bool write(const Surface& s, const std::string& path, int quality = 92) {
    const std::string ext = lowerExt(path);
    if (ext == ".png") return cairo_surface_write_to_png(s.get(), path.c_str()) == CAIRO_STATUS_SUCCESS;
    const std::string type = writer(ext);
    if (type.empty()) {
        // Through a PNG, and ImageMagick
        const std::string tmp = path + ".m3tmp.png";
        if (cairo_surface_write_to_png(s.get(), tmp.c_str()) != CAIRO_STATUS_SUCCESS) return false;
        const int rc = std::system(std::format("magick '{}' -quality {} '{}' >/dev/null 2>&1", tmp, quality, path).c_str());
        std::error_code ec;
        fs::remove(tmp, ec);
        return rc == 0 && fs::exists(path, ec);
    }
    // Premultiplied BGRA → straight RGBA, alpha only where it's wanted
    cairo_surface_flush(s.get());
    const int w = widthOf(s), h = heightOf(s), stride = cairo_image_surface_get_stride(s.get());
    const unsigned char* data = cairo_image_surface_get_data(s.get());
    const bool alpha = type != "jpeg" && type != "bmp";
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, alpha, 8, w, h);
    guchar* px = gdk_pixbuf_get_pixels(pb);
    const int rs = gdk_pixbuf_get_rowstride(pb), n = alpha ? 4 : 3;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint32_t v = *reinterpret_cast<const uint32_t*>(data + static_cast<ptrdiff_t>(y) * stride + x * 4);
            const unsigned a = v >> 24;
            guchar* p = px + static_cast<ptrdiff_t>(y) * rs + x * n;
            // On white where it was see-through and the format has no alpha
            auto ch = [&](unsigned c) { return static_cast<guchar>(a ? (alpha ? c * 255 / a : c + (255 - a)) : (alpha ? 0 : 255)); };
            p[0] = ch((v >> 16) & 255), p[1] = ch((v >> 8) & 255), p[2] = ch(v & 255);
            if (alpha) p[3] = static_cast<guchar>(a);
        }
    const std::string q = std::to_string(quality);
    GError* err = nullptr;
    gboolean ok;
    if (type == "jpeg" || type == "webp" || type == "avif") ok = gdk_pixbuf_save(pb, path.c_str(), type.c_str(), &err, "quality", q.c_str(), nullptr);
    else ok = gdk_pixbuf_save(pb, path.c_str(), type.c_str(), &err, nullptr);
    if (err) {
        log::warn("image editor: {}", err->message);
        g_error_free(err);
    }
    g_object_unref(pb);
    return ok;
}

// Where an edited picture goes by default: a PNG (a screenshot) over
// itself, anything else beside it as a PNG
std::string savePath(const std::string& path) {
    if (lowerExt(path) == ".png") return path;
    const fs::path p(path);
    return (p.parent_path() / (p.stem().string() + "-edited.png")).string();
}

std::string copyPath(const std::string& path, const std::string& ext) {
    const fs::path p(path);
    return (p.parent_path() / fileops::freeName(p.parent_path().string(), p.stem().string() + "-edited" + ext)).string();
}

class EditorKeeper : public Node {
public:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EditorKeeper() { floating = true; }
    ~EditorKeeper() override { *alive = false; }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }
};

Flex* separator(Node* bar) {
    auto* sep = bar->add<Flex>(Flex::Direction::Row);
    sep->fixedWidth = 1;
    sep->fixedHeight = 28;
    sep->setBackground("outline_variant", false);
    return sep;
}

} // namespace

void imageEditor(Flex* card, Popup& popup, const std::string& path) {
    card->setSpacing(10);
    // A format gdk-pixbuf can't read: ImageMagick's PNG of it, made first
    std::string from = path;
    if (!path.empty() && fs::exists(path) && !fileops::nativePicture(path)) {
        auto* keeper = card->add<EditorKeeper>();
        bool failed = false;
        from = fileops::renderable(path, [card, &popup, path, alive = keeper->alive] {
            if (!*alive) return;
            card->clearChildren();
            imageEditor(card, popup, path);
        }, &failed);
        if (from.empty()) {
            auto* head = card->add<Flex>(Flex::Direction::Row);
            head->setSpacing(12);
            head->add<Label>(failed ? "Couldn't open " + fs::path(path).filename().string() : "Opening " + fs::path(path).filename().string() + "…")->setSize(16);
            pillButton(head, "Close", false, [&popup] { popup.close(); }, "close");
            return;
        }
    }
    cairo_surface_t* loaded = from.empty() ? nullptr : load(from);
    if (!loaded) {
        auto* head = card->add<Flex>(Flex::Direction::Row);
        head->setSpacing(12);
        head->add<Label>(path.empty() ? "No picture to edit" : "Couldn't open " + fs::path(path).filename().string())->setSize(16);
        pillButton(head, "Close", false, [&popup] { popup.close(); }, "close");
        return;
    }

    auto* bar = card->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(6)->setAlignItems(Align::Center);
    bar->wrap = true;
    auto* bar2 = card->add<Flex>(Flex::Direction::Row);
    bar2->setSpacing(6)->setAlignItems(Align::Center);
    bar2->wrap = true;
    // The text tool's field: where it was clicked, what to write
    auto* textRow = card->add<Flex>(Flex::Direction::Row);
    textRow->setSpacing(10);
    textRow->setVisible(false);
    auto* body = card->add<Flex>(Flex::Direction::Row);
    body->setSpacing(12)->setAlignItems(Align::Stretch);
    body->grow = true;
    auto* canvas = body->add<Canvas>(wrap(loaded));
    canvas->grow = true;

    // ── Tools: one chosen at a time ──
    struct ToolDef {
        Tool tool;
        const char* glyph;
        const char* tip;
    };
    static const ToolDef tools[] = {
        {Tool::Pen, "draw", "Pen"},
        {Tool::Line, "horizontal_rule", "Line"},
        {Tool::Arrow, "north_east", "Arrow"},
        {Tool::Box, "rectangle", "Box"},
        {Tool::Ellipse, "circle", "Ellipse"},
        {Tool::Highlight, "ink_highlighter", "Highlighter"},
        {Tool::Text, "title", "Text"},
        {Tool::Number, "counter_1", "Numbered steps"},
        {Tool::Pixelate, "blur_on", "Hide (pixelate)"},
        {Tool::Blur, "blur_medium", "Hide (blur)"},
        {Tool::Crop, "crop", "Crop"},
    };
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
            if (canvas->onChange) canvas->onChange();
        };
        toolButtons->push_back({t, b});
    }
    paintTools();

    // ── Colours, sizes, fill ──
    separator(bar);
    static const char* colours[] = {"#f44336", "#ff9800", "#ffeb3b", "#4caf50", "#00bcd4", "#2196f3", "#9c27b0", "#e91e63", "#ffffff", "#9e9e9e", "#000000"};
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
        pill(b)->setPadding(Insets(3));
        auto* dot = b->add<Flex>(Flex::Direction::Row);
        pill(dot->setBackground(ColorRef(c), false));
        dot->setBorder(1, ColorRef("outline", 0.6f));
        dot->fixedWidth = dot->fixedHeight = 20;
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
        b->fixedWidth = b->fixedHeight = 36;
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
    auto* fillButton = bar->add<Button>();
    pill(fillButton)->setPadding(Insets(9));
    fillButton->tooltip = "Fill boxes and ellipses";
    auto* fillIcon = fillButton->add<Icon>("format_color_fill", 22);
    fillButton->onClicked = [canvas, fillButton, fillIcon](uint32_t button) {
        if (button != BTN_LEFT) return;
        canvas->fill = !canvas->fill;
        fillButton->setBackground(canvas->fill ? ColorRef("secondary_container") : ColorRef::none());
        fillIcon->setFill(canvas->fill);
    };

    // ── Undo, adjust, copy, save ──
    bar->add<Node>()->grow = true;
    auto* undoButton = kit::iconButton(bar, "undo", [canvas] { canvas->undo(); }, 22);
    undoButton->tooltip = "Undo (Ctrl+Z)";
    auto* redoButton = kit::iconButton(bar, "redo", [canvas] { canvas->redo(); }, 22);
    redoButton->tooltip = "Redo (Ctrl+Shift+Z)";

    // ── The second bar: turn, mirror, size, crop ──
    kit::iconButton(bar2, "rotate_left", [canvas] { canvas->rotate(false); }, 22)->tooltip = "Rotate left (Ctrl+Shift+R)";
    kit::iconButton(bar2, "rotate_right", [canvas] { canvas->rotate(true); }, 22)->tooltip = "Rotate right (Ctrl+R)";
    kit::iconButton(bar2, "flip", [canvas] { canvas->flip(true); }, 22)->tooltip = "Flip left to right";
    auto* flipV = kit::iconButton(bar2, "flip", [canvas] { canvas->flip(false); }, 22);
    flipV->tooltip = "Flip top to bottom";
    separator(bar2);
    auto* sizeHost = bar2->add<Flex>(Flex::Direction::Row);
    sizeHost->setSpacing(6)->setAlignItems(Align::Center);
    separator(bar2);
    auto* aspectHost = bar2->add<Flex>(Flex::Direction::Row);
    aspectHost->setSpacing(6)->setAlignItems(Align::Center);
    auto* applyCrop = pillButton(bar2, "Apply crop", true, [canvas] { canvas->applyCrop(); }, "crop");
    auto* clearCrop = kit::iconButton(bar2, "close", [canvas] { canvas->clearCrop(); }, 20);
    clearCrop->tooltip = "No crop";
    bar2->add<Node>()->grow = true;
    auto* adjustToggle = pillButton(bar2, "Adjust", false, {}, "tune");

    // The size: the picture's own, and smaller (or a common width)
    std::function<void()> paintSize = [sizeHost, canvas] {
        sizeHost->clearChildren();
        const int w = canvas->imageWidth(), h = canvas->imageHeight();
        sizeHost->add<Icon>("photo_size_select_large", 20)->setColor("on_surface_variant", false);
        std::vector<std::pair<std::string, std::string>> items;
        for (int pct : {75, 50, 33, 25})
            items.push_back({std::to_string(pct), std::format("{}%  ·  {} × {}", pct, std::max(1, w * pct / 100), std::max(1, h * pct / 100))});
        for (int fit : {3840, 2560, 1920, 1280, 800})
            if (std::max(w, h) > fit) {
                const double k = static_cast<double>(fit) / std::max(w, h);
                items.push_back({"fit" + std::to_string(fit), std::format("Longest side {}  ·  {} × {}", fit, std::lround(w * k), std::lround(h * k))});
            }
        dropdown(sizeHost, sizeHost, std::format("{} × {}", w, h), items, [canvas](const std::string& v) {
            const int w0 = canvas->imageWidth(), h0 = canvas->imageHeight();
            if (v.starts_with("fit")) {
                const double k = std::stod(v.substr(3)) / std::max(w0, h0);
                canvas->resize(static_cast<int>(std::lround(w0 * k)), static_cast<int>(std::lround(h0 * k)));
            } else {
                const int pct = std::stoi(v);
                canvas->resize(std::max(1, w0 * pct / 100), std::max(1, h0 * pct / 100));
            }
        });
    };
    // The crop's shape
    static const std::vector<std::pair<std::string, std::string>> aspects = {
        {"0", "Free"}, {"1", "Square 1:1"}, {"1.3333", "4:3"}, {"1.5", "3:2"}, {"1.7778", "16:9"}, {"0.75", "3:4"}, {"0.5625", "9:16"}, {"original", "As the picture"}};
    std::function<void()> paintAspect = [aspectHost, canvas] {
        aspectHost->clearChildren();
        aspectHost->add<Icon>("aspect_ratio", 20)->setColor("on_surface_variant", false);
        std::string shown = "Free";
        for (const auto& [v, l] : aspects)
            if (v != "original" && std::abs(std::stof(v) - canvas->aspect) < 0.001f) shown = l;
        dropdown(aspectHost, aspectHost, shown, aspects, [canvas](const std::string& v) {
            canvas->aspect = v == "original" ? static_cast<float>(canvas->imageWidth()) / static_cast<float>(canvas->imageHeight()) : std::stof(v);
            canvas->tool = Tool::Crop;
            if (canvas->onChange) canvas->onChange();
        });
    };

    // ── Adjust: light, colour, detail, filters ──
    auto* adjustScroll = body->add<ScrollView>();
    adjustScroll->maxHeight = 1e6f;
    adjustScroll->fixedWidth = 300;
    adjustScroll->setVisible(false);
    auto* panel = adjustScroll->add<Flex>(Flex::Direction::Column);
    panel->setSpacing(8)->setAlignItems(Align::Stretch)->setPadding(Insets(16))->setRadius(Radii(24));
    panel->setBackground("surface_container_high", false);
    adjustToggle->onClicked = [adjustScroll, adjustToggle](uint32_t button) {
        if (button != BTN_LEFT) return;
        adjustScroll->setVisible(!adjustScroll->visible());
        adjustToggle->setBackground(adjustScroll->visible() ? "tertiary_container" : "secondary_container");
    };
    auto sliders = std::make_shared<std::vector<std::function<void()>>>(); // each follows the adjustments shown
    std::function<void()> buildPanel = [panel, canvas, sliders] {
        panel->clearChildren();
        sliders->clear();
        auto* head = panel->add<Flex>(Flex::Direction::Row);
        head->setAlignItems(Align::Center);
        auto* t = head->add<Label>("Adjust");
        t->setSize(18)->setWeight(600);
        t->grow = true;
        pillButton(head, "Reset", false, [canvas] { canvas->commitAdjust({}); }, "restart_alt");
        auto slider = [&](const char* icon, const char* name, float Adjust::*field, float min, float max) {
            auto* row = panel->add<Flex>(Flex::Direction::Row);
            row->setSpacing(8)->setAlignItems(Align::Center);
            row->add<Icon>(icon, 18)->setColor("on_surface_variant", false);
            auto* label = row->add<Label>(name);
            label->setSize(13)->setWeight(500);
            label->grow = true;
            auto* value = row->add<Label>("0");
            value->setSize(12)->setColor("on_surface_variant", false);
            auto* sl = panel->add<Slider>();
            sl->setRange(min, max)->setStep(0.01f / (max - min));
            const auto show = [value, sl, canvas, field](bool animate) {
                const float v = canvas->adjust().*field;
                sl->setValue(v, animate);
                value->setText(std::format("{:+.0f}", v * 100));
            };
            show(false);
            sliders->push_back([show] { show(true); });
            sl->onChanged = [canvas, field, value](float v) {
                Adjust a = canvas->adjust();
                a.*field = v;
                value->setText(std::format("{:+.0f}", v * 100));
                canvas->previewAdjust(a);
            };
            sl->onCommitted = [canvas, field](float v) {
                Adjust a = canvas->adjust();
                a.*field = v;
                canvas->commitAdjust(a);
            };
        };
        kit::caption(panel, "Light");
        slider("exposure", "Exposure", &Adjust::exposure, -2, 2);
        slider("brightness_6", "Brightness", &Adjust::brightness, -1, 1);
        slider("contrast", "Contrast", &Adjust::contrast, -1, 1);
        kit::caption(panel, "Colour");
        slider("palette", "Saturation", &Adjust::saturation, -1, 1);
        slider("thermostat", "Warmth", &Adjust::warmth, -1, 1);
        kit::caption(panel, "Detail");
        slider("deblur", "Sharpen", &Adjust::sharpen, 0, 1);
        slider("blur_on", "Blur", &Adjust::blur, 0, 1);
        slider("vignette", "Vignette", &Adjust::vignette, 0, 1);
        kit::caption(panel, "Filter");
        auto* chips = panel->add<kit::Flow>();
        chips->spacing = 6;
        static const std::pair<const char*, const char*> filters[] = {{"none", "None"}, {"vivid", "Vivid"}, {"warm", "Warm"}, {"cool", "Cool"},
                                                                      {"mono", "Mono"}, {"noir", "Noir"}, {"sepia", "Sepia"}, {"fade", "Fade"}, {"invert", "Invert"}};
        for (const auto& [id, name] : filters) {
            const bool on = canvas->adjust().filter == id;
            auto* c = chips->add<Button>();
            pill(c->setBackground(on ? "secondary_container" : "surface_container_highest", false))->setPadding({14, 7});
            c->add<Label>(name)->setSize(13)->setWeight(on ? 600 : 500)->setColor(on ? "on_secondary_container" : "on_surface", false);
            const std::string f = id;
            c->onClicked = [canvas, f](uint32_t button) {
                if (button != BTN_LEFT) return;
                Adjust a = canvas->adjust();
                a.filter = f;
                canvas->commitAdjust(a);
            };
        }
    };
    buildPanel();

    // Everything that shows the state follows each step
    canvas->onChange = [canvas, undoButton, redoButton, applyCrop, clearCrop, paintSize, paintAspect, buildPanel, sliders,
                        lastFilter = std::make_shared<std::string>("none"), lastSize = std::make_shared<std::pair<int, int>>()] {
        undoButton->setOpacity(canvas->canUndo() ? 1.0f : 0.35f);
        redoButton->setOpacity(canvas->canRedo() ? 1.0f : 0.35f);
        applyCrop->setVisible(canvas->cropPending());
        clearCrop->setVisible(canvas->cropPending());
        if (*lastSize != std::pair{canvas->imageWidth(), canvas->imageHeight()}) {
            *lastSize = {canvas->imageWidth(), canvas->imageHeight()};
            EventLoop::get().defer(paintSize);
        }
        EventLoop::get().defer(paintAspect);
        // The panel: rebuilt for another filter, its sliders moved for the rest
        if (canvas->adjust().filter != *lastFilter) {
            *lastFilter = canvas->adjust().filter;
            EventLoop::get().defer(buildPanel);
        } else {
            for (auto& s : *sliders) s();
        }
    };
    canvas->onChange();

    // Copy, or save (over a screenshot, beside anything else; or in the
    // format chosen); then done
    auto copy = [canvas, &popup] {
        const std::string tmp = paths::runtimeDir() + "/edited.png";
        std::error_code ec;
        fs::create_directories(paths::runtimeDir(), ec);
        if (!write(canvas->finished(), tmp)) return;
        m3::run({"sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", tmp}, nullptr);
        m3::run({"notify-send", "-a", "Image editor", "Copied to the clipboard"}, nullptr);
        popup.close();
    };
    auto saveTo = [canvas, &popup](const std::string& out) {
        if (!write(canvas->finished(), out)) {
            m3::run({"notify-send", "-a", "Image editor", "Couldn't save", out}, nullptr);
            return;
        }
        if (Config::get().value<bool>("/screenshot/copy", true) && lowerExt(out) == ".png")
            m3::run({"sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", out}, nullptr);
        m3::run({"notify-send", "-a", "Image editor", "-i", out, "Saved", fs::path(out).filename().string()}, nullptr);
        popup.close();
    };
    auto save = [saveTo, path] { saveTo(savePath(path)); };
    pillButton(bar, "Copy", false, copy, "content_copy");
    auto* saveButton = pillButton(bar, "Save", true, save, "check");
    // Save as: over the original (in its own format), or a copy in another
    auto* more = bar->add<Button>();
    pill(more->setBackground("primary", false))->setPadding(Insets(8));
    more->add<Icon>("expand_more", 20)->setColor("on_primary", false);
    more->tooltip = "Save as";
    more->onClicked = [more, saveTo, path](uint32_t button) {
        if (button != BTN_LEFT) return;
        std::vector<MenuItem> m;
        const std::string ext = lowerExt(path);
        const bool own = ext == ".png" || !writer(ext).empty() || haveCommand("magick");
        if (own && lowerExt(savePath(path)) != ext)
            m.emplace_back("save", "Save over the original (" + ext.substr(1) + ")", "", [saveTo, path] { saveTo(path); });
        for (const auto& [e, name] : std::vector<std::pair<std::string, std::string>>{
                 {".png", "PNG: lossless, keeps transparency"}, {".jpg", "JPEG: photos, smaller"}, {".webp", "WebP: smaller still"}, {".avif", "AVIF: smallest"}}) {
            if (e != ".png" && writer(e).empty() && !haveCommand("magick")) continue;
            m.emplace_back("file_copy", "Save a copy as " + name, "", [saveTo, path, e] { saveTo(copyPath(path, e)); });
        }
        floatingMenu(more, m);
    };
    (void)saveButton;
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

    // Keys: undo, redo, copy, save, turn (Esc closes, as any panel)
    popup.onKey = [canvas, copy, save](const KeyEvent& e) {
        if (e.sym == XKB_KEY_Return && canvas->cropPending()) return canvas->applyCrop(), true;
        if (!e.ctrl()) return false;
        if (e.baseSym == XKB_KEY_z && e.shift()) canvas->redo();
        else if (e.baseSym == XKB_KEY_z) canvas->undo();
        else if (e.baseSym == XKB_KEY_y) canvas->redo();
        else if (e.baseSym == XKB_KEY_c) copy();
        else if (e.baseSym == XKB_KEY_s) save();
        else if (e.baseSym == XKB_KEY_r) canvas->rotate(!e.shift());
        else return false;
        return true;
    };
}

} // namespace m3::shell::panels
