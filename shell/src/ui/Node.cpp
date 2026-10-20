#include "ui/Node.h"

#include "ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace m3::ui {

// ── Node ──

Node::~Node() {
    if (auto* h = host()) h->nodeDestroyed(this);
}

void Node::adopt(std::unique_ptr<Node> child) {
    child->m_parent = this;
    m_children.push_back(std::move(child));
    requestLayout();
}

void Node::adopt(std::unique_ptr<Node> child, size_t index) {
    child->m_parent = this;
    m_children.insert(m_children.begin() + static_cast<long>(std::min(index, m_children.size())), std::move(child));
    requestLayout();
}

void Node::remove(Node* child) {
    std::erase_if(m_children, [child](auto& c) { return c.get() == child; });
    requestLayout();
}

void Node::clearChildren() {
    m_children.clear();
    requestLayout();
}

bool Node::shown() const {
    if (!m_visible) return false;
    if (!collapseWhenEmpty) return true;
    return std::any_of(m_children.begin(), m_children.end(), [](auto& c) { return c->shown(); });
}

namespace {
long g_measures = 0;
// Measurements are remembered only between changes: any requestLayout()
// and every layout pass start a new generation, so a size never outlives
// what it was computed from
uint64_t g_generation = 1;
} // namespace

long Node::measureCount() {
    return g_measures;
}

void Node::invalidateMeasures() {
    ++g_generation;
}

SizeF Node::measure(SizeF available) {
    if (!shown()) return {};
    // Containers measure their children while measuring and again while
    // arranging, level by level: without this a deep tree measures its
    // leaves thousands of times per pass
    for (const auto& m : m_measured)
        if (m.generation == g_generation && m.available.w == available.w && m.available.h == available.h) return m.size;
    ++g_measures;
    SizeF s = doMeasure(available);
    if (fixedWidth) s.w = *fixedWidth;
    if (fixedHeight) s.h = *fixedHeight;
    s.w = std::max(s.w, minWidth);
    s.h = std::max(s.h, minHeight);
    // Keep the newest two: overwrite the older slot
    m_measured[m_nextMeasured] = {g_generation, available, s};
    m_nextMeasured ^= 1;
    return s;
}

void Node::arrange(const RectF& rect) {
    m_rect = rect;
    doArrange();
}

SizeF Node::doMeasure(SizeF available) {
    SizeF s;
    for (auto& c : m_children) {
        const auto cs = c->measure(available);
        s.w = std::max(s.w, cs.w);
        s.h = std::max(s.h, cs.h);
    }
    return s;
}

void Node::doArrange() {
    for (auto& c : m_children) c->arrange(m_rect);
}

void Node::paint(Renderer& r) {
    if (!shown() || !r.visible(m_rect)) return;
    const float o = m_opacity.value(), dy = m_translateY.value(), dx = m_translateX.value();
    if (o <= 0.001f) return;
    if (o < 1) r.pushOpacity(o);
    const bool moved = dx != 0 || dy != 0;
    if (moved) r.pushTranslate(dx, dy);
    // On the desktop and lock screen, the card's backdrop blurred in the frame
    if (blurBehind && r.inlineBlur > 0) r.blurBackdrop(m_rect, r.inlineBlur, Radii(cornerRadius()));
    doPaint(r);
    if (clipsChildren()) r.pushClip(m_rect);
    for (auto& c : m_children) c->paint(r);
    doPaintOver(r);
    if (clipsChildren()) r.popClip();
    if (moved) r.popTranslate();
    if (o < 1) r.popOpacity();
    if (m_opacity.running() || m_translateY.running() || m_translateX.running()) requestRedraw();
}

Node* Node::hitTest(float x, float y, bool any) {
    if (!shown() || !m_rect.contains(x, y) || m_opacity.target() <= 0) return nullptr;
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        if (Node* hit = (*it)->hitTest(x, y, any)) return hit;
    return (any || interactive()) ? this : nullptr;
}

