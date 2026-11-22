#include "ui/TextArea.h"

#include "core/Process.h"
#include "render/Renderer.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::ui {

namespace {
constexpr float kPadY = 4;

bool wordChar(gunichar c) {
    return g_unichar_isalnum(c) || c == '_' || c == '\'';
}
} // namespace

TextArea::TextArea() = default;

TextArea::~TextArea() {
    *m_alive = false;
    if (m_blink) EventLoop::get().cancel(m_blink);
    if (m_layout) g_object_unref(m_layout);
    if (m_ctx) g_object_unref(m_ctx);
    if (m_fontMap) g_object_unref(m_fontMap);
    if (m_tex) {
        Renderer::get().makeCurrent();
        glDeleteTextures(1, &m_tex);
    }
}

TextArea* TextArea::setText(const std::string& t) {
    if (t == m_text) return this;
    m_text = t;
    m_cursor = m_anchor = std::min(m_cursor, m_text.size());
    m_undo.clear(), m_redo.clear();
    m_dirty = true;
    requestRedraw();
    return this;
}
TextArea* TextArea::setPlaceholder(const std::string& p) {
    m_placeholder = p;
    m_dirty = true;
    return this;
}
TextArea* TextArea::setSize(float px) {
    m_size = px;
    m_layoutWidth = -1;
    m_dirty = true;
    return this;
}
TextArea* TextArea::setMinLines(int n) {
    m_minLines = n;
    return this;
}

void TextArea::layoutFor(float width, float scale) {
    if (!m_fontMap) {
        m_fontMap = pango_cairo_font_map_new();
        m_ctx = pango_font_map_create_context(m_fontMap);
        m_layout = pango_layout_new(m_ctx);
        pango_layout_set_wrap(m_layout, PANGO_WRAP_WORD_CHAR);
    }
    if (width == m_layoutWidth && scale == m_layoutScale && !m_dirty) return;
    PangoFontDescription* d = pango_font_description_from_string(Theme::get().fontFamily().c_str());
    pango_font_description_set_absolute_size(d, m_size * Theme::get().textScale() * scale * PANGO_SCALE);
    pango_layout_set_font_description(m_layout, d);
    pango_font_description_free(d);
    pango_layout_set_width(m_layout, static_cast<int>(std::max(10.0f, width * scale) * PANGO_SCALE));
    pango_layout_set_spacing(m_layout, static_cast<int>(m_size * 0.3f * scale * PANGO_SCALE));
    pango_layout_set_text(m_layout, m_text.c_str(), static_cast<int>(m_text.size()));
    int lw = 0, lh = 0;
    pango_layout_get_pixel_size(m_layout, &lw, &lh);
    const float lineH = m_size * Theme::get().textScale() * 1.55f;
    m_height = std::max(lh / scale, lineH * static_cast<float>(m_minLines)) + 2 * kPadY;
    m_layoutWidth = width, m_layoutScale = scale;
}

SizeF TextArea::doMeasure(SizeF available) {
    const float w = available.w > 0 && available.w < 1e5f ? available.w : 400;
    const float scale = m_layoutScale > 0 ? m_layoutScale : 1;
    if (w != m_layoutWidth || m_dirty) {
        const bool dirty = m_dirty;
        layoutFor(w, scale);
        m_dirty = dirty; // the texture is made again when painted
    }
    return {w, m_height};
}

RectF TextArea::caretRect(size_t index) const {
    if (!m_layout || m_layoutScale <= 0) return {rect().x, rect().y + kPadY, 2, m_size * 1.3f};
    PangoRectangle strong;
    pango_layout_get_cursor_pos(m_layout, static_cast<int>(index), &strong, nullptr);
    const float s = m_layoutScale;
    return {rect().x + strong.x / static_cast<float>(PANGO_SCALE) / s, rect().y + kPadY + strong.y / static_cast<float>(PANGO_SCALE) / s, 2,
            std::max(8.0f, strong.height / static_cast<float>(PANGO_SCALE) / s)};
}

size_t TextArea::indexAt(float x, float y) {
    if (!m_layout || m_layoutScale <= 0) return m_text.size();
    int index = 0, trailing = 0;
    const float s = m_layoutScale;
    pango_layout_xy_to_index(m_layout, static_cast<int>((x - rect().x) * s * PANGO_SCALE), static_cast<int>((y - rect().y - kPadY) * s * PANGO_SCALE), &index, &trailing);
    const char* p = m_text.c_str() + index;
    for (int i = 0; i < trailing && *p; ++i) p = g_utf8_next_char(p);
    return static_cast<size_t>(p - m_text.c_str());
}

