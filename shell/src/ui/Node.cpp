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

void Node::clearChildren() {
    m_children.clear();
    requestLayout();
}

bool Node::shown() const {
    if (!m_visible) return false;
    if (!collapseWhenEmpty) return true;
    return std::any_of(m_children.begin(), m_children.end(), [](auto& c) { return c->shown(); });
}

SizeF Node::measure(SizeF available) {
    if (!shown()) return {};
    SizeF s = doMeasure(available);
    if (fixedWidth) s.w = *fixedWidth;
    if (fixedHeight) s.h = *fixedHeight;
    s.w = std::max(s.w, minWidth);
    s.h = std::max(s.h, minHeight);
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
    if (!shown()) return;
    const float o = m_opacity.value(), dy = m_translateY.value();
    if (o <= 0.001f) return;
    if (o < 1) r.pushOpacity(o);
    if (dy != 0) r.pushTranslate(0, dy);
    doPaint(r);
    if (clipsChildren()) r.pushClip(m_rect);
    for (auto& c : m_children) c->paint(r);
    if (clipsChildren()) r.popClip();
    if (dy != 0) r.popTranslate();
    if (o < 1) r.popOpacity();
    if (m_opacity.running() || m_translateY.running()) requestRedraw();
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

void Node::setTranslateY(float dy, bool animate) {
    m_translateY.set(dy, animate);
    requestRedraw();
}

bool Node::animating() const {
    if (!shown()) return false;
    if (doAnimating() || m_opacity.running() || m_translateY.running()) return true;
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

Radii Flex::effectiveRadius() const {
    if (m_pill) return Radii(std::min(rect().w, rect().h) / 2);
    return m_radius;
}

SizeF Flex::doMeasure(SizeF available) {
    const bool row = m_dir == Direction::Row;
    const SizeF inner{std::max(0.0f, available.w - m_padding.left - m_padding.right),
                      std::max(0.0f, available.h - m_padding.top - m_padding.bottom)};
    float main = 0, cross = 0;
    int count = 0;
    for (auto& c : children()) {
        if (!c->shown()) continue;
        const auto s = c->measure(inner);
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
        sizes.push_back(c->shown() ? c->measure({content.w, content.h}) : SizeF{});
        if (!c->shown()) continue;
        used += row ? sizes.back().w : sizes.back().h;
        ++visibleCount;
        if (c->grow) ++growCount;
    }
    if (visibleCount > 1) used += m_spacing * static_cast<float>(visibleCount - 1);

    const float leftover = contentMain - used;
    const float growShare = growCount > 0 ? std::max(0.0f, leftover) / static_cast<float>(growCount) : 0;
    float pos = 0;
    if (growCount == 0 && leftover > 0) {
        if (m_justify == Align::Center) pos = leftover / 2;
        if (m_justify == Align::End) pos = leftover;
    }

    for (size_t i = 0; i < children().size(); ++i) {
        auto& c = children()[i];
        if (!c->shown()) continue;
        float m = row ? sizes[i].w : sizes[i].h;
        float x = row ? sizes[i].h : sizes[i].w; // cross size
        if (c->grow) m += growShare;
        float crossPos = 0;
        switch (m_alignItems) {
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

Label* Label::setFamily(const std::string& family) {
    m_family = family;
    requestLayout();
    return this;
}

TextStyle Label::style() const {
    TextStyle s;
    s.family = !m_family.empty() ? m_family : Theme::get().fontFamily();
    s.size = m_size;
    s.weight = m_weight;
    s.maxWidth = m_maxWidth;
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
    requestRedraw();
}

void Button::onClick(uint32_t button) {
    m_state.set(m_hovered ? 0.08f : 0.0f);
    requestRedraw();
    if (onClicked) onClicked(button);
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
    if (m_state.running()) requestRedraw();
}

} // namespace m3::ui