void Node::focus() {
    if (auto* h = host()) h->requestFocus(this);
}

void Node::setOpacity(float o, bool animate) {
    m_opacity.set(o, animate);
    requestRedraw();
}

void Node::setTranslateX(float dx, bool animate) {
    m_translateX.set(dx, animate);
    requestRedraw();
}

void Node::setTranslateY(float dy, bool animate) {
    m_translateY.set(dy, animate);
    requestRedraw();
}

bool Node::animating() const {
    if (!shown()) return false;
    if (doAnimating() || m_opacity.running() || m_translateY.running() || m_translateX.running()) return true;
    return std::any_of(m_children.begin(), m_children.end(), [](auto& c) { return c->animating(); });
}

void Node::setVisible(bool v) {
    if (v == m_visible) return;
    m_visible = v;
    requestLayout();
}

Host* Node::host() const {
    const Node* n = this;
    while (n->m_parent) n = n->m_parent;
    return n->m_host;
}

void Node::requestLayout() {
    invalidateMeasures();
    if (auto* h = host()) h->requestLayout();
}

void Node::requestRedraw() {
    if (auto* h = host()) h->requestRedraw();
}

// ── Flex ──

Flex* Flex::setSpacing(float s) {
    m_spacing = s;
    requestLayout();
    return this;
}

Flex* Flex::setPadding(Insets p) {
    m_padding = p;
    requestLayout();
    return this;
}

Flex* Flex::setJustify(Align a) {
    m_justify = a;
    requestLayout();
    return this;
}

Flex* Flex::setAlignItems(Align a) {
    m_alignItems = a;
    requestLayout();
    return this;
}

Flex* Flex::setBackground(const ColorRef& c, bool animate) {
    m_background.set(c, animate);
    requestRedraw();
    return this;
}

Flex* Flex::setRadius(Radii r) {
    m_radius = r;
    m_pill = false;
    requestRedraw();
    return this;
}

Flex* Flex::setBorder(float width, const ColorRef& c) {
    m_borderWidth = width;
    m_border = c;
    requestRedraw();
    return this;
}

Flex* pill(Flex* f) {
    f->m_pill = true;
    return f;
}

Radii Flex::baseRadius() const {
    const float k = Theme::get().roundness();
    const float half = std::min(rect().w, rect().h) / 2;
    // Pills only lose roundness, so "Sharp" squares chips off too
    if (m_pill) return Radii(half * std::min(k, 1.0f));
    auto f = [&](float v) { return std::min(v * k, half); };
    return {f(m_radius.tl), f(m_radius.tr), f(m_radius.br), f(m_radius.bl)};
}

Radii Flex::effectiveRadius() const {
    return baseRadius();
}

float Flex::cornerRadius() const {
    const Radii r = effectiveRadius();
    return std::max({r.tl, r.tr, r.br, r.bl});
}

SizeF Flex::doMeasure(SizeF available) {
    const bool row = m_dir == Direction::Row;
    const SizeF inner{std::max(0.0f, available.w - m_padding.left - m_padding.right),
                      std::max(0.0f, available.h - m_padding.top - m_padding.bottom)};
    float main = 0, cross = 0;
    int count = 0;
    for (auto& c : children()) {
        if (!c->shown() || c->floating) continue;
        // Down a column each child gets the height its earlier siblings left,
        // so a scroll view under a header caps to the room really there
        const float spent = main + (count > 0 ? m_spacing * static_cast<float>(count) : 0);
        const auto s = c->measure(row ? inner : SizeF{inner.w, std::max(0.0f, inner.h - spent)});
        main += row ? s.w : s.h;
        cross = std::max(cross, row ? s.h : s.w);
        ++count;
    }
    if (count > 1) main += m_spacing * static_cast<float>(count - 1);
    const float padMain = row ? m_padding.left + m_padding.right : m_padding.top + m_padding.bottom;
    const float padCross = row ? m_padding.top + m_padding.bottom : m_padding.left + m_padding.right;
    return row ? SizeF{main + padMain, cross + padCross} : SizeF{cross + padCross, main + padMain};
}

