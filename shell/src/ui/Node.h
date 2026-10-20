#pragma once

#include "render/Renderer.h"
#include "render/Text.h"
#include "ui/Animation.h"
#include "ui/Color.h"
#include "ui/Geometry.h"
#include "ui/Input.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace m3::ui {

// Whoever displays a tree (a layer surface) gets told when it must lay out or
// repaint. Nodes never paint or relayout synchronously from a setter.
class Host {
public:
    virtual ~Host() = default;
    virtual void requestLayout() = 0;
    virtual void requestRedraw() = 0;
    // A node is going away; drop any pointer to it (hover, press, focus)
    virtual void nodeDestroyed(class Node*) {}
    virtual void requestFocus(class Node*) {}
};

enum class Align { Start, Center, End, Stretch };

// Scene graph base: owns children, measures and arranges them, paints itself
// then its children, and routes pointer input to the deepest interactive node.
class Node {
public:
    virtual ~Node();

    template <typename T, typename... Args>
    T* add(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = child.get();
        adopt(std::move(child));
        return raw;
    }
    void adopt(std::unique_ptr<Node> child);
    void adopt(std::unique_ptr<Node> child, size_t index);
    void remove(Node* child);
    void clearChildren();
    const std::vector<std::unique_ptr<Node>>& children() const { return m_children; }

    // Layout: measure() returns the preferred size within `available`;
    // arrange() assigns the final rect.
    SizeF measure(SizeF available);
    void arrange(const RectF& rect);
    const RectF& rect() const { return m_rect; }
    static long measureCount(); // doMeasure() calls so far (frame debugging)
    // Forget every remembered measurement (a layout pass starts, or anything
    // that affects sizes changed)
    static void invalidateMeasures();

    void paint(Renderer& r);

    // Input. hitTest returns the deepest interactive node (or, with `any`,
    // the deepest node of any kind).
    virtual Node* hitTest(float x, float y, bool any = false);
    virtual bool interactive() const { return false; }
    virtual bool focusable() const { return false; }
    virtual void onHover(bool) {}
    // The pointer entered or left this node or any of its descendants
    virtual void onHoverWithin(bool) {}
    virtual void onPress(uint32_t /*button*/, float /*x*/, float /*y*/) {}
    virtual void onDrag(float /*x*/, float /*y*/) {}
    virtual void onRelease() {}
    virtual void onClick(uint32_t) {}
    virtual bool onScroll(int) { return false; } // true: handled
    // Pixel-exact scrolling (scroll views): `precise` for touchpads, which
    // follow the fingers; wheels step. onScrollEnd: the fingers lifted.
    virtual bool onScrollPixels(float /*dy*/, bool /*precise*/) { return false; }
    virtual void onScrollEnd() {}
    // Takes scroll steps itself (sliders, buttons with onScrolled): pixel
    // scrolling stops here and leaves the steps to it
    virtual bool scrollsDiscretely() const { return false; }
    virtual bool onKey(const KeyEvent&) { return false; }
    virtual void onFocus(bool) {}
    void focus();
    Node* parentNode() const { return m_parent; }

    // Visual-only transforms for transitions; layout and hit testing ignore them
    void setOpacity(float o, bool animate = true);
    void setTranslateY(float dy, bool animate = true);
    void setTranslateX(float dx, bool animate = true);

    // Animations still running in this subtree
    bool animating() const;

    // Shown in a tooltip while the pointer rests on this node (or inside it).
    // setTooltip also updates one already showing (the volume as it changes)
    std::string tooltip;
    void setTooltip(std::string text) {
        if (text == tooltip) return;
        tooltip = std::move(text);
        if (tooltipChanged) tooltipChanged(this);
    }
    static inline std::function<void(Node*)> tooltipChanged;

    // Sizing constraints honoured by parents
    std::optional<float> fixedWidth, fixedHeight;
    float minWidth = 0, minHeight = 0;
    bool grow = false; // takes leftover main-axis space in a Flex
    // Cross-axis alignment within a Flex; unset follows the parent's alignItems
    std::optional<Align> alignSelf;
    // Left out of a Flex's flow: the owner places it (menus over a layout)
    bool floating = false;

    // The compositor blurs what is behind this node's rect (translucent
    // cards); the surface collects these into its blur region after layout
    bool blurBehind = false;
    // The part of the shell this subtree is (Theme::TextArea), for its own
    // text size on top of the shell's; 0 inherits
    uint8_t textArea = 0;
    // This subtree's own size (a desktop widget's): its text and, inside
    // widgets, its icons
    float textMul = 1;
    // Sized on its own: the parts above it don't scale its text (the lock
    // screen's clock, which has its own size)
    bool ownTextSize = false;
    virtual float cornerRadius() const { return 0; }

    void setVisible(bool v);
    bool visible() const { return m_visible; }
    // Visible, and, for collapseWhenEmpty containers, has a shown child
    bool shown() const;
    bool collapseWhenEmpty = false;

