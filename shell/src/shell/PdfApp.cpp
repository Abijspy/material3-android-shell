// The PDF viewer and editor (material3ui-pdf), on poppler-glib.
//
// Viewing: the pages one after another, fitted to the width or zoomed; page
// thumbnails; search with every match marked; selecting text to copy; links
// (to pages and to the web); password-protected files; a night mode.
//
// Editing (the Edit bar): highlight, underline and strike out text; draw
// (pen), boxes, ellipses, lines and arrows; text boxes and sticky notes; a
// signature drawn once and placed anywhere; an eraser for annotations;
// filling in forms. Everything is a real PDF annotation that other readers
// show, undone one at a time, and saved over the file or as a copy. Pages:
// rotate, delete, move and extract (through qpdf where it's installed).
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/TextArea.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <cairo.h>
#include <poppler.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> pdfGo;

namespace {

constexpr float kPtToPx = 96.0f / 72.0f;

enum class Tool { Select, Highlight, Underline, Strike, Pen, Box, Ellipse, Line, Arrow, Text, Note, Sign, Eraser };

struct ToolDef {
    Tool tool;
    const char *icon, *name;
};
const ToolDef kTools[] = {
    {Tool::Select, "text_select_start", "Select text"}, {Tool::Highlight, "ink_highlighter", "Highlight"}, {Tool::Underline, "format_underlined", "Underline"},
    {Tool::Strike, "format_strikethrough", "Strike out"}, {Tool::Pen, "draw", "Pen"},                     {Tool::Box, "crop_square", "Box"},
    {Tool::Ellipse, "circle", "Ellipse"},                {Tool::Line, "horizontal_rule", "Line"},         {Tool::Arrow, "arrow_right_alt", "Arrow"},
    {Tool::Text, "title", "Text box"},                    {Tool::Note, "sticky_note_2", "Sticky note"},   {Tool::Sign, "signature", "Signature"},
    {Tool::Eraser, "ink_eraser", "Erase an annotation"},
};

const char* kInks[] = {"#ffd54f", "#ef5350", "#42a5f5", "#66bb6a", "#ab47bc", "#212121"};

struct Rgb {
    double r, g, b;
};
Rgb hexRgb(const char* h) {
    const Color c = *Color::fromHex(h);
    return {c.r, c.g, c.b};
}
PopplerColor popplerColor(const Rgb& c) {
    PopplerColor p;
    p.red = static_cast<guint16>(c.r * 65535), p.green = static_cast<guint16>(c.g * 65535), p.blue = static_cast<guint16>(c.b * 65535);
    return p;
}

struct Match {
    int page;
    PopplerRectangle rect; // page points, top-left origin
};

struct PageView;

struct State {
    PopplerDocument* doc = nullptr;
    std::string path, password;
    int pages = 0;
    std::vector<std::pair<double, double>> sizes; // points
    bool dirty = false, night = false, fitWidth = true, editing = false, thumbs = false;
    float zoom = 1;
    int current = 0;
    Tool tool = Tool::Select;
    int ink = 0;
    double width = 2;
    // Undo: the annotations added, newest last
    std::vector<std::pair<int, PopplerAnnot*>> added;
    // Search
    std::string query;
    std::vector<Match> matches;
    int match = -1;
    // Selection (Select tool and the text markups): page and rect in points
    int selPage = -1;
    PopplerRectangle sel{0, 0, 0, 0};
    // The signature: strokes in a 0–1 box
    std::vector<std::vector<std::pair<double, double>>> signature;
    // Text waiting for a click (text box, note)
    std::string pendingText;

    std::vector<PageView*> views, thumbViews;
    Flex *outlineCol = nullptr, *sideTabs = nullptr;
    bool showOutline = false;
    Flex *root = nullptr, *toolbar = nullptr, *editBar = nullptr, *searchBar = nullptr, *body = nullptr, *pagesCol = nullptr, *thumbCol = nullptr,
         *empty = nullptr, *textBar = nullptr;
    ScrollView *scroll = nullptr, *thumbScroll = nullptr;
    Label *pageLabel = nullptr, *zoomLabel = nullptr, *title = nullptr, *matchLabel = nullptr, *dirtyLabel = nullptr;
    TextField* searchField = nullptr;
    Popup* popup = nullptr;
    std::function<void()> rebuildPages, rebuildBars, refreshLabels;
    std::function<void()> buildBars; // rebuildBars runs it on the next turn: a button in the bar may be what asked
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId tick = 0;
    std::deque<PageView*> lru; // pages with a rendered texture, oldest first

    ~State() {
        if (doc) g_object_unref(doc);
    }
};

std::string uriOf(const std::string& path) {
    GError* e = nullptr;
    char* u = g_filename_to_uri(path.c_str(), nullptr, &e);
    if (e) g_error_free(e);
    std::string s = u ? u : "";
    g_free(u);
    return s;
}

void markDirty(const std::shared_ptr<State>& st) {
    st->dirty = true;
    if (st->refreshLabels) st->refreshLabels();
}

// ── A page ──

struct PageView : public Node {
    std::shared_ptr<State> st;
    int index;
    bool thumb;
    GLuint tex = 0;
    float texZoom = 0;
    bool texNight = false;
    bool stale = true;
    // A drag in progress (tool preview), page points
    std::vector<std::pair<double, double>> stroke;
    bool dragging = false;
    double ax = 0, ay = 0, bx = 0, by = 0;

    PageView(std::shared_ptr<State> s, int i, bool t) : st(std::move(s)), index(i), thumb(t) {}
    ~PageView() override { dropTexture(); }

    bool interactive() const override { return true; }

    float pxPerPt() const {
        if (thumb) return 120.0f / static_cast<float>(st->sizes[static_cast<size_t>(index)].first);
        return st->zoom * kPtToPx;
    }
    void dropTexture() {
        if (!tex) return;
        Renderer::get().makeCurrent();
        glDeleteTextures(1, &tex);
        tex = 0;
        std::erase(st->lru, this);
    }
    void invalidate() {
        stale = true;
        requestRedraw();
    }

    std::pair<double, double> toPage(float x, float y) const {
        const float k = pxPerPt();
        return {(x - rect().x) / k, (y - rect().y) / k};
    }

protected:
    SizeF doMeasure(SizeF available) override {
        const auto [w, h] = st->sizes[static_cast<size_t>(index)];
        if (!thumb && st->fitWidth && available.w > 100 && available.w < 1e5f) {
            // The widest page fills the width
            double widest = 0;
            for (const auto& s : st->sizes) widest = std::max(widest, s.first);
            st->zoom = std::clamp(static_cast<float>((available.w - 48) / (widest * kPtToPx)), 0.2f, 4.0f);
        }
        const float k = pxPerPt();
        return {static_cast<float>(w) * k, static_cast<float>(h) * k};
    }

    void doPaint(Renderer& r) override {
        const auto& b = rect();
        if (!r.visible(b)) return;
        r.rect({b.x + 1, b.y + 3, b.w, b.h}, Color{0, 0, 0, 0.18f}, Radii(thumb ? 4 : 6));
        const float scale = r.scale();
        const float z = pxPerPt() * scale;
        if (!tex || stale || std::abs(texZoom - z) > 0.01f || texNight != st->night) render(z);
        r.image(tex, b, 1, Radii(thumb ? 4 : 6));
        if (thumb) {
            if (index == st->current) r.rect(b, Color{0, 0, 0, 0}, Radii(4), 3, Theme::get().color("primary"));
            return;
        }
        const float k = pxPerPt();
        auto box = [&](const PopplerRectangle& p) { return RectF{b.x + static_cast<float>(p.x1) * k, b.y + static_cast<float>(p.y1) * k, static_cast<float>(p.x2 - p.x1) * k, static_cast<float>(p.y2 - p.y1) * k}; };
        // Search matches; the current one stronger
        for (size_t i = 0; i < st->matches.size(); ++i) {
            const auto& m = st->matches[i];
            if (m.page != index) continue;
            const bool here = static_cast<int>(i) == st->match;
            r.rect(box(m.rect), Theme::get().color(here ? "tertiary" : "primary").withAlpha(here ? 0.45f : 0.25f), Radii(2));
        }
        // The selection
        if (st->selPage == index && (st->tool == Tool::Select || st->tool == Tool::Highlight || st->tool == Tool::Underline || st->tool == Tool::Strike)) {
            PopplerPage* page = poppler_document_get_page(st->doc, index);
            PopplerRectangle s = st->sel;
            cairo_region_t* region = poppler_page_get_selected_region(page, 1.0, POPPLER_SELECTION_GLYPH, &s);
            const int n = cairo_region_num_rectangles(region);
            for (int i = 0; i < n; ++i) {
                cairo_rectangle_int_t rc;
                cairo_region_get_rectangle(region, i, &rc);
                r.rect({b.x + static_cast<float>(rc.x) * k, b.y + static_cast<float>(rc.y) * k, static_cast<float>(rc.width) * k, static_cast<float>(rc.height) * k},
                       Theme::get().color("primary").withAlpha(0.3f));
            }
            cairo_region_destroy(region);
            g_object_unref(page);
        }
        // What's being drawn
        if (dragging) {
            const Rgb c = hexRgb(kInks[st->ink]);
            const Color ink{static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), 1};
            const float w = std::max(2.0f, static_cast<float>(st->width) * k);
            auto dot = [&](double x, double y) { r.rect({b.x + static_cast<float>(x) * k - w / 2, b.y + static_cast<float>(y) * k - w / 2, w, w}, ink, Radii(w / 2)); };
            auto seg = [&](double x0, double y0, double x1, double y1) {
                const double len = std::hypot(x1 - x0, y1 - y0) * k;
                const int n = std::max(1, static_cast<int>(len / std::max(1.0f, w * 0.4f)));
                for (int i = 0; i <= n; ++i) dot(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
            };
            const RectF area{b.x + static_cast<float>(std::min(ax, bx)) * k, b.y + static_cast<float>(std::min(ay, by)) * k, static_cast<float>(std::abs(bx - ax)) * k,
                             static_cast<float>(std::abs(by - ay)) * k};
            switch (st->tool) {
            case Tool::Pen:
                for (size_t i = 1; i < stroke.size(); ++i) seg(stroke[i - 1].first, stroke[i - 1].second, stroke[i].first, stroke[i].second);
                break;
            case Tool::Box: r.rect(area, Color{0, 0, 0, 0}, Radii(0), w, ink); break;
            case Tool::Ellipse: r.rect(area, Color{0, 0, 0, 0}, Radii(std::min(area.w, area.h) / 2), w, ink); break;
            case Tool::Line:
            case Tool::Arrow: seg(ax, ay, bx, by); break;
            default: break;
            }
        }
    }

