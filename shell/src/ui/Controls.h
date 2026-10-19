#pragma once

#include "core/EventLoop.h"
#include "ui/Node.h"

#include <functional>
#include <string>

namespace m3::ui {

// M3 slider: a thick rounded track split by a thin handle. `centred` fills
// from the middle (balance), otherwise from the start.
class Slider : public Node {
public:
    Slider();

    Slider* setRange(float min, float max);
    Slider* setValue(float v, bool animate = true);
    Slider* setCentred(bool c);
    Slider* setStep(float s);
    // M3 Expressive large slider (Quick Settings): a tall rounded track with
    // the icon inside its end, as Android 17 draws brightness and volume
    Slider* setIcon(const std::string& icon, float trackHeight = 40);
    const std::string& icon() const { return m_icon; }
    float value() const { return m_value; }

    std::function<void(float)> onChanged; // while dragging
    std::function<void(float)> onCommitted; // on release
    std::function<void()> onIconClicked;    // a press on the icon (mute), with setIcon

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onHover(bool h) override;
    void onPress(uint32_t, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;
    bool onScroll(int d) override;
    bool onKey(const KeyEvent& e) override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return m_shown.running() || m_handle.running(); }

private:
    void setFromX(float x);
    void change(float v, bool commit);
    float fraction(float v) const { return (v - m_min) / (m_max - m_min); }

    float m_min = 0, m_max = 1, m_value = 0, m_step = 0.05f;
    float m_track = 16; // M3 "XS" by default
    std::string m_icon;
    bool m_centred = false, m_dragging = false;
    Tween m_shown{0, motion::shortMs, Easing::standard()}; // displayed fraction
    Tween m_handle{4, motion::shortMs, Easing::standard()}; // handle width (pressed: 2)
};

// M3 switch: pill track with a thumb that grows and slides when on.
class Switch : public Node {
public:
    Switch();
    Switch* setChecked(bool c, bool animate = true);
    bool checked() const { return m_checked; }
    std::function<void(bool)> onToggled;

    bool interactive() const override { return true; }
    void onClick(uint32_t) override;

protected:
    SizeF doMeasure(SizeF) override { return {52, 32}; }
    void doPaint(Renderer& r) override;
    bool doAnimating() const override { return m_t.running(); }

private:
    bool m_checked = false;
    Tween m_t{0, motion::shortMs, Easing::standard()};
};

// Vertical scrolling for one content node, clipped, with eased wheel steps.
// When the content overflows, a scrollbar in a gutter on the right shows
// where you are; its thumb can be dragged and the track clicked.
class ScrollView : public Node {
public:
    float maxHeight = 400;
    // The scrollbar floats over the content instead of taking a gutter
    // (fixed-width grids, like Quick Settings' tiles)
    bool overlayBar = false;

    Node* content() const { return children().empty() ? nullptr : children().front().get(); }
    void scrollTo(float y, bool animate = true);
    bool onScroll(int d) override;

    // Interactive for the scrollbar; presses elsewhere do nothing
    bool interactive() const override { return overflows(); }
    void onHover(bool hovered) override;
    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doArrange() override;
    void doPaint(Renderer& r) override;
    bool clipsChildren() const override { return true; }
    bool doAnimating() const override { return m_offset.running() || m_bar.running(); }

private:
    static constexpr float kGutter = 14;
    float gutter() const { return overlayBar ? 0 : kGutter; }
    float maxOffset() const;
    void placeContent();
    bool overflows() const { return m_contentHeight > rect().h + 0.5f; }
    RectF thumb() const;
    Tween m_offset{0, motion::shortMs, Easing::standard()};
    Tween m_bar{0, motion::shortMs, Easing::standard()}; // 0 idle … 1 hovered/dragging
    float m_contentHeight = 0;
    float m_placedOffset = 0; // the offset the content was last arranged at
    float m_grab = -1; // pointer offset within the thumb while dragging
    bool m_hovered = false;
};

// Single-line text field: caret, placeholder, UTF-8 editing, horizontal
// scrolling to keep the caret visible.
class TextField : public Node {
public:
    bool bare = false; // no pill of its own (it sits in one, like a search bar)
    TextField();
    ~TextField() override;

    TextField* setText(const std::string& t);
    TextField* setPlaceholder(const std::string& p);
    TextField* setSecret(bool secret); // shows dots, never the text
    const std::string& text() const { return m_text; }

    std::function<void(const std::string&)> onChanged;
    std::function<void(const std::string&)> onSubmit;
    std::function<void()> onCancel;

    bool interactive() const override { return true; }
    bool focusable() const override { return true; }
    void onFocus(bool f) override;
    bool onKey(const KeyEvent& e) override;
    void onPress(uint32_t, float x, float y) override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doPaint(Renderer& r) override;

private:
    TextStyle style() const;
    float caretX(size_t pos) const;
    void edited();
    void restartBlink();

    std::string shownText() const;
    std::string m_text, m_placeholder;
    bool m_secret = false;
    size_t m_cursor = 0;
    bool m_focused = false, m_caretOn = true;
    float m_scroll = 0;
    EventLoop::TimerId m_blink = 0;
};

// Draws a texture supplied on demand (icons, pictures, pixmaps), fitted
// into its fixed size. The provider gets the device scale so it can ask
// for a sharp size, and may return null while an image is still decoding.
class ImageView : public Node {
public:
    using Provider = std::function<GLuint(float scale, int& w, int& h)>;
    ImageView(float size, Provider provider);
    ImageView(float width, float height, Provider provider);
    void setProvider(Provider p);
    bool cover = false; // fill and crop instead of fit

protected:
    SizeF doMeasure(SizeF) override { return {m_width, m_height}; }
    void doPaint(Renderer& r) override;

private:
    float m_width, m_height;
    Provider m_provider;
};

} // namespace m3::ui
