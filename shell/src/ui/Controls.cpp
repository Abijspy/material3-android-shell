#include "ui/Controls.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::ui {

// ── Slider ──

namespace {
constexpr float kTrack = 16;     // M3 "XS" slider track
constexpr float kHandleH = 32;
constexpr float kGap = 4;        // space either side of the handle
} // namespace

Slider::Slider() {
    minWidth = 120;
}

Slider* Slider::setRange(float min, float max) {
    m_min = min;
    m_max = max;
    return setValue(m_value, false);
}

Slider* Slider::setValue(float v, bool animate) {
    if (m_dragging) return this; // the user owns it while dragging
    m_value = std::clamp(v, m_min, m_max);
    m_shown.set(fraction(m_value), animate);
    requestRedraw();
    return this;
}

Slider* Slider::setCentred(bool c) {
    m_centred = c;
    requestRedraw();
    return this;
}

Slider* Slider::setStep(float s) {
    m_step = s;
    return this;
}

SizeF Slider::doMeasure(SizeF) {
    // Width comes from the parent (stretch or grow); this is the minimum
    return {minWidth, kHandleH + 8};
}

void Slider::change(float v, bool commit) {
    v = std::clamp(v, m_min, m_max);
    const bool moved = v != m_value;
    m_value = v;
    m_shown.set(fraction(v), !m_dragging);
    requestRedraw();
    if (moved && onChanged) onChanged(v);
    if (commit && onCommitted) onCommitted(v);
}

void Slider::setFromX(float x) {
    const float f = std::clamp((x - rect().x) / rect().w, 0.0f, 1.0f);
    change(m_min + f * (m_max - m_min), false);
}

void Slider::onHover(bool) {}

void Slider::onPress(uint32_t, float x, float) {
    m_dragging = true;
    m_handle.set(2);
    setFromX(x);
}

void Slider::onDrag(float x, float) {
    if (m_dragging) setFromX(x);
}

void Slider::onRelease() {
    m_dragging = false;
    m_handle.set(4);
    change(m_value, true);
}

bool Slider::onScroll(int d) {
    change(m_value - static_cast<float>(d) * m_step * (m_max - m_min), true);
    return true;
}

bool Slider::onKey(const KeyEvent& e) {
    const float step = m_step * (m_max - m_min);
    if (e.sym == XKB_KEY_Left || e.sym == XKB_KEY_Down) change(m_value - step, true);
    else if (e.sym == XKB_KEY_Right || e.sym == XKB_KEY_Up) change(m_value + step, true);
    else if (e.sym == XKB_KEY_Home) change(m_min, true);
    else if (e.sym == XKB_KEY_End) change(m_max, true);
    else return false;
    return true;
}

void Slider::doPaint(Renderer& r) {
    const auto& b = rect();
    const float cy = b.y + b.h / 2;
    const float f = std::clamp(m_shown.value(), 0.0f, 1.0f);
    const float hx = b.x + f * b.w;
    const float hw = m_handle.value();
    const Color active = Theme::get().color("primary");
    const Color inactive = Theme::get().color("secondary_container");
    const float ty = cy - kTrack / 2;
    const float outer = kTrack / 2, inner = 2;

    const float leftEnd = hx - hw / 2 - kGap;
    const float rightStart = hx + hw / 2 + kGap;
    if (m_centred) {
        // Inactive everywhere, active between the centre and the handle
        const float mid = b.x + b.w / 2;
        if (leftEnd > b.x) r.rect({b.x, ty, leftEnd - b.x, kTrack}, inactive, Radii(outer, inner, inner, outer));
        if (rightStart < b.right()) r.rect({rightStart, ty, b.right() - rightStart, kTrack}, inactive, Radii(inner, outer, outer, inner));
        const float a = std::min(mid, hx), z = std::max(mid, hx);
        const float from = a == hx ? rightStart : a, to = z == hx ? leftEnd : z;
        if (to - from > 1) r.rect({from, ty, to - from, kTrack}, active, Radii(inner));
        r.rect({mid - 1, cy - 6, 2, 12}, Theme::get().color("on_secondary_container"), Radii(1));
    } else {
        if (leftEnd > b.x) r.rect({b.x, ty, leftEnd - b.x, kTrack}, active, Radii(outer, inner, inner, outer));
        if (rightStart < b.right()) r.rect({rightStart, ty, b.right() - rightStart, kTrack}, inactive, Radii(inner, outer, outer, inner));
        // M3 stop indicator at the end of the track, until the fill reaches it
        if (rightStart < b.right() - 10)
            r.rect({b.right() - 8, cy - 2, 4, 4}, Theme::get().color("on_secondary_container"), Radii(2));
    }
    r.rect({hx - hw / 2, cy - kHandleH / 2, hw, kHandleH}, active, Radii(hw / 2));
    if (m_shown.running() || m_handle.running()) requestRedraw();
}

