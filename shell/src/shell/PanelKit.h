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

// Round icon button (close, back, power…)
ui::Button* iconButton(ui::Node* parent, const std::string& icon, std::function<void()> onClick, float size = 20);

// Quick Settings tile: icon, title and status; tonal when active
class Tile : public ui::Button {
public:
    Tile(const std::string& icon, const std::string& title);
    void set(bool active, const std::string& status);
    // Optional trailing chevron that opens details instead of toggling
    std::function<void()> onDetails;
    void enableDetails();

private:
    ui::Icon* m_icon;
    ui::Label* m_title;
    ui::Label* m_status;
    ui::Icon* m_chevron = nullptr;
    bool m_active = false;
};

} // namespace m3::shell::kit
