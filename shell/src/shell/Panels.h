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
// A dock icon's right-click menu; `arg` is "<desktop id>|<window class>"
void dockMenu(ui::Flex* card, Popup& popup, const std::string& arg);
// A pairing question asked while no device list is open
void bluetoothPair(ui::Flex* card, Popup& popup);
// Paired and nearby devices, scanning while shown, with the pairing prompt
// (Quick Settings and the bar popup); back may be empty
void bluetoothView(ui::Flex* body, const std::function<void()>& back);
// The pending pairing question, if any: the code to compare or type, or a
// field for one, and the buttons that answer it (call inside a Live sync)
void bluetoothPrompt(ui::Flex* parent);
// Material Symbol for a BlueZ device icon name ("audio-headset" …)
std::string bluetoothGlyph(const std::string& bluezIcon);
void battery(ui::Flex* card, Popup& popup);
void run(ui::Flex* card, Popup& popup);
void quickSettings(ui::Flex* card, Popup& popup, const std::string& view = {}); // view: "wifi", "bluetooth", "edit"
void notifications(ui::Flex* card, Popup& popup);
// The notification centre is on screen: new notifications show there, not
// as pop-ups over it
bool notificationCentreShown();
void power(ui::Flex* card, Popup& popup);
void launcher(ui::Flex* card, Popup& popup, const std::string& initial);
void trayMenu(ui::Flex* card, Popup& popup, const std::string& itemKey);
void polkit(ui::Flex* card, Popup& popup, int requestId);
void dashboard(ui::Flex* card, Popup& popup);
void wallpapers(ui::Flex* card, Popup& popup);
void overview(ui::Flex* card, Popup& popup);
void settings(ui::Flex* card, Popup& popup, const std::string& page);
// The dashboard's cards on their own, for the desktop: "calendar",
// "weather", "system" (null for anything else)
ui::Node* dashboardWidget(ui::Node* parent, const std::string& id);
// Hyprland Settings: the compositor's options, a window of its own
void hyprSettings(ui::Flex* card, Popup& popup, const std::string& page);
// The open Settings app: show a page; forget it once its window closes
void settingsPage(const std::string& id);
void settingsClosed(bool hypr = false);
void clipboard(ui::Flex* card, Popup& popup);
void screenshot(ui::Flex* card, Popup& popup);
// The Wi-Fi network list (Quick Settings and Settings); back may be empty.
// `details` adds the connected network's speeds and addresses above it.
// `connected`, when given, fills the connected network's entry instead of
// the compact summary (Settings: speeds and every detail) as a dropdown:
// tapping the entry opens and closes it rather than disconnecting (the
// details carry a button for that); the list grows to full height for the
// page to scroll
void wifiView(ui::Flex* body, const std::function<void()>& back, bool details = true,
              std::function<void(ui::Flex*)> connected = {});
// Download/upload as "1.2 MB/s"
std::string formatRate(double bytesPerSecond);

// Set by the shell: open another panel by name (Quick Settings → power menu…)
inline std::function<void(const std::string& name)> open;
// Set by the shell: a panel risen just above the dock, centred on `anchorX`
inline std::function<void(const std::string& name, const std::string& arg, float anchorX)> openAboveDock;
// Set by the shell: lock the session
inline std::function<void()> lock;
// Set by the shell: turn the bars' edit mode on or off
inline std::function<void(bool)> editBar;
// A file picker for pictures: `purpose` "wallpaper" (sets it) or "avatar"
// (your picture, ~/.face, with the default pictures first)
void filePicker(ui::Flex* card, Popup& popup, const std::string& purpose);
// A Quick Share transfer by its id: the question (PIN, files), then progress
void quickSharePanel(ui::Flex* card, Popup& popup, const std::string& arg);
// Sending: arg, the files' paths, one per line
void quickShareSend(ui::Flex* card, Popup& popup, const std::string& arg);
// A calendar event: "new:YYYY-MM-DD" to add one, or an event's id
void eventEditor(ui::Flex* card, Popup& popup, const std::string& arg);
// The first-run tour, in its own window
void welcome(ui::Flex* card, Popup& popup);
// After an update: the release notes since `since` (empty: this release's)
void whatsNew(ui::Flex* card, Popup& popup, const std::string& since);
// CHANGELOG.md's newest `max` releases (since `since`, when given) as
// lists of notes `width` wide (What's new, Settings → Updates and About)
void releaseNotes(ui::Node* parent, int max, float width, const std::string& since = {});
// The widget picker the bar's "+" opens; zone is left, centre or right
void barWidgets(ui::Flex* card, Popup& popup, const std::string& zone);
// The lock screen at full size, editable on its grid
void lockEditor(ui::Flex* card, Popup& popup);

} // namespace m3::shell::panels