    void requestLayout();
    void requestRedraw();
    void setHost(Host* host) { m_host = host; }

protected:
    virtual SizeF doMeasure(SizeF available);
    virtual void doArrange();
    virtual void doPaint(Renderer&) {}
    // After the children, inside this node's clip (effects over its content)
    virtual void doPaintOver(Renderer&) {}
    virtual bool doAnimating() const { return false; }
    virtual bool clipsChildren() const { return false; }

    Node* parent() const { return m_parent; }

private:
    Tween m_opacity{1, motion::mediumMs, Easing::emphasizedDecelerate()};
    Tween m_translateY{0, motion::mediumMs, Easing::emphasizedDecelerate()};
    Tween m_translateX{0, motion::mediumMs, Easing::emphasizedDecelerate()};
    Host* host() const;

    Node* m_parent = nullptr;
    Host* m_host = nullptr;
    std::vector<std::unique_ptr<Node>> m_children;
    RectF m_rect;
    bool m_visible = true;
    // The last two measurements (parents measure, then arrange with the
    // final size), valid until the measure generation moves on
    struct Measured {
        uint64_t generation = 0;
        SizeF available, size;
    };
    Measured m_measured[2];
    uint8_t m_nextMeasured = 0;
};

// Row or column container with padding, spacing, cross-axis alignment, an
// optional rounded background and border. The workhorse of every layout.
class Flex : public Node {
public:
    enum class Direction { Row, Column };

    explicit Flex(Direction dir = Direction::Row) : m_dir(dir) {}

    Flex* setSpacing(float s);
    Flex* setPadding(Insets p);
    Flex* setJustify(Align a);
    Flex* setAlignItems(Align a);
    Flex* setBackground(const ColorRef& c, bool animate = true);
    Flex* setRadius(Radii r);
    const Radii& radius() const { return m_radius; }
    Flex* setBorder(float width, const ColorRef& c);
    float cornerRadius() const override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doArrange() override;
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return m_background.running(); }
    // The set radius scaled by the theme's roundness, never past a pill
    virtual Radii effectiveRadius() const;
    Radii baseRadius() const;

    Direction m_dir;
    float m_spacing = 0;
    Insets m_padding;
    Align m_justify = Align::Start;
    Align m_alignItems = Align::Center;
    ColorTween m_background;
    Radii m_radius;
    float m_borderWidth = 0;
    ColorRef m_border = ColorRef::none();
    // Pill radius: half the smaller side, whatever the final size
    bool m_pill = false;

    friend Flex* pill(Flex*);
};

// Makes a Flex fully rounded at any size
Flex* pill(Flex* f);

class Label : public Node {
public:
    explicit Label(std::string text = {});

    Label* setText(const std::string& text);
    Label* setSize(float px);
    Label* setWeight(int w);
    Label* setColor(const ColorRef& c, bool animate = true);
    Label* setMaxWidth(float w);
    Label* setFamily(const std::string& family);
    Label* setMaxLines(int lines);
    Label* setCentered(bool centered); // wrapped lines centred under each other // wraps within maxWidth
    const std::string& text() const { return m_text; }
    float size() const { return m_size; }
    // The size it's drawn at: after the text sizes of the parts it's in
    float shownSize() const { return style().size; }
    int weight() const { return m_weight; }

protected:
    SizeF doMeasure(SizeF available) override;
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return m_color.running(); }
    TextStyle style() const;

    std::string m_text;
    std::string m_family; // empty: the theme font
    bool m_center = false;
    float m_size = 14;
    int m_weight = 400;
    float m_maxWidth = 0;
    int m_maxLines = 1;
    bool m_icon = false;
    float m_fill = 0;
    ColorTween m_color{ColorRef("on_surface")};
};

// A Material Symbols glyph by ligature name ("wifi", "volume_up").
class Icon : public Label {
public:
    explicit Icon(std::string name = {}, float size = 20);
    Icon* setFill(bool filled);
};

// Clickable container with the Material state layer: an on-surface tint at
// 8% on hover and 10% on press.
class Button : public Flex {
public:
    explicit Button(Direction dir = Direction::Row);

    std::function<void(uint32_t button)> onClicked;
    std::function<void(int direction)> onScrolled;
    ColorRef stateLayer = ColorRef("on_surface");
    // M3 Expressive shape morph: while pressed the corners spring towards
    // this radius (< 0: no morph)
    float pressedRadius = -1;

    bool interactive() const override { return true; }
    void onHover(bool hovered) override;
    void onPress(uint32_t button, float x, float y) override;
    void onRelease() override;
    void onClick(uint32_t button) override;
    bool onScroll(int d) override;
    bool scrollsDiscretely() const override { return static_cast<bool>(onScrolled); }

protected:
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return Flex::doAnimating() || m_state.running() || m_press.running(); }
    Radii effectiveRadius() const override;

private:
    Tween m_state{0, motion::shortMs, Easing::standard()};
    Tween m_press{0, motion::mediumMs, Easing::expressiveFast()};
    bool m_hovered = false;
};

} // namespace m3::ui
