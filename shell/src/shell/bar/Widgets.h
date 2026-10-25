#pragma once

#include "core/Signal.h"
#include "ui/Controls.h"
#include "ui/Node.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace m3::shell {

// Which edge the bar is on (Settings → Status bar → Position)
enum class BarEdge { Top, Bottom, Left, Right };
BarEdge barEdge();
// On the left or right edge: its widgets stack, top to bottom
bool barVertical();
// Where along the bar a widget sits, for the popup it opens: its centre's
// x on a top or bottom bar, its y on a side one
float alongBar(const ui::Node* n);

// Places three children: start, centre (truly centred, pushed aside only if
// it would overlap) and end; down the bar when it's vertical.
class BarLayout : public ui::Node {
public:
    float margin = 8;
    bool vertical = false;
    // Right click on the bar's empty space (Edit bar)
    std::function<void()> onContextMenu;
    bool interactive() const override { return static_cast<bool>(onContextMenu); }
    void onClick(uint32_t button) override;

protected:
    m3::SizeF doMeasure(m3::SizeF available) override;
    void doArrange() override;
};

// The pills' fill (Settings → Status bar → Pill highlight): Auto fills them
// in the Pills and Framed styles; none leaves them on the bar's own surface
std::optional<ColorRef> pillColour();
// Blur behind the bar: on in Look & feel, and for the bar
bool barBlur();
// A pill's shape: its corners and border (Settings → Status bar → Pills)
void shapePill(ui::Flex* pill);

// A rounded module background, in the pill colour; transparent with none,
// or when `filled` is false (widgets inside a group's capsule). On a side
// bar it's a column
ui::Flex* capsule(ui::Node* parent, bool filled);

class Workspaces : public ui::Flex {
public:
    explicit Workspaces(std::string monitor, bool vertical = false);
    void sync();

private:
    class Dot;
    std::string m_monitor;
    bool m_vertical;
    std::vector<ui::Button*> m_slots;
    std::vector<Dot*> m_dots;
    Connection m_conn;
};

// The time, and the date if chosen, as each place sets them: `place` is
// "island" or "bar" (Settings → Status bar → Island & clock)
class Clock : public ui::Label {
public:
    explicit Clock(std::string place = "island");
    ~Clock() override;
    void tick();
    // Hours over minutes, for a side bar (the time only, no date)
    Clock* setStacked(bool stacked);

    // Qt-style pattern ("hh:mm", "h:mm AP", "ddd d MMM") → strftime
    static std::string toStrftime(const std::string& qt);
    // A pattern as it reads now ("Mon 20 Oct"), for Settings' choices
    static std::string sample(const std::string& qt);

private:
    void schedule();
    std::string m_place;
    unsigned long long m_timer = 0;
    bool m_seconds = false, m_stacked = false;
    Connection m_config;
};

// The focused window: its app's icon, fixed, then its title, which scrolls
// when it's longer than its space (Settings → Status bar → Scrolling text).
// On a side bar the title reads down it, under the icon
class WindowTitle : public ui::Flex {
public:
    explicit WindowTitle(bool vertical = false);
    void sync();

private:
    ui::ImageView* m_icon;
    ui::Marquee* m_title = nullptr;
    ui::Label* m_turned = nullptr; // the side bar's title
    std::string m_iconName; // what m_icon draws
    Connection m_style;
    Connection m_conn;
};

} // namespace m3::shell