void TextArea::doPaint(Renderer& r) {
    const auto& b = rect();
    const float scale = r.scale();
    if (scale != m_layoutScale || b.w != m_layoutWidth) m_dirty = true;
    if (m_dirty) {
        layoutFor(b.w, scale);
        m_dirty = false;
        const int w = std::max(1, static_cast<int>(std::ceil(b.w * scale))), h = std::max(1, static_cast<int>(std::ceil(m_height * scale)));
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surf);
        const bool empty = m_text.empty();
        if (empty && !m_placeholder.empty()) pango_layout_set_text(m_layout, m_placeholder.c_str(), -1);
        // The selection behind the text
        if (hasSelection() && !empty) {
            const Color sel = Theme::get().color("primary");
            const double selAlpha = m_focused ? 0.42 : 0.24;
            const int a = static_cast<int>(std::min(m_cursor, m_anchor)), z = static_cast<int>(std::max(m_cursor, m_anchor));
            const int lines = pango_layout_get_line_count(m_layout);
            for (int i = 0; i < lines; ++i) {
                PangoLayoutLine* line = pango_layout_get_line_readonly(m_layout, i);
                const int ls = line->start_index, le = line->start_index + line->length;
                if (z < ls || a > le) continue;
                int* ranges = nullptr;
                int n = 0;
                pango_layout_line_get_x_ranges(line, std::max(a, ls), std::min(z, le), &ranges, &n);
                PangoRectangle logical;
                pango_layout_index_to_pos(m_layout, ls, &logical);
                for (int k = 0; k < n; ++k) {
                    const double x0 = ranges[2 * k] / static_cast<double>(PANGO_SCALE), x1 = ranges[2 * k + 1] / static_cast<double>(PANGO_SCALE);
                    cairo_rectangle(cr, x0, kPadY * scale + logical.y / static_cast<double>(PANGO_SCALE), std::max(3.0, x1 - x0),
                                    logical.height / static_cast<double>(PANGO_SCALE));
                }
                g_free(ranges);
                cairo_set_source_rgba(cr, sel.r, sel.g, sel.b, selAlpha); // straight colour: cairo premultiplies
                cairo_fill(cr);
            }
        }
        const Color ink = Theme::get().color(empty ? "on_surface_variant" : "on_surface");
        cairo_set_source_rgba(cr, ink.r, ink.g, ink.b, empty ? 0.7 : 1.0);
        cairo_move_to(cr, 0, kPadY * scale);
        pango_cairo_update_layout(cr, m_layout);
        pango_cairo_show_layout(cr, m_layout);
        if (empty) pango_layout_set_text(m_layout, m_text.c_str(), static_cast<int>(m_text.size()));
        cairo_destroy(cr);
        cairo_surface_flush(surf);
        if (!m_tex) glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surf));
        cairo_surface_destroy(surf);
        m_texW = w, m_texH = h;
    }
    r.image(m_tex, {b.x, b.y, m_texW / scale, m_texH / scale});
    if (m_focused && m_caretOn && !hasSelection()) r.rect(caretRect(m_cursor), Theme::get().color("primary"), Radii(1));
}

void TextArea::onFocus(bool f) {
    m_focused = f;
    m_dirty = true; // the selection's shade
    if (f) restartBlink();
    else if (m_blink) EventLoop::get().cancel(m_blink), m_blink = 0;
    requestRedraw();
}

void TextArea::restartBlink() {
    m_caretOn = true;
    if (m_blink) EventLoop::get().cancel(m_blink);
    m_blink = EventLoop::get().addTimer(530, [this, alive = m_alive] {
        if (!*alive) return;
        m_caretOn = !m_caretOn;
        requestRedraw();
    }, true);
}

void TextArea::reveal() {
    const RectF c = caretRect(m_cursor);
    for (Node* p = parentNode(); p; p = p->parentNode())
        if (auto* scroll = dynamic_cast<ScrollView*>(p)) {
            scroll->reveal({c.x, c.y - 12, c.w, c.h + 24});
            break;
        }
}

void TextArea::moveTo(size_t index, bool extend) {
    m_cursor = std::min(index, m_text.size());
    if (!extend) {
        if (hasSelection()) m_dirty = true;
        m_anchor = m_cursor;
    } else {
        m_dirty = true;
    }
    restartBlink();
    reveal();
    requestRedraw();
}

std::string TextArea::selected() const {
    const size_t a = std::min(m_cursor, m_anchor), z = std::max(m_cursor, m_anchor);
    return m_text.substr(a, z - a);
}