    void render(float z) {
        PopplerPage* page = poppler_document_get_page(st->doc, index);
        if (!page) return;
        const auto [pw, ph] = st->sizes[static_cast<size_t>(index)];
        const int w = std::max(1, static_cast<int>(std::lround(pw * z))), h = std::max(1, static_cast<int>(std::lround(ph * z)));
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surf);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_scale(cr, z, z);
        poppler_page_render(page, cr);
        if (st->night) {
            // Night: dark paper, light ink (pictures turned too, as readers do)
            cairo_identity_matrix(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
            cairo_set_source_rgb(cr, 0.92, 0.92, 0.92);
            cairo_paint(cr);
        }
        cairo_destroy(cr);
        cairo_surface_flush(surf);
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surf));
        cairo_surface_destroy(surf);
        g_object_unref(page);
        texZoom = z, texNight = st->night, stale = false;
        // A handful of pages keep their pictures; the oldest let go
        std::erase(st->lru, this);
        st->lru.push_back(this);
        while (st->lru.size() > (thumb ? 30u : 10u) + 30u) st->lru.front()->dropTexture();
    }

    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;
};

void rerender(const std::shared_ptr<State>& st, int page) {
    for (auto* v : st->views)
        if (v->index == page) v->invalidate();
    for (auto* v : st->thumbViews)
        if (v->index == page) v->invalidate();
}

void jumpTo(const std::shared_ptr<State>& st, int page, double yPt = 0) {
    if (page < 0 || page >= st->pages || page >= static_cast<int>(st->views.size()) || !st->scroll) return;
    PageView* v = st->views[static_cast<size_t>(page)];
    const float k = v->pxPerPt();
    const RectF rc = v->rect();
    const float y = rc.y + static_cast<float>(yPt) * k;
    st->scroll->reveal({rc.x, y - 30, rc.w, std::max(40.0f, st->scroll->rect().h - 60)});
    st->current = page;
    if (st->refreshLabels) st->refreshLabels();
}

void addAnnot(const std::shared_ptr<State>& st, int index, PopplerAnnot* annot) {
    if (!annot) return;
    PopplerPage* page = poppler_document_get_page(st->doc, index);
    poppler_page_add_annot(page, annot);
    g_object_unref(page);
    st->added.push_back({index, annot}); // our reference, for undo
    markDirty(st);
    rerender(st, index);
}

void undo(const std::shared_ptr<State>& st) {
    if (st->added.empty()) return;
    auto [index, annot] = st->added.back();
    st->added.pop_back();
    PopplerPage* page = poppler_document_get_page(st->doc, index);
    poppler_page_remove_annot(page, annot);
    g_object_unref(page);
    g_object_unref(annot);
    markDirty(st);
    rerender(st, index);
}

// Points top-left (as drawn) → the PDF's own (origin bottom-left)
PopplerRectangle pdfRect(const std::shared_ptr<State>& st, int page, double x1, double y1, double x2, double y2) {
    const double h = st->sizes[static_cast<size_t>(page)].second;
    PopplerRectangle r;
    r.x1 = std::min(x1, x2), r.x2 = std::max(x1, x2);
    r.y1 = h - std::max(y1, y2), r.y2 = h - std::min(y1, y2);
    return r;
}

void colourIt(const std::shared_ptr<State>& st, PopplerAnnot* a) {
    PopplerColor c = popplerColor(hexRgb(kInks[st->ink]));
    poppler_annot_set_color(a, &c);
}

// Highlight, underline or strike out the text selected on a page
void markText(const std::shared_ptr<State>& st, int index) {
    PopplerPage* page = poppler_document_get_page(st->doc, index);
    PopplerRectangle s = st->sel;
    cairo_region_t* region = poppler_page_get_selected_region(page, 1.0, POPPLER_SELECTION_GLYPH, &s);
    g_object_unref(page);
    const int n = cairo_region_num_rectangles(region);
    if (n == 0) {
        cairo_region_destroy(region);
        return;
    }
    const double h = st->sizes[static_cast<size_t>(index)].second;
    GArray* quads = g_array_new(FALSE, FALSE, sizeof(PopplerQuadrilateral));
    double x1 = 1e9, y1 = 1e9, x2 = -1e9, y2 = -1e9;
    for (int i = 0; i < n; ++i) {
        cairo_rectangle_int_t rc;
        cairo_region_get_rectangle(region, i, &rc);
        PopplerQuadrilateral q;
        q.p1 = {static_cast<double>(rc.x), h - rc.y};
        q.p2 = {static_cast<double>(rc.x + rc.width), h - rc.y};
        q.p3 = {static_cast<double>(rc.x), h - (rc.y + rc.height)};
        q.p4 = {static_cast<double>(rc.x + rc.width), h - (rc.y + rc.height)};
        g_array_append_val(quads, q);
        x1 = std::min(x1, static_cast<double>(rc.x)), y1 = std::min(y1, static_cast<double>(rc.y));
        x2 = std::max(x2, static_cast<double>(rc.x + rc.width)), y2 = std::max(y2, static_cast<double>(rc.y + rc.height));
    }
    cairo_region_destroy(region);
    PopplerRectangle bounds = pdfRect(st, index, x1, y1, x2, y2);
    PopplerAnnot* a = st->tool == Tool::Highlight   ? poppler_annot_text_markup_new_highlight(st->doc, &bounds, quads)
                      : st->tool == Tool::Underline ? poppler_annot_text_markup_new_underline(st->doc, &bounds, quads)
                                                    : poppler_annot_text_markup_new_strikeout(st->doc, &bounds, quads);
    g_array_unref(quads);
    colourIt(st, a);
    addAnnot(st, index, a);
    st->selPage = -1;
}

void inkAnnot(const std::shared_ptr<State>& st, int index, const std::vector<std::vector<std::pair<double, double>>>& strokes) {
    double x1 = 1e9, y1 = 1e9, x2 = -1e9, y2 = -1e9;
    for (const auto& s : strokes)
        for (const auto& [x, y] : s) x1 = std::min(x1, x), y1 = std::min(y1, y), x2 = std::max(x2, x), y2 = std::max(y2, y);
    if (x2 < x1) return;
    const double pad = st->width + 2;
    PopplerRectangle bounds = pdfRect(st, index, x1 - pad, y1 - pad, x2 + pad, y2 + pad);
    PopplerAnnot* a = poppler_annot_ink_new(st->doc, &bounds);
    const double h = st->sizes[static_cast<size_t>(index)].second;
    std::vector<PopplerPath*> paths;
    for (const auto& s : strokes) {
        std::vector<PopplerPoint> pts;
        for (const auto& [x, y] : s) pts.push_back({x, h - y});
        if (pts.size() == 1) pts.push_back({pts[0].x + 0.5, pts[0].y});
        // The path takes the array (transfer full): one of glib's own
        auto* owned = g_new(PopplerPoint, pts.size());
        std::copy(pts.begin(), pts.end(), owned);
        paths.push_back(poppler_path_new_from_array(owned, pts.size()));
    }
    colourIt(st, a);
    poppler_annot_set_border_width(a, st->width);
    // On its page first: poppler works the strokes out against it
    addAnnot(st, index, a);
    poppler_annot_ink_set_ink_list(POPPLER_ANNOT_INK(a), paths.data(), paths.size());
    for (auto* p : paths) poppler_path_free(p);
    rerender(st, index);
}

void textAnnot(const std::shared_ptr<State>& st, int index, double x, double y, bool note) {
    if (st->pendingText.empty()) return;
    if (note) {
        PopplerRectangle r = pdfRect(st, index, x, y, x + 20, y + 20);
        PopplerAnnot* a = poppler_annot_text_new(st->doc, &r);
        poppler_annot_set_contents(a, st->pendingText.c_str());
        poppler_annot_text_set_icon(POPPLER_ANNOT_TEXT(a), POPPLER_ANNOT_TEXT_ICON_COMMENT);
        colourIt(st, a);
        addAnnot(st, index, a);
    } else {
        // Sized to the text: about half a point-size per character, a line each
        const double size = 12;
        size_t longest = 0, lines = 1, run = 0;
        for (char c : st->pendingText) {
            if (c == '\n') longest = std::max(longest, run), run = 0, ++lines;
            else ++run;
        }
        longest = std::max(longest, run);
        PopplerRectangle r = pdfRect(st, index, x, y, x + std::max(40.0, static_cast<double>(longest) * size * 0.55 + 8), y + static_cast<double>(lines) * size * 1.3 + 6);
        PopplerAnnot* a = poppler_annot_free_text_new(st->doc, &r);
        poppler_annot_set_contents(a, st->pendingText.c_str());
        PopplerFontDescription* fd = poppler_font_description_new("Helvetica");
        fd->size_pt = size;
        poppler_annot_free_text_set_font_desc(POPPLER_ANNOT_FREE_TEXT(a), fd);
        poppler_font_description_free(fd);
        PopplerColor c = popplerColor(hexRgb(kInks[st->ink == 0 ? 5 : st->ink]));
        poppler_annot_free_text_set_font_color(POPPLER_ANNOT_FREE_TEXT(a), &c);
        addAnnot(st, index, a);
    }
    st->pendingText.clear();
    if (st->rebuildBars) st->rebuildBars();
}

