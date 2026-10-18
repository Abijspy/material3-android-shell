#pragma once

#include "shell/Popup.h"

#include <functional>
#include <string>

namespace m3::shell::panels {

// Popup contents. Each fills the card and keeps itself in sync with its
// services for as long as the popup lives.
void audio(ui::Flex* card, Popup& popup);
void network(ui::Flex* card, Popup& popup);
void bluetooth(ui::Flex* card, Popup& popup);
void run(ui::Flex* card, Popup& popup);
void quickSettings(ui::Flex* card, Popup& popup, const std::string& view = {}); // view: "wifi"
void notifications(ui::Flex* card, Popup& popup);
void power(ui::Flex* card, Popup& popup);
void launcher(ui::Flex* card, Popup& popup, const std::string& initial);
void trayMenu(ui::Flex* card, Popup& popup, const std::string& itemKey);
void polkit(ui::Flex* card, Popup& popup, int requestId);

// Set by the shell: open another panel by name (Quick Settings → power menu…)
inline std::function<void(const std::string& name)> open;
// Set by the shell: lock the session
inline std::function<void()> lock;

} // namespace m3::shell::panels
