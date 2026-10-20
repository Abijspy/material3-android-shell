#pragma once

#include "core/Signal.h"
#include "ui/Controls.h"
#include "ui/Node.h"

#include <functional>
#include <string>
#include <vector>

namespace m3::shell {

// Places three children: start, centre (truly centred, pushed aside only if
// it would overlap) and end.
class BarLayout : public ui::Node {
public:
    float margin = 8;
    // Right click on the bar's empty space (Edit bar)
    std::function<void()> onContextMenu;
    bool interactive() const override { return static_cast<bool>(onContextMenu); }
    void onClick(uint32_t button) override;

protected:
    m3::SizeF doMeasure(m3::SizeF available) override;
    void doArrange() override;
};

// A rounded module background in "floating" bar style; transparent when the
// bar draws its own full background.
ui::Flex* capsule(ui::Node* parent, bool filled);

class Workspaces : public ui::Flex {
public:
    explicit Workspaces(std::string monitor);
    void sync();

private:
    class Dot;
    std::string m_monitor;
    std::vector<ui::Button*> m_slots;
    std::vector<Dot*> m_dots;
    Connection m_conn;
};

class Clock : public ui::Label {
public:
    Clock();
    ~Clock() override;
    void tick();

    // Qt-style pattern from config ("hh:mm", "h:mm AP") → strftime
    static std::string toStrftime(const std::string& qt);

private:
    void schedule();
    unsigned long long m_timer = 0;
    bool m_seconds = false;
};

// The focused window: its app's icon, fixed, then its title, which scrolls
// when it's longer than its space (Settings → Status bar → Scrolling text)
class WindowTitle : public ui::Flex {
public:
    WindowTitle();
    void sync();

private:
    ui::ImageView* m_icon;
    ui::Marquee* m_title;
    std::string m_iconName; // what m_icon draws
    Connection m_style;
    Connection m_conn;
};

} // namespace m3::shell