void shapeAnnot(const std::shared_ptr<State>& st, int index, double ax, double ay, double bx, double by) {
    if (std::hypot(bx - ax, by - ay) < 3) return;
    const double pad = st->width + 6;
    PopplerRectangle r = pdfRect(st, index, std::min(ax, bx) - (st->tool >= Tool::Line ? pad : 0), std::min(ay, by) - (st->tool >= Tool::Line ? pad : 0),
                                 std::max(ax, bx) + (st->tool >= Tool::Line ? pad : 0), std::max(ay, by) + (st->tool >= Tool::Line ? pad : 0));
    PopplerAnnot* a = nullptr;
    if (st->tool == Tool::Box) a = poppler_annot_square_new(st->doc, &r);
    else if (st->tool == Tool::Ellipse) a = poppler_annot_circle_new(st->doc, &r);
    else if (st->tool == Tool::Line) {
        const double h = st->sizes[static_cast<size_t>(index)].second;
        PopplerPoint s{ax, h - ay}, e{bx, h - by};
        a = poppler_annot_line_new(st->doc, &r, &s, &e);
    } else if (st->tool == Tool::Arrow) {
        // A line, and the head as ink (every reader shows it)
        const double h = st->sizes[static_cast<size_t>(index)].second;
        PopplerPoint s{ax, h - ay}, e{bx, h - by};
        PopplerAnnot* line = poppler_annot_line_new(st->doc, &r, &s, &e);
        colourIt(st, line);
        poppler_annot_set_border_width(line, st->width);
        addAnnot(st, index, line);
        const double ang = std::atan2(by - ay, bx - ax), len = 8 + st->width * 3;
        inkAnnot(st, index, {{{bx - len * std::cos(ang - 0.45), by - len * std::sin(ang - 0.45)}, {bx, by}, {bx - len * std::cos(ang + 0.45), by - len * std::sin(ang + 0.45)}}});
        return;
    }
    if (!a) return;
    colourIt(st, a);
    poppler_annot_set_border_width(a, st->width);
    addAnnot(st, index, a);
}

void erase(const std::shared_ptr<State>& st, int index, double x, double y) {
    PopplerPage* page = poppler_document_get_page(st->doc, index);
    const double h = st->sizes[static_cast<size_t>(index)].second;
    GList* list = poppler_page_get_annot_mapping(page);
    PopplerAnnot* hit = nullptr;
    for (GList* l = list; l; l = l->next) {
        auto* m = static_cast<PopplerAnnotMapping*>(l->data);
        const PopplerAnnotType t = poppler_annot_get_annot_type(m->annot);
        if (t == POPPLER_ANNOT_LINK || t == POPPLER_ANNOT_WIDGET || t == POPPLER_ANNOT_POPUP) continue;
        if (x >= m->area.x1 && x <= m->area.x2 && h - y >= m->area.y1 && h - y <= m->area.y2) hit = m->annot;
    }
    if (hit) {
        poppler_page_remove_annot(page, hit);
        std::erase_if(st->added, [&](const auto& p) {
            if (p.second != hit) return false;
            g_object_unref(p.second);
            return true;
        });
        markDirty(st);
        rerender(st, index);
    }
    poppler_page_free_annot_mapping(list);
    g_object_unref(page);
}

// Select tool, a click: a link, or a form field
bool clickLinkOrField(const std::shared_ptr<State>& st, int index, double x, double y) {
    PopplerPage* page = poppler_document_get_page(st->doc, index);
    const double h = st->sizes[static_cast<size_t>(index)].second;
    bool done = false;
    GList* links = poppler_page_get_link_mapping(page);
    for (GList* l = links; l && !done; l = l->next) {
        auto* m = static_cast<PopplerLinkMapping*>(l->data);
        if (x < m->area.x1 || x > m->area.x2 || h - y < m->area.y1 || h - y > m->area.y2) continue;
        PopplerAction* a = m->action;
        if (a->type == POPPLER_ACTION_URI && a->uri.uri) {
            m3::run({"xdg-open", a->uri.uri}, nullptr);
            done = true;
        } else if (a->type == POPPLER_ACTION_GOTO_DEST && a->goto_dest.dest) {
            PopplerDest* d = a->goto_dest.dest;
            PopplerDest* found = nullptr;
            if (d->type == POPPLER_DEST_NAMED) d = found = poppler_document_find_dest(st->doc, d->named_dest);
            if (d) {
                const int target = d->page_num - 1;
                const double th = target >= 0 && target < st->pages ? st->sizes[static_cast<size_t>(target)].second : 0;
                EventLoop::get().defer([st, target, yy = d->change_top ? th - d->top : 0.0] {
                    if (*st->alive) jumpTo(st, target, yy);
                });
            }
            if (found) poppler_dest_free(found);
            done = true;
        }
    }
    poppler_page_free_link_mapping(links);
    if (!done) {
        GList* fields = poppler_page_get_form_field_mapping(page);
        for (GList* l = fields; l && !done; l = l->next) {
            auto* m = static_cast<PopplerFormFieldMapping*>(l->data);
            if (x < m->area.x1 || x > m->area.x2 || h - y < m->area.y1 || h - y > m->area.y2) continue;
            PopplerFormField* f = m->field;
            const PopplerFormFieldType t = poppler_form_field_get_field_type(f);
            if (t == POPPLER_FORM_FIELD_BUTTON && poppler_form_field_button_get_button_type(f) != POPPLER_FORM_BUTTON_PUSH) {
                poppler_form_field_button_set_state(f, !poppler_form_field_button_get_state(f));
                markDirty(st);
                rerender(st, index);
                done = true;
            } else if (t == POPPLER_FORM_FIELD_TEXT && !poppler_form_field_is_read_only(f)) {
                // Typed in the bar above the pages
                char* cur = poppler_form_field_text_get_text(f);
                st->pendingText = cur ? cur : "";
                g_free(cur);
                g_object_ref(f);
                auto field = std::shared_ptr<PopplerFormField>(f, [](PopplerFormField* p) { g_object_unref(p); });
                st->textBar->clearChildren();
                st->textBar->setVisible(true);
                st->textBar->add<Icon>("edit_note", 20)->setColor("primary", false);
                auto* tf = st->textBar->add<TextField>();
                tf->grow = true;
                tf->setText(st->pendingText);
                tf->setPlaceholder("The field's text");
                auto apply = [st, field, index, tf] {
                    poppler_form_field_text_set_text(field.get(), tf->text().c_str());
                    markDirty(st);
                    rerender(st, index);
                    st->pendingText.clear();
                    st->textBar->setVisible(false);
                };
                tf->onSubmit = [apply](const std::string&) { apply(); };
                pillButton(st->textBar, "Fill in", true, apply, "check");
                EventLoop::get().defer([st, tf] {
                    if (*st->alive) tf->focus();
                });
                done = true;
            }
        }
        poppler_page_free_form_field_mapping(fields);
    }
    g_object_unref(page);
    return done;
}

void PageView::onPress(uint32_t button, float x, float y) {
    if (thumb) {
        if (button == BTN_LEFT) jumpTo(st, index);
        return;
    }
    if (button != BTN_LEFT) return;
    const auto [px, py] = toPage(x, y);
    ax = bx = px, ay = by = py;
    stroke = {{px, py}};
    dragging = true;
    st->current = index;
    switch (st->tool) {
    case Tool::Select:
    case Tool::Highlight:
    case Tool::Underline:
    case Tool::Strike:
        st->selPage = index;
        st->sel = {px, py, px, py};
        break;
    case Tool::Eraser:
        erase(st, index, px, py);
        dragging = false;
        break;
    case Tool::Text:
    case Tool::Note:
        if (st->pendingText.empty()) {
            if (st->rebuildBars) st->rebuildBars(); // asks for the text first
        } else {
            textAnnot(st, index, px, py, st->tool == Tool::Note);
        }
        dragging = false;
        break;
    case Tool::Sign:
        if (!st->signature.empty()) {
            // Placed 150 pt wide where it was clicked, its own shape kept
            std::vector<std::vector<std::pair<double, double>>> strokes;
            for (const auto& s : st->signature) {
                std::vector<std::pair<double, double>> o;
                for (const auto& [sx, sy] : s) o.push_back({px + sx * 150, py + sy * 60});
                strokes.push_back(o);
            }
            inkAnnot(st, index, strokes);
        }
        dragging = false;
        break;
    default: break;
    }
    requestRedraw();
}