void Flex::doArrange() {
    const bool row = m_dir == Direction::Row;
    const RectF content{rect().x + m_padding.left, rect().y + m_padding.top,
                        std::max(0.0f, rect().w - m_padding.left - m_padding.right),
                        std::max(0.0f, rect().h - m_padding.top - m_padding.bottom)};
    const float contentMain = row ? content.w : content.h;
    const float contentCross = row ? content.h : content.w;

    std::vector<SizeF> sizes;
    float used = 0;
    int visibleCount = 0, growCount = 0;
    for (auto& c : children()) {
        // As in doMeasure: only the room left after the earlier siblings
        const float spent = used + m_spacing * static_cast<float>(visibleCount);
        const SizeF room = row ? SizeF{content.w, content.h} : SizeF{content.w, std::max(0.0f, content.h - spent)};
        sizes.push_back(c->shown() && !c->floating ? c->measure(room) : SizeF{});
        if (!c->shown() || c->floating) continue;
        used += row ? sizes.back().w : sizes.back().h;
        ++visibleCount;
        if (c->grow) ++growCount;
    }
    if (visibleCount > 1) used += m_spacing * static_cast<float>(visibleCount - 1);

    const float leftover = contentMain - used;
    // Room to spare goes to the growing children; too little, and they give
    // it back (wrapping content then fits) rather than spill past the edge
    const float growShare = growCount > 0 ? leftover / static_cast<float>(growCount) : 0;
    float pos = 0;
    if (growCount == 0 && leftover > 0) {
        if (m_justify == Align::Center) pos = leftover / 2;
        if (m_justify == Align::End) pos = leftover;
    }

    for (size_t i = 0; i < children().size(); ++i) {
        auto& c = children()[i];
        if (!c->shown() || c->floating) continue;
        float m = row ? sizes[i].w : sizes[i].h;
        float x = row ? sizes[i].h : sizes[i].w; // cross size
        if (c->grow) m = std::max(0.0f, m + growShare);
        float crossPos = 0;
        switch (c->alignSelf.value_or(m_alignItems)) {
        case Align::Stretch: x = contentCross; break;
        case Align::Center: crossPos = (contentCross - x) / 2; break;
        case Align::End: crossPos = contentCross - x; break;
        case Align::Start: break;
        }
        c->arrange(row ? RectF{content.x + pos, content.y + crossPos, m, x}
                       : RectF{content.x + crossPos, content.y + pos, x, m});
        pos += m + m_spacing;
    }
}

void Flex::doPaint(Renderer& r) {
    const Color bg = m_background.value();
    const Color border = m_border.resolve();
    if (bg.a > 0 || (m_borderWidth > 0 && border.a > 0)) r.rect(rect(), bg, effectiveRadius(), m_borderWidth, border);
    if (m_background.running()) requestRedraw();
}

// ── Label ──

Label::Label(std::string text) : m_text(std::move(text)) {}

Label* Label::setText(const std::string& text) {
    if (text == m_text) return this;
    m_text = text;
    requestLayout();
    return this;
}

Label* Label::setSize(float px) {
    m_size = px;
    requestLayout();
    return this;
}

Label* Label::setWeight(int w) {
    m_weight = w;
    requestLayout();
    return this;
}

Label* Label::setColor(const ColorRef& c, bool animate) {
    m_color.set(c, animate);
    requestRedraw();
    return this;
}

Label* Label::setMaxWidth(float w) {
    m_maxWidth = w;
    requestLayout();
    return this;
}

Label* Label::setCentered(bool centered) {
    m_center = centered;
    requestLayout();
    return this;
}

Label* Label::setMaxLines(int lines) {
    m_maxLines = lines;
    requestLayout();
    return this;
}

Label* Label::setFamily(const std::string& family) {
    m_family = family;
    requestLayout();
    return this;
}

