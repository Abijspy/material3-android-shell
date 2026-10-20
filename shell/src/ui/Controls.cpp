#include "ui/Controls.h"

#include "render/Text.h"

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

Slider* Slider::setIcon(const std::string& icon, float trackHeight) {
    if (icon == m_icon && trackHeight == m_track) return this;
    m_icon = icon;
    m_track = trackHeight;
    requestLayout();
    return this;
}

namespace {
// The handle stands a little proud of the track
float handleHeight(float track) {
    return std::max(kHandleH, track + 8);
}
} // namespace

SizeF Slider::doMeasure(SizeF) {
    // Width comes from the parent (stretch or grow); this is the minimum
    // Large sliders sit in a stack: no extra room beyond the handle
    return {minWidth, handleHeight(m_track) + (m_track > kTrack ? 0 : 8)};
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
    // The icon's square at the track's end is a button of its own
    if (onIconClicked && !m_icon.empty() && x > rect().right() - m_track) {
        onIconClicked();
        return;
    }
    m_dragging = true;
    m_handle.set(2);
    setFromX(x);
}

void Slider::onDrag(float x, float) {
    if (m_dragging) setFromX(x);
}

void Slider::onRelease() {
    if (!m_dragging) return; // the icon was pressed, not the track
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
    const float ty = cy - m_track / 2;
    // Large tracks are rounded rectangles, small ones pills
    const float outer = m_track / 2, inner = m_track > kTrack ? 4 : 2;
    const float kTrack = m_track; // the drawing below uses the instance's track

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
    const float handleH = handleHeight(m_track);
    r.rect({hx - hw / 2, cy - handleH / 2, hw, handleH}, active, Radii(hw / 2));
    // The icon inside the track's end (Android 17): on the fill once it reaches it
    if (!m_icon.empty() && !m_centred) {
        TextStyle st;
        st.family = Theme::get().iconFamily();
        st.icon = true;
        st.fill = 1;
        st.size = std::round(m_track * 0.5f);
        const auto& tex = TextEngine::get().texture(m_icon, st, r.scale());
        const float ix = b.right() - (m_track + tex.logical.width) / 2;
        const bool covered = leftEnd >= ix + tex.logical.width;
        const float s = r.scale();
        r.mask(tex.id, {std::round(ix * s) / s, std::round((cy - tex.logical.height / 2) * s) / s, tex.logical.width, tex.logical.height},
               Theme::get().color(covered ? "on_primary" : "on_secondary_container"));
    }
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
    // M3 switch with icons (as Android 16): a full-size thumb carrying ✕
    // when off and ✓ when on
    const float d = 24;
    const float cx = track.x + 16 + (52 - 32) * t;
    const Color thumb = Color::mix(Theme::get().color("outline"), Theme::get().color("on_primary"), t);
    r.rect({cx - d / 2, track.y + 16 - d / 2, d, d}, thumb, Radii(d / 2));
    TextStyle st;
    st.family = Theme::get().iconFamily();
    st.icon = true;
    st.size = 16;
    st.weight = 600;
    const bool checked = t >= 0.5f;
    const auto& tex = TextEngine::get().texture(checked ? "check" : "close", st, r.scale());
    const float s = r.scale();
    r.mask(tex.id,
           {std::round((cx - tex.logical.width / 2) * s) / s, std::round((track.y + 16 - tex.logical.height / 2) * s) / s,
            tex.logical.width, tex.logical.height},
           Theme::get().color(checked ? "on_primary_container" : "surface_container_highest"));
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
    if (c.h > cap) c = content()->measure({available.w - gutter(), 1e6f});
    m_contentHeight = c.h;
    return {c.w + (c.h > cap ? gutter() : 0), std::min(c.h, cap)};
}

float ScrollView::maxOffset() const {
    return std::max(0.0f, m_contentHeight - rect().h);
}

void ScrollView::scrollTo(float y, bool animate) {
    m_offset.set(std::clamp(y, 0.0f, maxOffset()), animate);
    // Only the content moves: re-place it here (cheap, sizes are
    // remembered) instead of laying out the whole surface every frame
    placeContent();
    requestRedraw();
}

void ScrollView::placeContent() {
    if (!content() || rect().w <= 0) return;
    const float off = std::clamp(m_offset.value(), 0.0f, maxOffset());
    m_placedOffset = off;
    content()->arrange({rect().x, rect().y - off, rect().w - (overflows() ? gutter() : 0), m_contentHeight});
}