void PageView::onDrag(float x, float y) {
    if (!dragging || thumb) return;
    const auto [px, py] = toPage(x, y);
    bx = px, by = py;
    if (st->tool == Tool::Pen) stroke.push_back({px, py});
    if (st->selPage == index) st->sel.x2 = px, st->sel.y2 = py;
    requestRedraw();
}

void PageView::onRelease() {
    if (!dragging || thumb) return;
    dragging = false;
    const bool moved = std::hypot(bx - ax, by - ay) > 2;
    switch (st->tool) {
    case Tool::Select:
        if (!moved) {
            st->selPage = -1;
            clickLinkOrField(st, index, ax, ay);
        } else {
            // Selected text is the primary selection at once
            PopplerPage* page = poppler_document_get_page(st->doc, index);
            PopplerRectangle s = st->sel;
            char* text = poppler_page_get_selected_text(page, POPPLER_SELECTION_GLYPH, &s);
            if (text && *text) m3::run({"wl-copy", "--primary"}, nullptr, text);
            g_free(text);
            g_object_unref(page);
        }
        break;
    case Tool::Highlight:
    case Tool::Underline:
    case Tool::Strike:
        if (moved) markText(st, index);
        break;
    case Tool::Pen:
        inkAnnot(st, index, {stroke});
        break;
    case Tool::Box:
    case Tool::Ellipse:
    case Tool::Line:
    case Tool::Arrow: shapeAnnot(st, index, ax, ay, bx, by); break;
    default: break;
    }
    stroke.clear();
    requestRedraw();
}

// The signature pad: strokes drawn with the pointer, kept in a 0–1 box
class SignPad : public Node {
public:
    std::shared_ptr<State> st;
    std::vector<std::vector<std::pair<double, double>>> strokes;
    explicit SignPad(std::shared_ptr<State> s) : st(std::move(s)) {}
    bool interactive() const override { return true; }

protected:
    SizeF doMeasure(SizeF) override { return {360, 140}; }
    void onPress(uint32_t b, float x, float y) override {
        if (b != BTN_LEFT) return;
        strokes.push_back({norm(x, y)});
        requestRedraw();
    }
    void onDrag(float x, float y) override {
        if (strokes.empty()) return;
        strokes.back().push_back(norm(x, y));
        requestRedraw();
    }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Color{1, 1, 1, 1}, Radii(14));
        r.rect({b.x + 20, b.bottom() - 34, b.w - 40, 1.5f}, Color{0, 0, 0, 0.25f});
        for (const auto& s : strokes)
            for (size_t i = 0; i < s.size(); ++i) {
                const auto& [x0, y0] = s[i > 0 ? i - 1 : i];
                const auto& [x1, y1] = s[i];
                const int n = std::max(1, static_cast<int>(std::hypot((x1 - x0) * b.w, (y1 - y0) * b.h) / 1.2));
                for (int k = 0; k <= n; ++k) {
                    const float x = b.x + static_cast<float>(x0 + (x1 - x0) * k / n) * b.w, y = b.y + static_cast<float>(y0 + (y1 - y0) * k / n) * b.h;
                    r.rect({x - 1.5f, y - 1.5f, 3, 3}, Color{0.08f, 0.1f, 0.25f, 1}, Radii(1.5f));
                }
            }
    }

private:
    std::pair<double, double> norm(float x, float y) const {
        return {std::clamp((x - rect().x) / rect().w, 0.0f, 1.0f), std::clamp((y - rect().y) / rect().h, 0.0f, 1.0f)};
    }
};

// ── Files: open, save, pages ──

bool load(const std::shared_ptr<State>& st, const std::string& path, std::string* error) {
    GError* err = nullptr;
    PopplerDocument* d = poppler_document_new_from_file(uriOf(path).c_str(), st->password.empty() ? nullptr : st->password.c_str(), &err);
    if (!d) {
        if (error) *error = err && err->code == POPPLER_ERROR_ENCRYPTED ? "encrypted" : err ? err->message : "Couldn't open it";
        if (err) g_error_free(err);
        return false;
    }
    for (auto& [i, a] : st->added) g_object_unref(a);
    st->added.clear();
    if (st->doc) g_object_unref(st->doc);
    st->doc = d;
    st->path = path;
    st->pages = poppler_document_get_n_pages(d);
    st->sizes.clear();
    for (int i = 0; i < st->pages; ++i) {
        PopplerPage* p = poppler_document_get_page(d, i);
        double w = 612, h = 792;
        if (p) poppler_page_get_size(p, &w, &h), g_object_unref(p);
        st->sizes.push_back({w, h});
    }
    st->dirty = false;
    st->matches.clear(), st->match = -1, st->selPage = -1;
    st->current = std::min(st->current, std::max(0, st->pages - 1));
    return true;
}