// ── Switch ──

Switch::Switch() = default;

Switch* Switch::setChecked(bool c, bool animate) {
    m_checked = c;
    m_t.set(c ? 1 : 0, animate);
    requestRedraw();
    return this;
}

void Switch::onClick(uint32_t) {
    setChecked(!m_checked);
    if (onToggled) onToggled(m_checked);
}

void Switch::doPaint(Renderer& r) {
    const auto& b = rect();
    const float t = m_t.value();
    const RectF track{b.x, b.y + (b.h - 32) / 2, 52, 32};
    const Color on = Theme::get().color("primary");
    const Color off = Theme::get().color("surface_container_highest");
    const Color border = Theme::get().color("outline");
    r.rect(track, Color::mix(off, on, t), Radii(16), 2 * (1 - t), border);
    const float d = 16 + 8 * t; // thumb grows 16 → 24
    const float cx = track.x + 16 + (52 - 32) * t;
    const Color thumb = Color::mix(Theme::get().color("outline"), Theme::get().color("on_primary"), t);
    r.rect({cx - d / 2, track.y + 16 - d / 2, d, d}, thumb, Radii(d / 2));
    if (m_t.running()) requestRedraw();
}

// ── ScrollView ──

SizeF ScrollView::doMeasure(SizeF available) {
    if (!content()) return {};
    // Never taller than the room we are given: small screens scroll instead
    // of cutting the content off
    const float cap = available.h > 48 ? std::min(maxHeight, available.h) : maxHeight;
    SizeF c = content()->measure({available.w, 1e6f});
    // Overflowing: the content gives up a gutter for the scrollbar
    if (c.h > cap) c = content()->measure({available.w - kGutter, 1e6f});
    m_contentHeight = c.h;
    return {c.w + (c.h > cap ? kGutter : 0), std::min(c.h, cap)};
}

float ScrollView::maxOffset() const {
    return std::max(0.0f, m_contentHeight - rect().h);
}

void ScrollView::scrollTo(float y, bool animate) {
    m_offset.set(std::clamp(y, 0.0f, maxOffset()), animate);
    requestLayout();
}

bool ScrollView::onScroll(int d) {
    if (maxOffset() <= 0) return false;
    scrollTo(m_offset.target() + static_cast<float>(d) * 64);
    return true;
}

void ScrollView::doArrange() {
    if (!content()) return;
    SizeF c = content()->measure({rect().w, 1e6f});
    const bool over = c.h > rect().h + 0.5f;
    if (over) c = content()->measure({rect().w - kGutter, 1e6f});
    m_contentHeight = c.h;
    const float off = std::clamp(m_offset.value(), 0.0f, maxOffset());
    content()->arrange({rect().x, rect().y - off, rect().w - (over ? kGutter : 0), c.h});
    if (m_offset.running()) requestLayout();
}

RectF ScrollView::thumb() const {
    const RectF& r = rect();
    const float track = r.h - 8;
    const float h = std::max(32.0f, track * r.h / std::max(m_contentHeight, 1.0f));
    const float max = maxOffset();
    const float t = max > 0 ? std::clamp(m_offset.value(), 0.0f, max) / max : 0;
    const float w = 4 + 4 * m_bar.value();
    return {r.right() - 5 - w, r.y + 4 + (track - h) * t, w, h};
}

void ScrollView::doPaint(Renderer& r) {
    if (!overflows()) return;
    const float k = m_bar.value();
    // Track only while engaged; the thumb always shows there is more
    if (k > 0) {
        const RectF& b = rect();
        r.rect({b.right() - 13, b.y + 4, 12, b.h - 8}, ColorRef("on_surface", 0.06f * k).resolve(), Radii(6));
    }
    const RectF t = thumb();
    r.rect(t, ColorRef(k > 0 ? "primary" : "on_surface_variant", 0.45f + 0.4f * k).resolve(), Radii(t.w / 2));
    if (m_bar.running()) requestRedraw();
}

