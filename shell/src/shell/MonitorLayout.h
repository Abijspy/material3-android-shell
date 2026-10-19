#pragma once

#include "ui/Node.h"

#include <functional>
#include <string>
#include <vector>

namespace m3::shell {

// The monitors' arrangement, to scale: each a card that drags to a new
// place, snapping to the others' edges. Positions are in Hyprland's layout
// coordinates (logical pixels).
class MonitorLayout : public ui::Node {
public:
    struct Screen {
        std::string key, title, detail;
        float x, y, w, h; // logical
        bool focused = false;
    };
    explicit MonitorLayout(std::vector<Screen> screens);

    // Released somewhere new: its new top-left
    std::function<void(const std::string& key, int x, int y)> onMoved;

    bool interactive() const override { return true; }
    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 260}; }
    void doArrange() override;
    void doPaint(Renderer& r) override;

private:
    struct View {
        float scale, ox, oy; // logical → node: node = origin + logical * scale
    };
    View fit() const;
    std::vector<Screen> m_screens;
    std::vector<ui::Node*> m_cards;
    int m_drag = -1;
    float m_grabX = 0, m_grabY = 0;
    View m_frozen{}; // the view stays put while dragging
};

} // namespace m3::shell