bool saveTo(const std::shared_ptr<State>& st, const std::string& target) {
    // Written beside it, then put in its place
    const std::string tmp = target + ".saving.pdf";
    GError* err = nullptr;
    const bool ok = poppler_document_save(st->doc, uriOf(tmp).c_str(), &err);
    if (!ok) {
        m3::run({"notify-send", "-a", "PDF", "-i", "dialog-error", "Couldn't save", err ? err->message : target}, nullptr);
        if (err) g_error_free(err);
        std::error_code ec;
        fs::remove(tmp, ec);
        return false;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    return !ec;
}

void save(const std::shared_ptr<State>& st) {
    if (!st->doc || !st->dirty) return;
    if (saveTo(st, st->path)) {
        st->dirty = false;
        // Reopened from the saved file: what's on screen is what's in it
        const int cur = st->current;
        std::string e;
        load(st, st->path, &e);
        st->current = cur;
        if (st->rebuildPages) st->rebuildPages();
        EventLoop::get().defer([st, cur] {
            if (*st->alive) jumpTo(st, cur);
        });
    }
}

std::string saveCopy(const std::shared_ptr<State>& st) {
    const fs::path p(st->path);
    const std::string target = (p.parent_path() / fileops::freeName(p.parent_path().string(), p.stem().string() + " (edited).pdf")).string();
    if (!saveTo(st, target)) return "";
    m3::run({"notify-send", "-a", "PDF", "-i", "document-save", "Saved a copy", fileops::displayPath(target)}, nullptr);
    return target;
}

// Pages rearranged, through qpdf (kept: links, forms, the outline); the
// file's saved first, the new order written over it, and it's reopened
void pages(const std::shared_ptr<State>& st, const std::vector<std::string>& qpdfArgs, int focusPage) {
    if (!haveCommand("qpdf")) {
        m3::run({"notify-send", "-a", "PDF", "-i", "dialog-information", "Pages need qpdf", "sudo pacman -S qpdf"}, nullptr);
        return;
    }
    if (st->dirty) save(st);
    const std::string tmp = st->path + ".pages.pdf";
    std::vector<std::string> cmd = {"qpdf"};
    cmd.insert(cmd.end(), qpdfArgs.begin(), qpdfArgs.end());
    for (auto& a : cmd) {
        if (a == "@IN") a = st->path;
        if (a == "@OUT") a = tmp;
    }
    m3::run(cmd, [st, tmp, focusPage](int code, const std::string&) {
        if (!*st->alive) return;
        std::error_code ec;
        // qpdf's 3 is "warnings": the file's still written
        if ((code != 0 && code != 3) || !fs::exists(tmp, ec)) {
            fs::remove(tmp, ec);
            m3::run({"notify-send", "-a", "PDF", "-i", "dialog-error", "Couldn't change the pages"}, nullptr);
            return;
        }
        fs::rename(tmp, st->path, ec);
        std::string e;
        load(st, st->path, &e);
        st->current = std::clamp(focusPage, 0, std::max(0, st->pages - 1));
        if (st->rebuildPages) st->rebuildPages();
        EventLoop::get().defer([st] {
            if (*st->alive) jumpTo(st, st->current);
        });
    });
}

std::string order(const std::vector<int>& pagesOneBased) {
    std::string s;
    for (int p : pagesOneBased) s += (s.empty() ? "" : ",") + std::to_string(p);
    return s;
}

void pageMenu(const std::shared_ptr<State>& st, Node* from) {
    const int cur = st->current, n = st->pages;
    std::vector<MenuItem> items;
    items.emplace_back("rotate_right", "Rotate this page right", "", [st, cur] { pages(st, {"@IN", std::format("--rotate=+90:{}", cur + 1), "@OUT"}, cur); });
    items.emplace_back("rotate_left", "Rotate this page left", "", [st, cur] { pages(st, {"@IN", std::format("--rotate=-90:{}", cur + 1), "@OUT"}, cur); });
    items.emplace_back("arrow_upward", "Move it up", "", [st, cur] {
        std::vector<int> o;
        for (int i = 1; i <= st->pages; ++i) o.push_back(i);
        std::swap(o[static_cast<size_t>(cur)], o[static_cast<size_t>(cur - 1)]);
        pages(st, {"--empty", "--pages", "@IN", order(o), "--", "@OUT"}, cur - 1);
    }, false, false, cur > 0);
    items.emplace_back("arrow_downward", "Move it down", "", [st, cur] {
        std::vector<int> o;
        for (int i = 1; i <= st->pages; ++i) o.push_back(i);
        std::swap(o[static_cast<size_t>(cur)], o[static_cast<size_t>(cur + 1)]);
        pages(st, {"--empty", "--pages", "@IN", order(o), "--", "@OUT"}, cur + 1);
    }, false, false, cur + 1 < n);
    items.emplace_back("content_cut", "Save this page on its own", "", [st, cur] {
        const fs::path p(st->path);
        const std::string out = (p.parent_path() / fileops::freeName(p.parent_path().string(), std::format("{} (page {}).pdf", p.stem().string(), cur + 1))).string();
        if (!haveCommand("qpdf")) {
            m3::run({"pdfseparate", "-f", std::to_string(cur + 1), "-l", std::to_string(cur + 1), st->path, out}, nullptr);
        } else {
            m3::run({"qpdf", "--empty", "--pages", st->path, std::to_string(cur + 1), "--", out}, nullptr);
        }
        m3::run({"notify-send", "-a", "PDF", "-i", "document-save", "Page saved", fileops::displayPath(out)}, nullptr);
    }, false, false, true, true);
    items.emplace_back("delete", "Delete this page", "", [st, cur] {
        std::vector<int> o;
        for (int i = 1; i <= st->pages; ++i)
            if (i != cur + 1) o.push_back(i);
        pages(st, {"--empty", "--pages", "@IN", order(o), "--", "@OUT"}, cur);
    }, false, true, n > 1);
    floatingMenu(from, items);
}

void runSearch(const std::shared_ptr<State>& st, const std::string& q) {
    st->query = q;
    st->matches.clear();
    st->match = -1;
    if (!q.empty() && st->doc) {
        for (int i = 0; i < st->pages; ++i) {
            PopplerPage* page = poppler_document_get_page(st->doc, i);
            const double h = st->sizes[static_cast<size_t>(i)].second;
            GList* found = poppler_page_find_text_with_options(page, q.c_str(), POPPLER_FIND_DEFAULT);
            for (GList* l = found; l; l = l->next) {
                auto* r = static_cast<PopplerRectangle*>(l->data);
                // Found in the PDF's own coordinates: turned to top-left
                st->matches.push_back({i, {r->x1, h - r->y2, r->x2, h - r->y1}});
                poppler_rectangle_free(r);
            }
            g_list_free(found);
            g_object_unref(page);
        }
        // From where you are
        for (size_t i = 0; i < st->matches.size(); ++i)
            if (st->matches[i].page >= st->current) {
                st->match = static_cast<int>(i);
                break;
            }
        if (st->match < 0 && !st->matches.empty()) st->match = 0;
    }
    if (st->match >= 0) jumpTo(st, st->matches[static_cast<size_t>(st->match)].page, st->matches[static_cast<size_t>(st->match)].rect.y1);
    for (auto* v : st->views) v->requestRedraw();
    if (st->refreshLabels) st->refreshLabels();
}

void stepMatch(const std::shared_ptr<State>& st, int by) {
    if (st->matches.empty()) return;
    const int n = static_cast<int>(st->matches.size());
    st->match = ((st->match + by) % n + n) % n;
    const auto& m = st->matches[static_cast<size_t>(st->match)];
    jumpTo(st, m.page, m.rect.y1);
    for (auto* v : st->views) v->requestRedraw();
    if (st->refreshLabels) st->refreshLabels();
}

void properties(const std::shared_ptr<State>& st, Node* from) {
    std::vector<MenuItem> items;
    auto add = [&](const char* icon, const std::string& k, gchar* v) {
        if (v && *v) items.emplace_back(icon, k, v, [] {}, false, false, true);
        g_free(v);
    };
    add("title", "Title", poppler_document_get_title(st->doc));
    add("person", "Author", poppler_document_get_author(st->doc));
    add("topic", "Subject", poppler_document_get_subject(st->doc));
    add("build", "Made with", poppler_document_get_creator(st->doc));
    add("precision_manufacturing", "Producer", poppler_document_get_producer(st->doc));
    add("picture_as_pdf", "Version", poppler_document_get_pdf_version_string(st->doc));
    std::error_code ec;
    items.emplace_back("description", "Pages", std::to_string(st->pages), [] {});
    items.emplace_back("hard_drive", "Size", fileops::humanSize(fs::file_size(st->path, ec)), [] {});
    const std::time_t created = poppler_document_get_creation_date(st->doc);
    if (created > 0) items.emplace_back("calendar_today", "Created", fileops::humanTime(created), [] {});
    floatingMenu(from, items);
}

// PDFs where they gather, newest first (the empty window's list)
std::vector<std::string> recentPdfs() {
    std::vector<std::pair<std::time_t, std::string>> all;
    const std::string home = paths::home();
    for (const auto& [var, dir] : std::initializer_list<std::pair<const char*, const char*>>{{"XDG_DOCUMENTS_DIR", "Documents"}, {"XDG_DOWNLOAD_DIR", "Downloads"}, {"XDG_DESKTOP_DIR", "Desktop"}}) {
        const char* v = std::getenv(var);
        const std::string d = v && *v ? v : home + "/" + dir;
        for (const auto& e : fileops::list(d, false))
            if (!e.dir && (e.name.ends_with(".pdf") || e.name.ends_with(".PDF"))) all.push_back({e.modified, e.path});
    }
    std::ranges::sort(all, std::greater{});
    std::vector<std::string> out;
    for (size_t i = 0; i < all.size() && i < 40; ++i) out.push_back(all[i].second);
    return out;
}

class PdfKeeper : public Node {
public:
    explicit PdfKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~PdfKeeper() override {
        *m_st->alive = false;
        if (m_st->tick) EventLoop::get().cancel(m_st->tick);
        // Unsaved edits aren't lost: a copy beside the file (unless Settings
        // says to let them go)
        if (m_st->dirty && m_st->doc && Config::get().value<std::string>("/pdf/onClose", "copy") == "copy") saveCopy(m_st);
        for (auto& [i, a] : m_st->added) g_object_unref(a);
        m_st->added.clear();
        m_st->views.clear(), m_st->thumbViews.clear(), m_st->lru.clear();
        m_st->rebuildPages = m_st->rebuildBars = m_st->refreshLabels = m_st->buildBars = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

void openPdf(const std::shared_ptr<State>& st, const std::string& path);

void showEmpty(const std::shared_ptr<State>& st, const std::string& note = {}) {
    auto* e = st->empty;
    e->clearChildren();
    e->setVisible(true);
    st->body->setVisible(false);
    st->toolbar->setVisible(false);
    st->editBar->setVisible(false);
    st->searchBar->setVisible(false);
    e->add<Icon>("picture_as_pdf", 64)->setColor("primary", false);
    e->add<Label>("PDF")->setSize(26)->setWeight(500);
    if (!note.empty()) caption(e, note);
    const auto recent = recentPdfs();
    if (recent.empty()) {
        caption(e, "Open a PDF from Files, or run material3ui-pdf <file>");
        return;
    }
    caption(e, "Recent PDFs in Documents, Downloads and the desktop");
    auto* list = e->add<Flex>(Flex::Direction::Column);
    list->setSpacing(2)->setAlignItems(Align::Stretch);
    list->fixedWidth = 520;
    for (const auto& p : recent) {
        auto* b = list->add<Button>(Flex::Direction::Row);
        b->setPadding({14, 9})->setSpacing(12)->setRadius(Radii(14));
        b->add<Icon>("picture_as_pdf", 22)->setColor("error", false);
        auto* t = b->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start);
        t->grow = true;
        t->add<Label>(fs::path(p).filename().string())->setSize(14)->setWeight(500)->setMaxWidth(420);
        t->add<Label>(fileops::displayPath(fs::path(p).parent_path().string()))->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420);
        b->onClicked = [st, p](uint32_t btn) {
            if (btn == BTN_LEFT) openPdf(st, p);
        };
    }
}

void askPassword(const std::shared_ptr<State>& st, const std::string& path, bool wrong) {
    auto* e = st->empty;
    e->clearChildren();
    e->setVisible(true);
    st->body->setVisible(false);
    st->toolbar->setVisible(false);
    e->add<Icon>("lock", 56)->setColor("primary", false);
    e->add<Label>(fs::path(path).filename().string())->setSize(18)->setWeight(500);
    caption(e, wrong ? "That password isn't it. Try again" : "This PDF is protected with a password");
    auto* f = e->add<TextField>();
    f->setSecret(true);
    f->setPlaceholder("Password");
    f->fixedWidth = 320;
    f->onSubmit = [st, path](const std::string& pw) {
        st->password = pw;
        openPdf(st, path);
    };
    EventLoop::get().defer([st, f] {
        if (*st->alive) f->focus();
    });
}

void openPdf(const std::shared_ptr<State>& st, const std::string& path) {
    if (st->dirty && st->doc) saveCopy(st);
    std::string err;
    st->current = 0;
    if (!load(st, path, &err)) {
        if (err == "encrypted") return askPassword(st, path, !st->password.empty());
        return showEmpty(st, "Couldn't open " + fs::path(path).filename().string() + ": " + err);
    }
    st->password.clear();
    st->empty->setVisible(false);
    st->body->setVisible(true);
    st->toolbar->setVisible(true);
    if (st->rebuildPages) st->rebuildPages();
    if (st->rebuildBars) st->rebuildBars();
}

void setTool(std::shared_ptr<State> st, Tool t) {
    st->tool = t;
    st->selPage = -1;
    if (t != Tool::Text && t != Tool::Note) st->pendingText.clear();
    if (st->rebuildBars) st->rebuildBars();
    for (auto* v : st->views) v->requestRedraw();
}

} // namespace