void TextArea::edited(bool snapshot) {
    (void)snapshot;
    m_dirty = true;
    m_layoutWidth = -1;
    restartBlink();
    if (onChanged) onChanged(m_text);
    // The new height reaches the layout, then the caret is followed
    requestRedraw();
    EventLoop::get().defer([this, alive = m_alive] {
        if (*alive) reveal();
    });
}

void TextArea::eraseSelection() {
    if (!hasSelection()) return;
    const size_t a = std::min(m_cursor, m_anchor), z = std::max(m_cursor, m_anchor);
    m_text.erase(a, z - a);
    m_cursor = m_anchor = a;
}

void TextArea::insert(const std::string& in) {
    std::string s = in;
    if (singleLine) std::ranges::replace(s, '\n', ' ');
    // Typing in a run is one step to undo; a pause or a jump starts another
    const auto now = EventLoop::Clock::now();
    if (m_undo.empty() || now - m_lastEdit > std::chrono::milliseconds(900) || s.size() > 1 || s == " " || s == "\n") m_undo.push_back({m_text, m_cursor});
    if (m_undo.size() > 200) m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_lastEdit = now;
    eraseSelection();
    m_text.insert(m_cursor, s);
    m_cursor += s.size();
    m_anchor = m_cursor;
    edited(true);
}

size_t TextArea::lineMove(int dir) const {
    if (!m_layout) return m_cursor;
    int line = 0, x = 0;
    pango_layout_index_to_line_x(m_layout, static_cast<int>(m_cursor), 0, &line, &x);
    const int target = line + dir;
    if (target < 0) return 0;
    if (target >= pango_layout_get_line_count(m_layout)) return m_text.size();
    PangoLayoutLine* l = pango_layout_get_line_readonly(m_layout, target);
    int index = 0, trailing = 0;
    pango_layout_line_x_to_index(l, x, &index, &trailing);
    const char* p = m_text.c_str() + index;
    for (int i = 0; i < trailing && *p; ++i) p = g_utf8_next_char(p);
    size_t out = static_cast<size_t>(p - m_text.c_str());
    // Not past a wrapped line's end onto the next
    if (out > static_cast<size_t>(l->start_index + l->length)) out = static_cast<size_t>(l->start_index + l->length);
    return out;
}

size_t TextArea::lineEdge(bool end) const {
    if (!m_layout) return end ? m_text.size() : 0;
    int line = 0, x = 0;
    pango_layout_index_to_line_x(m_layout, static_cast<int>(m_cursor), 0, &line, &x);
    PangoLayoutLine* l = pango_layout_get_line_readonly(m_layout, line);
    if (!end) return static_cast<size_t>(l->start_index);
    size_t e = static_cast<size_t>(l->start_index + l->length);
    if (e > 0 && e <= m_text.size() && m_text[e - 1] == '\n') --e;
    return e;
}

size_t TextArea::wordEdge(size_t from, bool forward) const {
    const char* s = m_text.c_str();
    const char* p = s + from;
    if (forward) {
        while (*p && !wordChar(g_utf8_get_char(p))) p = g_utf8_next_char(p);
        while (*p && wordChar(g_utf8_get_char(p))) p = g_utf8_next_char(p);
    } else {
        while (p > s && !wordChar(g_utf8_get_char(g_utf8_prev_char(p)))) p = g_utf8_prev_char(p);
        while (p > s && wordChar(g_utf8_get_char(g_utf8_prev_char(p)))) p = g_utf8_prev_char(p);
    }
    return static_cast<size_t>(p - s);
}