void ScrollView::onHover(bool hovered) {
    m_hovered = hovered;
    if (m_grab < 0) m_bar.set(hovered ? 1 : 0);
    requestRedraw();
}

void ScrollView::onPress(uint32_t, float x, float y) {
    if (x < rect().right() - kGutter) return;
    const RectF t = thumb();
    // On the thumb: drag from where it was grabbed; on the track: jump there
    m_grab = (y >= t.y && y < t.bottom()) ? y - t.y : t.h / 2;
    m_bar.set(1);
    onDrag(x, y);
}

void ScrollView::onDrag(float, float y) {
    if (m_grab < 0) return;
    const float track = rect().h - 8;
    const float h = thumb().h;
    const float t = std::clamp((y - m_grab - rect().y - 4) / std::max(1.0f, track - h), 0.0f, 1.0f);
    scrollTo(t * maxOffset(), false);
}

void ScrollView::onRelease() {
    m_grab = -1;
    m_bar.set(m_hovered ? 1 : 0);
    requestRedraw();
}

// ── TextField ──

TextField::TextField() {
    minWidth = 200;
}

TextField::~TextField() {
    if (m_blink) EventLoop::get().cancel(m_blink);
}

TextField* TextField::setText(const std::string& t) {
    m_text = t;
    m_cursor = t.size();
    requestRedraw();
    return this;
}

TextField* TextField::setSecret(bool secret) {
    m_secret = secret;
    requestRedraw();
    return this;
}

std::string TextField::shownText() const {
    if (!m_secret) return m_text;
    std::string dots;
    for (unsigned char c : m_text)
        if ((c & 0xC0) != 0x80) dots += "\u2022";
    return dots;
}

TextField* TextField::setPlaceholder(const std::string& p) {
    m_placeholder = p;
    requestRedraw();
    return this;
}

TextStyle TextField::style() const {
    TextStyle s;
    s.family = Theme::get().fontFamily();
    s.size = 15;
    return s;
}

SizeF TextField::doMeasure(SizeF) {
    return {minWidth, 48};
}

float TextField::caretX(size_t pos) const {
    if (pos == 0) return 0;
    if (m_secret) {
        size_t chars = 0;
        for (size_t i = 0; i < pos; ++i)
            if ((static_cast<unsigned char>(m_text[i]) & 0xC0) != 0x80) ++chars;
        std::string dots;
        for (size_t i = 0; i < chars; ++i) dots += "\u2022";
        return TextEngine::get().measure(dots, style()).width;
    }
    return TextEngine::get().measure(m_text.substr(0, pos), style()).width;
}

void TextField::onFocus(bool f) {
    m_focused = f;
    if (f) {
        restartBlink();
    } else if (m_blink) {
        EventLoop::get().cancel(m_blink);
        m_blink = 0;
    }
    requestRedraw();
}

void TextField::restartBlink() {
    m_caretOn = true;
    if (m_blink) EventLoop::get().cancel(m_blink);
    m_blink = EventLoop::get().addTimer(530, [this] {
        m_caretOn = !m_caretOn;
        requestRedraw();
    }, true);
}

void TextField::onPress(uint32_t, float x, float) {
    // Put the caret at the nearest character boundary
    const float local = x - rect().x - 20 + m_scroll;
    size_t best = 0;
    float bestDist = std::abs(local);
    for (size_t i = 1; i <= m_text.size(); ++i) {
        if (i < m_text.size() && (static_cast<unsigned char>(m_text[i]) & 0xC0) == 0x80) continue;
        const float d = std::abs(caretX(i) - local);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    m_cursor = best;
    restartBlink();
    requestRedraw();
}

namespace {

size_t prevChar(const std::string& s, size_t i) {
    if (i == 0) return 0;
    do --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}

size_t nextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    do ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
    return i;
}

size_t prevWord(const std::string& s, size_t i) {
    while (i > 0 && s[i - 1] == ' ') --i;
    while (i > 0 && s[i - 1] != ' ') --i;
    return i;
}

} // namespace

void TextField::edited() {
    restartBlink();
    requestRedraw();
    if (onChanged) onChanged(m_text);
}