// The document's contents (its outline), a click to each part
int outlinePage(const std::shared_ptr<State>& st, PopplerAction* a) {
    if (!a || a->type != POPPLER_ACTION_GOTO_DEST || !a->goto_dest.dest) return -1;
    PopplerDest* d = a->goto_dest.dest;
    PopplerDest* found = nullptr;
    if (d->type == POPPLER_DEST_NAMED) d = found = poppler_document_find_dest(st->doc, d->named_dest);
    const int page = d ? d->page_num - 1 : -1;
    if (found) poppler_dest_free(found);
    return page;
}
void addOutline(const std::shared_ptr<State>& st, Flex* into, PopplerIndexIter* it, int depth) {
    do {
        PopplerAction* a = poppler_index_iter_get_action(it);
        const std::string title = a && a->any.title ? a->any.title : "";
        const int page = outlinePage(st, a);
        if (!title.empty()) {
            auto* b = into->add<Button>(Flex::Direction::Row);
            b->setPadding({8.0f + static_cast<float>(depth) * 14, 6, 8, 6})->setSpacing(8)->setRadius(Radii(10));
            auto* l = b->add<Label>(title);
            l->setSize(depth == 0 ? 13 : 12)->setWeight(depth == 0 ? 600 : 400)->setMaxWidth(170.0f - static_cast<float>(depth) * 14)->setMaxLines(2);
            l->grow = true;
            if (page >= 0) b->add<Label>(std::to_string(page + 1))->setSize(11)->setColor("on_surface_variant", false);
            b->onClicked = [st, page](uint32_t btn) {
                if (btn == BTN_LEFT && page >= 0) jumpTo(st, page);
            };
        }
        if (a) poppler_action_free(a);
        if (PopplerIndexIter* child = poppler_index_iter_get_child(it)) {
            addOutline(st, into, child, depth + 1);
            poppler_index_iter_free(child);
        }
    } while (poppler_index_iter_next(it));
}

