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
void dashboard(ui::Flex* card, Popup& popup);
void systemMonitor(ui::Flex* card, Popup& popup);
void wallpapers(ui::Flex* card, Popup& popup);
void overview(ui::Flex* card, Popup& popup);
void settings(ui::Flex* card, Popup& popup, const std::string& page);
void clipboard(ui::Flex* card, Popup& popup);
void screenshot(ui::Flex* card, Popup& popup);
// The Wi-Fi network list (Quick Settings and Settings); back may be empty.
// `details` adds the connected network's speeds and addresses above it.
void wifiView(ui::Flex* body, const std::function<void()>& back, bool details = true);
// Download/upload as "1.2 MB/s"
std::string formatRate(double bytesPerSecond);

// Set by the shell: open another panel by name (Quick Settings → power menu…)
inline std::function<void(const std::string& name)> open;
// Set by the shell: lock the session
inline std::function<void()> lock;
// Set by the shell: turn the bars' edit mode on or off
inline std::function<void(bool)> editBar;
// The widget picker the bar's "+" opens; zone is left, centre or right
void barWidgets(ui::Flex* card, Popup& popup, const std::string& zone);
// The lock screen at full size, editable on its grid
void lockEditor(ui::Flex* card, Popup& popup);

} // namespace m3::shell::panels