bool TextField::onKey(const KeyEvent& e) {
    switch (e.sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        if (onSubmit) onSubmit(m_text);
        return true;
    case XKB_KEY_Escape:
        if (onCancel) {
            onCancel();
            return true;
        }
        return false;
    case XKB_KEY_BackSpace: {
        if (m_cursor == 0) return true;
        const size_t from = e.ctrl() ? prevWord(m_text, m_cursor) : prevChar(m_text, m_cursor);
        m_text.erase(from, m_cursor - from);
        m_cursor = from;
        edited();
        return true;
    }
    case XKB_KEY_Delete:
        if (m_cursor < m_text.size()) {
            m_text.erase(m_cursor, nextChar(m_text, m_cursor) - m_cursor);
            edited();
        }
        return true;
    case XKB_KEY_Left:
        m_cursor = e.ctrl() ? prevWord(m_text, m_cursor) : prevChar(m_text, m_cursor);
        restartBlink();
        requestRedraw();
        return true;
    case XKB_KEY_Right:
        m_cursor = nextChar(m_text, m_cursor);
        restartBlink();
        requestRedraw();
        return true;
    case XKB_KEY_Home:
        m_cursor = 0;
        requestRedraw();
        return true;
    case XKB_KEY_End:
        m_cursor = m_text.size();
        requestRedraw();
        return true;
    default:
        break;
    }
    if (e.ctrl() && (e.sym == XKB_KEY_u || e.sym == XKB_KEY_U)) {
        m_text.erase(0, m_cursor);
        m_cursor = 0;
        edited();
        return true;
    }
    if (!e.text.empty() && !e.ctrl() && !(e.mods & ModSuper)) {
        m_text.insert(m_cursor, e.text);
        m_cursor += e.text.size();
        edited();
        return true;
    }
    return false;
}

void TextField::doPaint(Renderer& r) {
    const auto& b = rect();
    r.rect(b, Theme::get().color("surface_container_highest"), Radii(b.h / 2),
           m_focused ? 2 : 0, Theme::get().color("primary"));

    const float padX = 20;
    const RectF inner{b.x + padX, b.y, b.w - padX * 2, b.h};
    const float cx = caretX(m_cursor);
    // Scroll so the caret stays inside the field
    if (cx - m_scroll > inner.w) m_scroll = cx - inner.w;
    if (cx - m_scroll < 0) m_scroll = cx;

    r.pushClip(inner);
    const bool empty = m_text.empty();
    const std::string shown = empty ? m_placeholder : shownText();
    if (!shown.empty()) {
        const auto& tex = TextEngine::get().texture(shown, style(), r.scale());
        const float s = r.scale();
        const float x = std::round((inner.x - (empty ? 0 : m_scroll)) * s) / s;
        const float y = std::round((b.y + (b.h - tex.logical.height) / 2) * s) / s;
        r.mask(tex.id, {x, y, tex.logical.width, tex.logical.height},
               Theme::get().color(empty ? "on_surface_variant" : "on_surface"));
    }
    if (m_focused && m_caretOn) r.rect({inner.x + cx - m_scroll, b.y + 13, 2, b.h - 26}, Theme::get().color("primary"), Radii(1));
    r.popClip();
}

// ── ImageView ──

ImageView::ImageView(float size, Provider provider) : ImageView(size, size, std::move(provider)) {}

ImageView::ImageView(float width, float height, Provider provider)
    : m_width(width), m_height(height), m_provider(std::move(provider)) {}

void ImageView::setProvider(Provider p) {
    m_provider = std::move(p);
    requestRedraw();
}

void ImageView::doPaint(Renderer& r) {
    if (!m_provider) return;
    int w = 0, h = 0;
    const GLuint tex = m_provider(r.scale(), w, h);
    if (!tex || w <= 0 || h <= 0) return;
    const auto& b = rect();
    const float sx = b.w / static_cast<float>(w), sy = b.h / static_cast<float>(h);
    const float s = cover ? std::max(sx, sy) : std::min(sx, sy);
    const float dw = static_cast<float>(w) * s, dh = static_cast<float>(h) * s;
    if (cover) r.pushClip(b);
    r.image(tex, {b.x + (b.w - dw) / 2, b.y + (b.h - dh) / 2, dw, dh});
    if (cover) r.popClip();
}

} // namespace m3::ui
