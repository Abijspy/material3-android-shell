#pragma once

#include "core/Signal.h"
#include "ui/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Building blocks shared by the popup panels
namespace m3::shell::kit {

// A column that re-runs `sync` whenever a watched service changes, and drops
// the subscriptions with the popup.
class Live : public ui::Flex {
public:
    Live();
    ~Live() override;
    std::vector<Connection> conns;
    std::function<void()> sync;
    // Service changes sync on the next loop iteration, coalesced: a sync may
    // rebuild the very button whose click caused the change
    void watch(Signal<>& s);
    void syncSoon();

private:
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_pending = false;
};

ui::Flex* header(ui::Node* parent, const std::string& icon, const std::string& title);
ui::Label* caption(ui::Node* parent, const std::string& text);

// A level bar as wide as it is given (battery charge)
class Meter : public ui::Node {
public:
    Meter(float value, ColorRef color, float height = 12);

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, m_height}; }
    void doPaint(Renderer& r) override;

private:
    float m_value, m_height;
    ColorRef m_color;
};

// Children left to right, wrapping onto new lines (chips)
class Flow : public ui::Node {
public:
    float spacing = 6;

protected:
    SizeF doMeasure(SizeF available) override { return place(available.w, false); }
    void doArrange() override { place(rect().w, true); }

private:
    SizeF place(float width, bool arrange) {
        float x = 0, y = 0, line = 0, widest = 0;
        for (auto& c : children()) {
            if (!c->shown()) continue;
            const SizeF s = c->measure({width, 1e6f});
            if (x > 0 && x + s.w > width) {
                x = 0;
                y += line + spacing;
                line = 0;
            }
            if (arrange) c->arrange({rect().x + x, rect().y + y, s.w, s.h});
            x += s.w + spacing;
            line = std::max(line, s.h);
            widest = std::max(widest, x - spacing);
        }
        return {widest, y + line};
    }
};

// Round icon button (close, back, power…)
ui::Button* iconButton(ui::Node* parent, const std::string& icon, std::function<void()> onClick, float size = 20);

// Quick Settings tile, as Android 17 draws them: a pill with the icon in a
// chip of its own. Tiles with details (Wi-Fi, Bluetooth) light the chip when
// on, which then toggles while the rest opens the details; the others fill
// completely. The chip morphs from a circle (off) to a rounded square (on).
class Tile : public ui::Button {
public:
    Tile(const std::string& icon, const std::string& title);
    void set(bool active, const std::string& status, const std::string& icon = {});
    std::function<void()> onToggle;  // the chip, or the whole tile without details
    std::function<void()> onDetails; // the body, when set
    void setSelected(bool selected); // the editor's outline

private:
    ui::Button* m_chip;
    ui::Icon* m_icon;
    ui::Label* m_title;
    ui::Label* m_status;
    bool m_active = false;
};

// Small Quick Settings tile: the icon alone in a pill, filled when on
class SmallTile : public ui::Button {
public:
    SmallTile(const std::string& icon, float width);
    void set(bool active, const std::string& icon = {});
    void setSelected(bool selected);

private:
    ui::Icon* m_icon;
};

} // namespace m3::shell::kit
