#pragma once

#include "shell/Popup.h"

namespace m3::shell::panels {

// Popup contents. Each fills the card and keeps itself in sync with its
// service for as long as the popup lives.
void audio(ui::Flex* card, Popup& popup);
void network(ui::Flex* card, Popup& popup);
void bluetooth(ui::Flex* card, Popup& popup);
void run(ui::Flex* card, Popup& popup);

} // namespace m3::shell::panels