bool TextArea::onKey(const KeyEvent& e) {
    const bool ctrl = e.ctrl(), shift = e.shift();
    const char* s = m_text.c_str();
    auto prev = [&](size_t i) { return i == 0 ? 0 : static_cast<size_t>(g_utf8_prev_char(s + i) - s); };
    auto next = [&](size_t i) { return i >= m_text.size() ? m_text.size() : static_cast<size_t>(g_utf8_next_char(s + i) - s); };
    switch (e.sym) {
    case XKB_KEY_Left:
        if (hasSelection() && !shift) return moveTo(std::min(m_cursor, m_anchor), false), true;
        moveTo(ctrl ? wordEdge(m_cursor, false) : prev(m_cursor), shift);
        return true;
    case XKB_KEY_Right:
        if (hasSelection() && !shift) return moveTo(std::max(m_cursor, m_anchor), false), true;
        moveTo(ctrl ? wordEdge(m_cursor, true) : next(m_cursor), shift);
        return true;
    case XKB_KEY_Up: moveTo(lineMove(-1), shift); return true;
    case XKB_KEY_Down: moveTo(lineMove(1), shift); return true;
    case XKB_KEY_Home: moveTo(ctrl ? 0 : lineEdge(false), shift); return true;
    case XKB_KEY_End: moveTo(ctrl ? m_text.size() : lineEdge(true), shift); return true;
    case XKB_KEY_Page_Up: moveTo(lineMove(-12), shift); return true;
    case XKB_KEY_Page_Down: moveTo(lineMove(12), shift); return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (ctrl || singleLine) break;
        insert("\n");
        return true;
    case XKB_KEY_Tab:
        if (ctrl || singleLine) break;
        insert("\t");
        return true;
    case XKB_KEY_BackSpace:
        if (!hasSelection()) m_anchor = ctrl ? wordEdge(m_cursor, false) : prev(m_cursor);
        if (hasSelection()) {
            m_undo.push_back({m_text, std::max(m_cursor, m_anchor)});
            m_redo.clear();
            eraseSelection();
            edited(true);
        }
        return true;
    case XKB_KEY_Delete:
        if (!hasSelection()) m_anchor = ctrl ? wordEdge(m_cursor, true) : next(m_cursor);
        if (hasSelection()) {
            m_undo.push_back({m_text, m_cursor});
            m_redo.clear();
            eraseSelection();
            edited(true);
        }
        return true;
    default: break;
    }
    if (ctrl) {
        const uint32_t k = e.baseSym >= 'A' && e.baseSym <= 'Z' ? e.baseSym - 'A' + 'a' : e.baseSym;
        switch (k) {
        case 'a':
            m_anchor = 0;
            moveTo(m_text.size(), true);
            return true;
        case 'c':
            if (hasSelection()) run({"wl-copy"}, nullptr, selected());
            return true;
        case 'x':
            if (hasSelection()) {
                run({"wl-copy"}, nullptr, selected());
                m_undo.push_back({m_text, m_cursor});
                eraseSelection();
                edited(true);
            }
            return true;
        case 'v':
            run({"wl-paste", "--no-newline", "--type", "text"}, [this, alive = m_alive](int code, const std::string& out) {
                if (*alive && code == 0 && !out.empty()) insert(out);
            });
            return true;
        case 'z':
            if (shift) {
                if (m_redo.empty()) return true;
                m_undo.push_back({m_text, m_cursor});
                auto [t, c] = m_redo.back();
                m_redo.pop_back();
                m_text = t, m_cursor = m_anchor = std::min(c, t.size());
            } else {
                if (m_undo.empty()) return true;
                m_redo.push_back({m_text, m_cursor});
                auto [t, c] = m_undo.back();
                m_undo.pop_back();
                m_text = t, m_cursor = m_anchor = std::min(c, t.size());
            }
            edited(false);
            return true;
        case 'y':
            if (m_redo.empty()) return true;
            m_undo.push_back({m_text, m_cursor});
            {
                auto [t, c] = m_redo.back();
                m_redo.pop_back();
                m_text = t, m_cursor = m_anchor = std::min(c, t.size());
            }
            edited(false);
            return true;
        default: break;
        }
        return onOtherKey && onOtherKey(e);
    }
    if (!e.text.empty() && static_cast<unsigned char>(e.text[0]) >= 0x20 && e.text[0] != 0x7f) {
        insert(e.text);
        return true;
    }
    return onOtherKey && onOtherKey(e);
}

void TextArea::onPress(uint32_t button, float x, float y) {
    focus();
    if (button != BTN_LEFT) return;
    const size_t i = indexAt(x, y);
    const auto now = EventLoop::Clock::now();
    const bool dbl = now - m_lastPress < std::chrono::milliseconds(400);
    m_lastPress = now;
    if (dbl) {
        // A word
        m_anchor = wordEdge(i, false);
        size_t end = i;
        const char* s = m_text.c_str();
        const char* p = s + end;
        while (*p && wordChar(g_utf8_get_char(p))) p = g_utf8_next_char(p);
        end = static_cast<size_t>(p - s);
        if (m_anchor > i) m_anchor = i;
        m_cursor = end;
        m_dirty = true;
        requestRedraw();
        return;
    }
    moveTo(i, false);
    m_dragging = true;
}

void TextArea::onDrag(float x, float y) {
    if (!m_dragging) return;
    moveTo(indexAt(x, y), true);
}

void TextArea::onRelease() {
    m_dragging = false;
    if (hasSelection()) run({"wl-copy", "--primary"}, nullptr, selected());
}

} // namespace m3::ui
