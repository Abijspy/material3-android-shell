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
    float value() const { return m_value; }

    std::function<void(float)> onChanged; // while dragging
    std::function<void(float)> onCommitted; // on release

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
class ScrollView : public Node {
public:
    float maxHeight = 400;

    Node* content() const { return children().empty() ? nullptr : children().front().get(); }
    void scrollTo(float y, bool animate = true);
    bool onScroll(int d) override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doArrange() override;
    bool clipsChildren() const override { return true; }
    bool doAnimating() const override { return m_offset.running(); }

private:
    float maxOffset() const;
    Tween m_offset{0, motion::shortMs, Easing::standard()};
    float m_contentHeight = 0;
};

// Single-line text field: caret, placeholder, UTF-8 editing, horizontal
// scrolling to keep the caret visible.
class TextField : public Node {
public:
    TextField();
    ~TextField() override;

    TextField* setText(const std::string& t);
    TextField* setPlaceholder(const std::string& p);
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

    std::string m_text, m_placeholder;
    size_t m_cursor = 0;
    bool m_focused = false, m_caretOn = true;
    float m_scroll = 0;
    EventLoop::TimerId m_blink = 0;
};

} // namespace m3::ui