bool ScrollView::onScroll(int d) {
    if (maxOffset() <= 0) return false;
    const auto& t = Theme::get();
    m_offset.setDuration(kWheelMs);
    scrollTo(m_offset.target() + static_cast<float>(d) * t.scrollStep(), t.smoothScroll());
    return true;
}

bool ScrollView::onScrollPixels(float dy, bool precise) {
    if (maxOffset() <= 0) return false;
    if (!precise) {
        // A wheel notch (15 units) is about 60 px, eased from wherever the
        // last step has got to
        m_velocity = 0;
        const auto& t = Theme::get();
        m_offset.setDuration(kWheelMs);
        scrollTo(m_offset.target() + dy * t.scrollStep() / 16, t.smoothScroll());
        return true;
    }
    // Touchpad: follow the fingers exactly, and remember how fast they go
    const auto now = AnimClock::now();
    const float dt = std::chrono::duration<float, std::milli>(now - m_lastPixels).count();
    m_lastPixels = now;
    const float speed = dy / std::clamp(dt, 4.0f, 50.0f);
    m_velocity = dt > 80 ? speed : 0.6f * speed + 0.4f * m_velocity;
    scrollTo(m_offset.value() + dy, false);
    return true;
}

void ScrollView::onScrollEnd() {
    // Fingers lifted while moving: glide on and slow to a stop
    const float idle = std::chrono::duration<float, std::milli>(AnimClock::now() - m_lastPixels).count();
    const float glide = Theme::get().scrollGlide();
    if (std::abs(m_velocity) < 0.25f || idle > 60 || maxOffset() <= 0 || glide <= 0) {
        m_velocity = 0;
        return;
    }
    const float distance = std::clamp(m_velocity * 320.0f * glide, -2400.0f * glide, 2400.0f * glide);
    m_velocity = 0;
    m_offset.setDuration(std::clamp(static_cast<int>(std::abs(distance) * 0.9f), 350, 900));
    scrollTo(m_offset.value() + distance);
}

void ScrollView::doArrange() {
    if (!content()) return;
    SizeF c = content()->measure({rect().w, 1e6f});
    const bool over = c.h > rect().h + 0.5f;
    if (over) c = content()->measure({rect().w - gutter(), 1e6f});
    m_contentHeight = c.h;
    placeContent();
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
    // While the offset eases, follow it before the content paints
    if (m_offset.running() || m_offset.value() != m_placedOffset) {
        placeContent();
        if (m_offset.running()) requestRedraw();
    }
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

void ScrollView::warmText(Renderer& r) {
    // Rows below and above the view: their text drawn ahead, nearest first,
    // a few milliseconds a frame, so scrolling never stops to draw it
    if (!content()) return;
    if (content() != m_warmedFor || m_contentHeight != m_warmedHeight || r.scale() != m_warmedScale) {
        m_warmedFor = content();
        m_warmedHeight = m_contentHeight;
        m_warmedScale = r.scale();
        m_warmAt = 0;
        std::vector<std::pair<float, const Label*>> found;
        const float middle = rect().y + rect().h / 2;
        std::function<void(const Node&)> walk = [&](const Node& n) {
            if (!n.shown()) return;
            if (const auto* l = dynamic_cast<const Label*>(&n); l && !l->text().empty())
                found.push_back({std::abs(l->rect().y - middle), l});
            for (const auto& c : n.children()) walk(*c);
        };
        walk(*content());
        std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first < b.first; });
        m_warm.clear();
        for (const auto& [_, l] : found) m_warm.push_back({l->text(), l->textStyle()});
    }
    const auto start = AnimClock::now();
    auto& text = TextEngine::get();
    while (m_warmAt < m_warm.size() && AnimClock::now() - start < std::chrono::milliseconds(3)) {
        text.texture(m_warm[m_warmAt].first, m_warm[m_warmAt].second, r.scale());
        ++m_warmAt;
    }
    if (m_warmAt < m_warm.size()) requestRedraw();
}