// One document's view: the window holds one per tab
std::shared_ptr<State> pdfPane(Flex* card, Popup& popup, const std::string& file, std::function<bool(const KeyEvent&)>& keys) {
    auto st = std::make_shared<State>();
    st->popup = &popup;
    card->setPadding(Insets(10))->setSpacing(8);
    st->root = card;
    // How it opens (Settings → PDF)
    const std::string zoom = Config::get().value<std::string>("/pdf/zoom", "fit");
    st->fitWidth = zoom == "fit";
    if (!st->fitWidth) st->zoom = static_cast<float>(std::atof(zoom.c_str())) / 100.0f;
    st->night = Config::get().value<bool>("/pdf/night", false);
    st->thumbs = Config::get().value<bool>("/pdf/thumbs", false);

    st->empty = card->add<Flex>(Flex::Direction::Column);
    st->empty->setAlignItems(Align::Center)->setSpacing(12)->setPadding({0, 40});
    st->empty->grow = true;

    // The bar: the file, the page, zoom, and what to do
    st->toolbar = card->add<Flex>(Flex::Direction::Row);
    st->toolbar->setSpacing(4)->setAlignItems(Align::Center)->setPadding({6, 0});
    {
        auto* tb = st->toolbar;
        iconButton(tb, "view_sidebar", [st] {
            st->thumbs = !st->thumbs;
            if (st->thumbScroll) st->thumbScroll->setVisible(st->thumbs);
        }, 22)->tooltip = "Pages (F9)";
        auto* names = tb->add<Flex>(Flex::Direction::Column);
        names->setAlignItems(Align::Start)->setSpacing(1)->setPadding({6, 0});
        names->grow = true;
        names->givesWay = true;
        st->title = names->add<Label>("");
        st->title->setSize(15)->setWeight(600)->setMaxWidth(380);
        auto* sub = names->add<Flex>(Flex::Direction::Row);
        sub->setSpacing(8);
        st->pageLabel = sub->add<Label>("");
        st->pageLabel->setSize(12)->setColor("on_surface_variant", false);
        st->dirtyLabel = sub->add<Label>("");
        st->dirtyLabel->setSize(12)->setWeight(600)->setColor("tertiary", false);
        iconButton(tb, "zoom_out", [st] {
            st->fitWidth = false;
            st->zoom = std::max(0.25f, st->zoom / 1.2f);
            Node::invalidateMeasures();
            if (st->root) st->root->requestLayout();
            if (st->refreshLabels) st->refreshLabels();
        }, 22)->tooltip = "Zoom out (−)";
        auto* zp = tb->add<Button>();
        pill(zp->setBackground("surface_container_high", false))->setPadding({12, 6});
        st->zoomLabel = zp->add<Label>("Fit");
        st->zoomLabel->setSize(13)->setWeight(500);
        st->zoomLabel->fixedWidth = 46;
        zp->tooltip = "Fit to the width (Ctrl+0)";
        zp->onClicked = [st](uint32_t b) {
            if (b != BTN_LEFT) return;
            st->fitWidth = true;
            Node::invalidateMeasures();
            st->root->requestLayout();
            if (st->refreshLabels) st->refreshLabels();
        };
        iconButton(tb, "zoom_in", [st] {
            st->fitWidth = false;
            st->zoom = std::min(4.0f, st->zoom * 1.2f);
            Node::invalidateMeasures();
            st->root->requestLayout();
            if (st->refreshLabels) st->refreshLabels();
        }, 22)->tooltip = "Zoom in (+)";
        tb->add<Node>()->fixedWidth = 6;
        iconButton(tb, "search", [st] {
            st->searchBar->setVisible(!st->searchBar->visible());
            if (st->searchBar->visible()) EventLoop::get().defer([st] {
                if (*st->alive && st->searchField) st->searchField->focus();
            });
        }, 22)->tooltip = "Search (Ctrl+F)";
        auto* editBtn = iconButton(tb, "edit", [st] {
            st->editing = !st->editing;
            if (!st->editing) setTool(st, Tool::Select);
            if (st->rebuildBars) st->rebuildBars();
        }, 22);
        editBtn->tooltip = "Edit: mark up, sign, fill in (E)";
        iconButton(tb, "dark_mode", [st] {
            st->night = !st->night;
            for (auto* v : st->views) v->requestRedraw();
        }, 22)->tooltip = "Night mode (N)";
        Button* more = iconButton(tb, "more_vert", [] {}, 22);
        more->onClicked = [st, more](uint32_t b) {
            if (b != BTN_LEFT || !st->doc) return;
            std::vector<MenuItem> items;
            items.emplace_back("save", "Save", "Ctrl+S", [st] { save(st); }, false, false, st->dirty);
            items.emplace_back("file_copy", "Save a copy", "Ctrl+Shift+S", [st] { saveCopy(st); });
            items.emplace_back("auto_stories", "Pages…", "", [st, more] { pageMenu(st, more); }, false, false, true, true);
            items.emplace_back("print", "Print", "", [st] {
                if (haveCommand("lp")) m3::run({"lp", st->path}, nullptr);
                else m3::run({"notify-send", "-a", "PDF", "Printing needs CUPS (lp)"}, nullptr);
            });
            items.emplace_back("folder_open", "Show in Files", "", [st] {
                if (open) open("files " + st->path);
            });
            items.emplace_back("info", "Properties", "", [st, more] {
                EventLoop::get().defer([st, more] {
                    if (*st->alive) properties(st, more);
                });
            });
            floatingMenu(more, items);
        };
    }

    // Search
    st->searchBar = card->add<Flex>(Flex::Direction::Row);
    st->searchBar->setSpacing(8)->setAlignItems(Align::Center)->setPadding({6, 0});
    {
        auto* pillBox = st->searchBar->add<Flex>(Flex::Direction::Row);
        pill(pillBox->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
        pillBox->grow = true;
        pillBox->add<Icon>("search", 20)->setColor("on_surface_variant", false);
        st->searchField = pillBox->add<TextField>();
        st->searchField->bare = true;
        st->searchField->grow = true;
        st->searchField->setPlaceholder("Find in the document");
        st->searchField->onSubmit = [st](const std::string& q) {
            if (q == st->query && !st->matches.empty()) stepMatch(st, wl::Display::get().heldModifiers() & ModShift ? -1 : 1);
            else runSearch(st, q);
        };
        st->matchLabel = st->searchBar->add<Label>("");
        st->matchLabel->setSize(13)->setColor("on_surface_variant", false);
        iconButton(st->searchBar, "keyboard_arrow_up", [st] { stepMatch(st, -1); }, 22)->tooltip = "Previous (Shift+Enter)";
        iconButton(st->searchBar, "keyboard_arrow_down", [st] { stepMatch(st, 1); }, 22)->tooltip = "Next (Enter)";
        iconButton(st->searchBar, "close", [st] {
            st->searchBar->setVisible(false);
            runSearch(st, "");
        }, 20);
    }
    st->searchBar->setVisible(false);

    // The edit bar
    st->editBar = card->add<Flex>(Flex::Direction::Column);
    st->editBar->setSpacing(6);
    st->editBar->setVisible(false);
    st->textBar = card->add<Flex>(Flex::Direction::Row);
    st->textBar->setSpacing(8)->setAlignItems(Align::Center)->setPadding({8, 4})->setRadius(Radii(18));
    st->textBar->setBackground("surface_container_high", false);
    st->textBar->setVisible(false);

    // The pages, and their thumbnails beside them
    st->body = card->add<Flex>(Flex::Direction::Row);
    st->body->setSpacing(8)->setAlignItems(Align::Stretch)->setRadius(Radii(20))->setBackground(ColorRef("surface_container_lowest", 0.6f), false);
    st->body->grow = true;
    st->thumbScroll = st->body->add<ScrollView>();
    st->thumbScroll->maxHeight = 1e6f;
    st->thumbScroll->fixedWidth = 210;
    auto* sideCol = st->thumbScroll->add<Flex>(Flex::Direction::Column);
    sideCol->setAlignItems(Align::Stretch);
    st->sideTabs = sideCol->add<Flex>(Flex::Direction::Row);
    st->sideTabs->setPadding({8, 10, 8, 0})->setJustify(Align::Center);
    st->thumbCol = sideCol->add<Flex>(Flex::Direction::Column);
    st->thumbCol->setSpacing(12)->setAlignItems(Align::Center)->setPadding({12, 14});
    st->outlineCol = sideCol->add<Flex>(Flex::Direction::Column);
    st->outlineCol->setSpacing(2)->setAlignItems(Align::Stretch)->setPadding({6, 10});
    st->outlineCol->setVisible(false);
    st->thumbScroll->setVisible(st->thumbs);
    st->scroll = st->body->add<ScrollView>();
    st->scroll->maxHeight = 1e6f;
    st->scroll->grow = true;
    st->pagesCol = st->scroll->add<Flex>(Flex::Direction::Column);
    st->pagesCol->setSpacing(16)->setAlignItems(Align::Center)->setPadding({24, 20});
    card->add<PdfKeeper>(st);

    st->refreshLabels = [st] {
        if (!st->doc) return;
        st->title->setText(fs::path(st->path).filename().string());
        st->pageLabel->setText(std::format("Page {} of {}", st->current + 1, st->pages));
        st->dirtyLabel->setText(st->dirty ? "Edited: Ctrl+S saves" : "");
        st->zoomLabel->setText(st->fitWidth ? "Fit" : std::format("{:.0f}%", st->zoom * 100));
        st->matchLabel->setText(st->query.empty() ? "" : st->matches.empty() ? "No matches" : std::format("{} of {}", st->match + 1, st->matches.size()));
        for (auto* v : st->thumbViews) v->requestRedraw();
    };
    st->rebuildPages = [st] {
        st->pagesCol->clearChildren();
        st->thumbCol->clearChildren();
        st->views.clear(), st->thumbViews.clear(), st->lru.clear();
        for (int i = 0; i < st->pages; ++i) st->views.push_back(st->pagesCol->add<PageView>(st, i, false));
        // Contents, when the document has them
        st->outlineCol->clearChildren();
        st->sideTabs->clearChildren();
        if (PopplerIndexIter* it = poppler_index_iter_new(st->doc)) {
            addOutline(st, st->outlineCol, it, 0);
            poppler_index_iter_free(it);
            connectedGroup<int>(st->sideTabs, st->showOutline ? 1 : 0, {{0, "Pages"}, {1, "Contents"}}, [st](int v) {
                st->showOutline = v == 1;
                st->thumbCol->setVisible(!st->showOutline);
                st->outlineCol->setVisible(st->showOutline);
            });
        } else {
            st->showOutline = false;
        }
        st->thumbCol->setVisible(!st->showOutline);
        st->outlineCol->setVisible(st->showOutline);
        for (int i = 0; i < st->pages; ++i) {
            auto* col = st->thumbCol->add<Flex>(Flex::Direction::Column);
            col->setAlignItems(Align::Center)->setSpacing(4);
            st->thumbViews.push_back(col->add<PageView>(st, i, true));
            col->add<Label>(std::to_string(i + 1))->setSize(11)->setColor("on_surface_variant", false);
        }
        if (st->refreshLabels) st->refreshLabels();
    };
    st->rebuildBars = [st] {
        EventLoop::get().defer([st] {
            if (*st->alive && st->buildBars) st->buildBars();
        });
    };
    st->buildBars = [st] {
        auto* e = st->editBar;
        e->clearChildren();
        e->setVisible(st->editing && st->doc);
        if (!st->editing) {
            st->textBar->setVisible(false);
            return;
        }
        auto* row = e->add<Flex>(Flex::Direction::Row);
        row->setSpacing(2)->setAlignItems(Align::Center)->setPadding({4, 0});
        row->wrap = true;
        for (const auto& t : kTools) {
            const bool here = st->tool == t.tool;
            auto* b = iconButton(row, t.icon, [st, tool = t.tool] { setTool(st, tool); }, 22);
            b->tooltip = t.name;
            if (here) b->setBackground("secondary_container", false);
        }
        row->add<Node>()->fixedWidth = 10;
        for (int i = 0; i < 6; ++i) {
            auto* dot = row->add<Button>();
            dot->setPadding(Insets(4))->setRadius(Radii(14));
            if (st->ink == i) dot->setBackground("secondary_container", false);
            auto* d = dot->add<Flex>(Flex::Direction::Row);
            d->fixedWidth = d->fixedHeight = 20;
            d->setRadius(Radii(10))->setBackground(ColorRef(*Color::fromHex(kInks[i])), false);
            dot->onClicked = [st, i](uint32_t b) {
                if (b != BTN_LEFT) return;
                st->ink = i;
                if (st->rebuildBars) st->rebuildBars();
            };
        }
        dropdown(row, row, std::format("{:g} pt", st->width), {{"1", "Thin"}, {"2", "Medium"}, {"4", "Thick"}, {"7", "Marker"}}, [st](const std::string& v) {
            st->width = std::stod(v);
            if (st->rebuildBars) st->rebuildBars();
        });
        row->add<Node>()->grow = true;
        auto* undoBtn = iconButton(row, "undo", [st] { undo(st); }, 22);
        undoBtn->tooltip = "Undo (Ctrl+Z)";
        undoBtn->setOpacity(st->added.empty() ? 0.4f : 1.0f, false);
        pillButton(row, "Save", true, [st] { save(st); }, "save");

        // What the tool wants first: the words, or the signature
        st->textBar->clearChildren();
        st->textBar->setVisible(false);
        if ((st->tool == Tool::Text || st->tool == Tool::Note) && st->pendingText.empty()) {
            st->textBar->setVisible(true);
            st->textBar->add<Icon>(st->tool == Tool::Note ? "sticky_note_2" : "title", 20)->setColor("primary", false);
            auto* ta = st->textBar->add<TextArea>();
            ta->setMinLines(1)->setSize(15)->setPlaceholder(st->tool == Tool::Note ? "The note: then click where it goes" : "The text: then click where it goes");
            ta->grow = true;
            auto ready = [st, ta] {
                if (ta->text().empty()) return;
                st->pendingText = ta->text();
                st->textBar->setVisible(false);
                caption(st->editBar, "Now click on the page where it goes");
            };
            ta->onOtherKey = [ready](const KeyEvent& ev) {
                if ((ev.sym == XKB_KEY_Return || ev.sym == XKB_KEY_KP_Enter) && ev.ctrl()) return ready(), true;
                return false;
            };
            pillButton(st->textBar, "Place it", true, ready, "ads_click");
            EventLoop::get().defer([st, ta] {
                if (*st->alive) ta->focus();
            });
        } else if (st->tool == Tool::Sign) {
            st->textBar->setVisible(true);
            auto* col = st->textBar->add<Flex>(Flex::Direction::Column);
            col->setSpacing(6);
            caption(col, st->signature.empty() ? "Sign here with the mouse or touchpad, then Use it" : "Your signature: click on a page to place it, or draw it again");
            auto* pad = col->add<SignPad>(st);
            pad->strokes = st->signature;
            auto* btns = st->textBar->add<Flex>(Flex::Direction::Column);
            btns->setSpacing(6);
            pillButton(btns, "Use it", true, [st, pad] {
                st->signature = pad->strokes;
                Config::get().set("/pdf/signature", nlohmann::json(pad->strokes));
                if (st->rebuildBars) st->rebuildBars();
            }, "check");
            pillButton(btns, "Clear", false, [pad] {
                pad->strokes.clear();
                pad->requestRedraw();
            }, "ink_eraser");
        } else {
            auto hint = [&]() -> std::string {
                switch (st->tool) {
                case Tool::Select: return "Drag over text to select it (it's copied); click links and form fields";
                case Tool::Highlight:
                case Tool::Underline:
                case Tool::Strike: return "Drag over the text to mark";
                case Tool::Pen: return "Draw on the page";
                case Tool::Eraser: return "Click an annotation to take it away";
                default: return "Drag on the page";
                }
            }();
            caption(e, hint);
        }
    };

    // Which page is in view, as the pages scroll
    st->tick = EventLoop::get().addTimer(250, [st] {
        if (!*st->alive || st->views.empty() || !st->scroll) return;
        const float mid = st->scroll->rect().y + st->scroll->rect().h * 0.4f;
        for (auto* v : st->views)
            if (v->rect().y <= mid && v->rect().bottom() + 16 >= mid) {
                if (v->index != st->current) {
                    st->current = v->index;
                    if (st->refreshLabels) st->refreshLabels();
                }
                break;
            }
    }, true);

    // The signature drawn last time
    try {
        const auto sig = Config::get().value<nlohmann::json>("/pdf/signature", nlohmann::json::array());
        st->signature = sig.get<std::vector<std::vector<std::pair<double, double>>>>();
    } catch (...) {
    }

    auto& p = popup;
    keys = [st, &p](const KeyEvent& e) {
        const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
        if (e.ctrl()) {
            if (k == 'f') {
                st->searchBar->setVisible(true);
                EventLoop::get().defer([st] {
                    if (*st->alive && st->searchField) st->searchField->focus();
                });
                return true;
            }
            if (k == 's' && e.shift()) return saveCopy(st), true;
            if (k == 's') return save(st), true;
            if (k == 'z') return undo(st), true;
            if (k == '0') {
                st->fitWidth = true;
                Node::invalidateMeasures();
                st->root->requestLayout();
                if (st->refreshLabels) st->refreshLabels();
                return true;
            }
            if (k == 'c' && st->selPage >= 0 && st->doc) {
                PopplerPage* page = poppler_document_get_page(st->doc, st->selPage);
                PopplerRectangle s = st->sel;
                char* text = poppler_page_get_selected_text(page, POPPLER_SELECTION_GLYPH, &s);
                if (text) m3::run({"wl-copy"}, nullptr, text);
                g_free(text);
                g_object_unref(page);
                return true;
            }
            return false;
        }
        if (!st->doc) return e.sym == XKB_KEY_Escape ? (p.close(), true) : false;
        switch (e.sym) {
        case XKB_KEY_Page_Down:
        case XKB_KEY_space: jumpTo(st, std::min(st->pages - 1, st->current + 1)); return true;
        case XKB_KEY_Page_Up: jumpTo(st, std::max(0, st->current - 1)); return true;
        case XKB_KEY_Home: jumpTo(st, 0); return true;
        case XKB_KEY_End: jumpTo(st, st->pages - 1); return true;
        case XKB_KEY_plus:
        case XKB_KEY_equal:
            st->fitWidth = false;
            st->zoom = std::min(4.0f, st->zoom * 1.2f);
            Node::invalidateMeasures();
            st->root->requestLayout();
            if (st->refreshLabels) st->refreshLabels();
            return true;
        case XKB_KEY_minus:
            st->fitWidth = false;
            st->zoom = std::max(0.25f, st->zoom / 1.2f);
            Node::invalidateMeasures();
            st->root->requestLayout();
            if (st->refreshLabels) st->refreshLabels();
            return true;
        case XKB_KEY_F9:
            st->thumbs = !st->thumbs;
            st->thumbScroll->setVisible(st->thumbs);
            return true;
        case XKB_KEY_e:
            st->editing = !st->editing;
            if (!st->editing) setTool(st, Tool::Select);
            if (st->rebuildBars) st->rebuildBars();
            return true;
        case XKB_KEY_n:
            st->night = !st->night;
            for (auto* v : st->views) v->requestRedraw();
            return true;
        case XKB_KEY_Escape:
            if (st->tool != Tool::Select) return setTool(st, Tool::Select), true;
            if (st->searchBar->visible()) return st->searchBar->setVisible(false), runSearch(st, ""), true;
            p.close();
            return true;
        default: return false;
        }
    };

    if (file.empty()) showEmpty(st);
    else openPdf(st, file);
    return st;
}

namespace {

struct PdfTab {
    std::shared_ptr<State> st;
    Flex* box = nullptr;
    std::function<bool(const KeyEvent&)> keys;
};
struct PdfWindow {
    std::vector<PdfTab> tabs;
    int active = 0;
    Flex *strip = nullptr, *stack = nullptr;
    Popup* popup = nullptr;
    std::function<void()> rebuild;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void showPdfTab(const std::shared_ptr<PdfWindow>& w, int i) {
    if (w->tabs.empty()) return;
    w->active = std::clamp(i, 0, static_cast<int>(w->tabs.size()) - 1);
    for (size_t k = 0; k < w->tabs.size(); ++k) w->tabs[k].box->setVisible(static_cast<int>(k) == w->active);
    if (w->rebuild) w->rebuild();
}

void addPdfTab(const std::shared_ptr<PdfWindow>& w, const std::string& file) {
    // Already open: that tab; an empty tab: this file in it
    for (size_t i = 0; i < w->tabs.size(); ++i)
        if (!file.empty() && w->tabs[i].st->path == file && w->tabs[i].st->doc) return showPdfTab(w, static_cast<int>(i));
    if (!file.empty() && !w->tabs.empty() && !w->tabs[static_cast<size_t>(w->active)].st->doc) {
        openPdf(w->tabs[static_cast<size_t>(w->active)].st, file);
        return showPdfTab(w, w->active);
    }
    PdfTab t;
    t.box = w->stack->add<Flex>(Flex::Direction::Column);
    t.box->setAlignItems(Align::Stretch);
    t.box->grow = true;
    t.st = pdfPane(t.box, *w->popup, file, t.keys);
    w->tabs.push_back(std::move(t));
    showPdfTab(w, static_cast<int>(w->tabs.size()) - 1);
}

void closePdfTab(const std::shared_ptr<PdfWindow>& w, int i) {
    if (w->tabs.size() <= 1) {
        if (w->popup) w->popup->close();
        return;
    }
    Flex* box = w->tabs[static_cast<size_t>(i)].box;
    w->tabs.erase(w->tabs.begin() + i);
    EventLoop::get().defer([w, box] {
        if (*w->alive) w->stack->remove(box);
    });
    showPdfTab(w, std::min(i, static_cast<int>(w->tabs.size()) - 1));
}

} // namespace

void pdfViewer(Flex* card, Popup& popup, const std::string& file) {
    auto w = std::make_shared<PdfWindow>();
    w->popup = &popup;
    card->setPadding(Insets(0))->setSpacing(0);
    w->strip = card->add<Flex>(Flex::Direction::Row);
    w->strip->setSpacing(4)->setAlignItems(Align::Center)->setPadding({12, 6, 12, 0});
    w->stack = card->add<Flex>(Flex::Direction::Column);
    w->stack->setAlignItems(Align::Stretch);
    w->stack->grow = true;
    w->rebuild = [w] {
        auto* s = w->strip;
        s->clearChildren();
        s->setVisible(w->tabs.size() > 1);
        for (size_t i = 0; i < w->tabs.size(); ++i) {
            const bool here = static_cast<int>(i) == w->active;
            const auto& st = w->tabs[i].st;
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({12, 5, 6, 5})->setSpacing(6);
            b->setBackground(here ? "secondary_container" : "surface_container_high", false);
            b->add<Icon>("picture_as_pdf", 16)->setColor(here ? "on_secondary_container" : "error", false);
            b->add<Label>(st->doc ? fs::path(st->path).filename().string() : "Open a PDF")->setSize(13)->setWeight(here ? 600 : 500)->setMaxWidth(180);
            b->givesWay = true;
            b->onClicked = [w, i](uint32_t btn) {
                if (btn == BTN_LEFT) EventLoop::get().defer([w, i] {
                    if (*w->alive) showPdfTab(w, static_cast<int>(i));
                });
            };
            iconButton(b, "close", [w, i] {
                EventLoop::get().defer([w, i] {
                    if (*w->alive) closePdfTab(w, static_cast<int>(i));
                });
            }, 14)->setPadding(Insets(3));
        }
        iconButton(s, "add", [w] {
            EventLoop::get().defer([w] {
                if (*w->alive) addPdfTab(w, "");
            });
        }, 18)->tooltip = "Another PDF (Ctrl+T)";
    };
    popup.onKey = [w](const KeyEvent& e) {
        const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
        if (e.ctrl() && k == 't') return addPdfTab(w, ""), true;
        if (e.ctrl() && k == 'w') return closePdfTab(w, w->active), true;
        if (e.ctrl() && (e.sym == XKB_KEY_Tab || e.sym == XKB_KEY_ISO_Left_Tab)) {
            const int n = static_cast<int>(w->tabs.size());
            showPdfTab(w, ((w->active + (e.shift() || e.sym == XKB_KEY_ISO_Left_Tab ? -1 : 1)) % n + n) % n);
            return true;
        }
        if (w->tabs.empty()) return false;
        auto& t = w->tabs[static_cast<size_t>(w->active)];
        return t.keys && t.keys(e);
    };
    pdfGo = [w](const std::string& f) {
        if (*w->alive && !f.empty()) addPdfTab(w, f);
    };
    struct WKeeper : Node {
        std::shared_ptr<PdfWindow> w;
        explicit WKeeper(std::shared_ptr<PdfWindow> x) : w(std::move(x)) { floating = true; }
        ~WKeeper() override {
            *w->alive = false;
            w->rebuild = nullptr;
        }
        SizeF doMeasure(SizeF) override { return {0, 0}; }
    };
    card->add<WKeeper>(w);
    addPdfTab(w, file);
}

// Settings → PDF
void pdfSettings(Flex* page) {
    auto* g = group(page, "Opening", "From the next PDF you open");
    choiceRow<std::string>(g, "zoom_in", "Zoom", "", "/pdf/zoom", "fit", {{"fit", "Fit the width"}, {"100", "100%"}, {"150", "150%"}});
    switchRow(g, "dark_mode", "Night mode", "Dark pages with light text (N)", "/pdf/night", false);
    switchRow(g, "view_sidebar", "Page thumbnails", "The pages down the side (F9)", "/pdf/thumbs", false);
    auto* e = group(page, "Editing");
    choiceRow<std::string>(e, "save", "Unsaved changes when closing", "", "/pdf/onClose", "copy", {{"copy", "Keep an (edited) copy"}, {"discard", "Let them go"}});
    auto r = row(e, "signature", "Your signature", Config::get().value<nlohmann::json>("/pdf/signature", nlohmann::json::array()).empty() ? "None yet: draw one with the Signature tool" : "Kept for placing on any PDF");
    pillButton(r.row, "Clear", false, [] { Config::get().set("/pdf/signature", nlohmann::json::array()); }, "ink_eraser");
    caption(page, haveCommand("qpdf") ? "Pages can be rotated, moved and deleted (qpdf is installed)" : "Install qpdf to rotate, move and delete pages: sudo pacman -S qpdf");
}

void pdfClosed() {
    pdfGo = nullptr;
}

} // namespace m3::shell::panels