TextStyle Label::style() const {
    TextStyle s;
    s.family = !m_family.empty() ? m_family : Theme::get().fontFamily();
    // Accessibility: larger and bolder text (icons keep their size and grade)
    const auto& theme = Theme::get();
    // Each part of the shell it sits in (the lock screen, a widget on it).
    // Icons keep their size, except in widgets, which grow whole
    float scale = theme.textScale(), iconScale = 1;
    for (const Node* n = this; n; n = n->parentNode()) {
        if (n->textArea) scale *= theme.areaScale(n->textArea);
        if (n->textArea == Theme::AreaWidgets) iconScale *= theme.areaScale(n->textArea);
        scale *= n->textMul;
        iconScale *= n->textMul;
        if (n->ownTextSize) break;
    }
    s.size = std::round(m_size * (m_icon ? iconScale : scale));
    s.weight = !m_icon && theme.boldText() ? std::min(900, m_weight + 200) : m_weight;
    s.maxWidth = m_maxWidth;
    s.maxLines = m_maxLines;
    s.center = m_center;
    s.icon = m_icon;
    s.fill = m_fill;
    return s;
}

SizeF Label::doMeasure(SizeF) {
    if (m_text.empty()) return {};
    const auto m = TextEngine::get().measure(m_text, style());
    return {std::ceil(m.width), std::ceil(m.height)};
}

void Label::doPaint(Renderer& r) {
    if (m_text.empty()) return;
    const auto& tex = TextEngine::get().texture(m_text, style(), r.scale());
    const float s = r.scale();
    // Snap the quad to device pixels so text is never resampled
    const float x = std::round(rect().x * s) / s;
    const float y = std::round((rect().y + (rect().h - tex.logical.height) / 2) * s) / s;
    r.mask(tex.id, {x, y, tex.logical.width, tex.logical.height}, m_color.value());
    if (m_color.running()) requestRedraw();
}

// ── Icon ──

Icon::Icon(std::string name, float size) : Label(std::move(name)) {
    m_icon = true;
    m_family = Theme::get().iconFamily();
    m_size = size;
}

Icon* Icon::setFill(bool filled) {
    m_fill = filled ? 1 : 0;
    requestRedraw();
    return this;
}

// ── Button ──

Button::Button(Direction dir) : Flex(dir) {}

void Button::onHover(bool hovered) {
    m_hovered = hovered;
    m_state.set(hovered ? 0.08f : 0.0f);
    requestRedraw();
}

void Button::onPress(uint32_t, float, float) {
    m_state.set(0.12f);
    if (pressedRadius >= 0) m_press.set(1);
    requestRedraw();
}

void Button::onRelease() {
    m_state.set(m_hovered ? 0.08f : 0.0f);
    m_press.set(0);
    requestRedraw();
}

void Button::onClick(uint32_t button) {
    m_state.set(m_hovered ? 0.08f : 0.0f);
    requestRedraw();
    if (onClicked) onClicked(button);
}

Radii Button::effectiveRadius() const {
    const Radii base = baseRadius();
    const float t = m_press.value();
    if (pressedRadius < 0 || t == 0) return base;
    const float to = std::min(pressedRadius * Theme::get().roundness(), std::min(rect().w, rect().h) / 2);
    // The spring overshoots; corners never go negative
    auto mix = [&](float from) { return std::max(0.0f, from + (to - from) * t); };
    return {mix(base.tl), mix(base.tr), mix(base.br), mix(base.bl)};
}

bool Button::onScroll(int d) {
    if (!onScrolled) return false;
    onScrolled(d);
    return true;
}

void Button::doPaint(Renderer& r) {
    Flex::doPaint(r);
    const float a = m_state.value();
    if (a > 0) r.rect(rect(), stateLayer.resolve().withAlpha(a), effectiveRadius());
    if (m_state.running() || m_press.running()) requestRedraw();
}

} // namespace m3::ui