void ScrollView::doPaintOver(Renderer& r) {
    if (overflows()) warmText(r);
    if (!blurEdges || !overflows()) return;
    const RectF& b = rect();
    constexpr float edge = 40, radius = 10;
    // Each edge blurs in as content passes it, so a page at rest at the
    // top is sharp there
    const float off = std::clamp(m_offset.value(), 0.0f, maxOffset());
    const float above = std::min(off / edge, 1.0f), below = std::min((maxOffset() - off) / edge, 1.0f);
    const float w = b.w - gutter(); // the scrollbar stays sharp
    if (above > 0) r.blurEdge({b.x, b.y, w, edge}, radius * above, true);
    if (below > 0) r.blurEdge({b.x, b.bottom() - edge, w, edge}, radius * below, false);
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
    const float local = x - rect().x - (bare ? 4 : 20) + m_scroll;
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
    if (!bare)
        r.rect(b, Theme::get().color("surface_container_highest"), Radii(b.h / 2), m_focused ? 2 : 0,
               Theme::get().color("primary"));

    const float padX = bare ? 4 : 20;
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

// ── Marquee ──

Marquee::Marquee(float maxWidth, std::string text) : m_max(maxWidth), m_start(std::chrono::steady_clock::now()) {
    m_label = add<Label>(std::move(text));
}

Marquee::~Marquee() {
    if (m_wake) EventLoop::get().cancel(m_wake);
}

const std::string& Marquee::text() const {
    return m_label->text();
}

Marquee* Marquee::setText(const std::string& text) {
    if (text == m_label->text()) return this;
    m_label->setText(text);
    m_start = std::chrono::steady_clock::now(); // a new text starts from its beginning
    requestLayout();
    return this;
}

Marquee* Marquee::setMaxWidth(float w) {
    m_max = w;
    requestLayout();
    return this;
}

SizeF Marquee::doMeasure(SizeF available) {
    const SizeF s = m_label->measure({1e6f, available.h});
    m_natural = s.w;
    return {std::min({s.w, m_max, available.w}), s.h};
}

void Marquee::doArrange() {
    // The label at its full width; the overflow is clipped and scrolled
    m_label->arrange({rect().x, rect().y, std::max(m_natural, rect().w), rect().h});
}

namespace {
constexpr float kMarqueeGap = 40;    // px between the end and the beginning again (Loop)
constexpr double kMarqueePause = 2;  // s at the start of each round, and at the end (Bounce)
} // namespace

Marquee::Mode Marquee::modeOf(const std::string& name) {
    return name == "bounce" ? Mode::Bounce : name == "off" ? Mode::Off : Mode::Loop;
}

Marquee* Marquee::setMode(Mode mode) {
    if (mode == m_mode) return this;
    m_mode = mode;
    m_start = std::chrono::steady_clock::now();
    requestRedraw();
    return this;
}

Marquee* Marquee::setSpeed(float pxPerSecond) {
    m_speed = std::max(5.0f, pxPerSecond);
    requestRedraw();
    return this;
}

void Marquee::doPaint(Renderer&) {
    const float over = m_natural - rect().w;
    if (over <= 0.5f || m_mode == Mode::Off) {
        m_offset = 0;
        m_label->setTranslateX(0, false);
        return;
    }
    const double speed = m_speed, t0 = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
    double offset = 0, wait = 0;
    if (m_mode == Mode::Loop) {
        // A round: a pause, then the text and the gap go by
        const double travel = m_natural + kMarqueeGap, cycle = kMarqueePause + travel / speed, t = std::fmod(t0, cycle);
        if (t < kMarqueePause) wait = kMarqueePause - t;
        else offset = (t - kMarqueePause) * speed;
    } else {
        // Pause, to the end, pause, back
        const double run = over / speed, cycle = 2 * (kMarqueePause + run), t = std::fmod(t0, cycle);
        if (t < kMarqueePause) wait = kMarqueePause - t;
        else if (t < kMarqueePause + run) offset = (t - kMarqueePause) * speed;
        else if (t < 2 * kMarqueePause + run) offset = over, wait = 2 * kMarqueePause + run - t;
        else offset = over - (t - 2 * kMarqueePause - run) * speed;
    }
    m_offset = -static_cast<float>(std::round(offset));
    m_label->setTranslateX(m_offset, false);
    if (wait <= 0) return requestRedraw();
    // Still: wake when it moves again, no frames in between
    if (!m_wake)
        m_wake = EventLoop::get().addTimer(static_cast<int>(wait * 1000) + 16, [this] {
            m_wake = 0;
            requestRedraw();
        });
}

void Marquee::doPaintOver(Renderer& r) {
    if (m_mode != Mode::Loop || m_natural - rect().w <= 0.5f) return;
    // The beginning again, one text and a gap along: the loop is seamless.
    // Moved by the label's own offset (culling looks at its layout place)
    m_label->setTranslateX(m_offset + m_natural + kMarqueeGap, false);
    m_label->paint(r);
    m_label->setTranslateX(m_offset, false);
}

} // namespace m3::ui
