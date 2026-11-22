#pragma once

#include "core/EventLoop.h"
#include "ui/Node.h"

#include <GLES2/gl2.h>
#include <string>
#include <vector>

typedef struct _PangoLayout PangoLayout;
typedef struct _PangoContext PangoContext;
typedef struct _PangoFontMap PangoFontMap;

namespace m3::ui {

// Text across many lines, edited: wrapped to its width, as tall as the text
// (a scroll view round it scrolls it, and follows the caret). The caret and
// selection by keyboard (arrows, Ctrl for words, Home/End, Shift to select)
// and pointer (click, drag, double click for a word), copy, cut and paste
// (the Wayland clipboard), and undo. Notes and the PDF editor's comments
// are written in it.
class TextArea : public Node {
public:
    TextArea();
    ~TextArea() override;

    TextArea* setText(const std::string& t);
    TextArea* setPlaceholder(const std::string& p);
    TextArea* setSize(float px);
    TextArea* setMinLines(int n);
    const std::string& text() const { return m_text; }

    std::function<void(const std::string&)> onChanged;
    // A key it doesn't use (Escape, Ctrl+S…), for its owner
    std::function<bool(const KeyEvent&)> onOtherKey;
    // One line, still wrapped to the width (a title): Enter and Tab go to
    // onOtherKey, pasted line breaks become spaces
    bool singleLine = false;

    bool interactive() const override { return true; }
    bool keyboardNavigable() const override { return true; }
    bool focusable() const override { return true; }
    void onFocus(bool f) override;
    bool onKey(const KeyEvent& e) override;
    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;

protected:
    SizeF doMeasure(SizeF available) override;
    void doPaint(Renderer& r) override;

private:
    void layoutFor(float width, float scale);
    size_t indexAt(float x, float y);
    RectF caretRect(size_t index) const;
    void insert(const std::string& s);
    void eraseSelection();
    bool hasSelection() const { return m_anchor != m_cursor; }
    std::string selected() const;
    void moveTo(size_t index, bool extend);
    size_t lineMove(int dir) const;
    size_t lineEdge(bool end) const;
    size_t wordEdge(size_t from, bool forward) const;
    void edited(bool snapshot);
    void reveal();
    void restartBlink();

    std::string m_text, m_placeholder;
    size_t m_cursor = 0, m_anchor = 0;
    float m_size = 15;
    int m_minLines = 3;
    bool m_focused = false, m_caretOn = true, m_dragging = false;
    EventLoop::TimerId m_blink = 0;
    EventLoop::Clock::time_point m_lastPress{};
    std::vector<std::pair<std::string, size_t>> m_undo, m_redo;
    EventLoop::Clock::time_point m_lastEdit{};

    // The drawn text
    PangoFontMap* m_fontMap = nullptr;
    PangoContext* m_ctx = nullptr;
    PangoLayout* m_layout = nullptr;
    float m_layoutWidth = -1, m_layoutScale = 0;
    bool m_dirty = true;
    GLuint m_tex = 0;
    int m_texW = 0, m_texH = 0;
    float m_height = 0; // logical px
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace m3::ui
