#pragma once

#include "ui/Node.h"

#include <string>

// More looks for the widgets shared by the lock screen, the login screen and
// the desktop: calendars (a day tile, a week strip, a month), music players
// (a disc in a progress ring, a slim bar) and weather (big, a card)
namespace m3::shell {

// Adds the widget `id` to `parent` aligned as `align`, or returns nullptr
// when `id` isn't one of these. On the greeter nothing personal shows.
ui::Node* addStyledWidget(ui::Node* parent, const std::string& id, bool greeter, ui::Align align);

} // namespace m3::shell
