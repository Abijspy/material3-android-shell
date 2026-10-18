#pragma once

#include "core/Signal.h"
#include "ui/Node.h"

#include <string>
#include <vector>

namespace m3::shell {

// Places three children: start, centre (truly centred, pushed aside only if
// it would overlap) and end.
class BarLayout : public ui::Node {
public:
    float margin = 8;

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

class WindowTitle : public ui::Label {
public:
    WindowTitle();
    void sync();

private:
    Connection m_conn;
};

class Battery : public ui::Flex {
public:
    Battery();
    ~Battery() override;
    void sync();

private:
    std::string m_path; // /sys/class/power_supply/BATx
    ui::Icon* m_icon = nullptr;
    ui::Label* m_label = nullptr;
    unsigned long long m_timer = 0;
};

} // namespace m3::shell
