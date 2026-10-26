#pragma once

#include <string>

// Alarms and timers (services/Alarms): the clock popup and Settings → Alarms
// & timers
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

// As Android's Clock: an Alarms tab and a Timers tab. arg: "alarms" or
// "timers" (the tab), "new" (a new alarm), "edit:<id>" (an alarm)
void clock(ui::Flex* card, Popup& popup, const std::string& arg);
void clockPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
