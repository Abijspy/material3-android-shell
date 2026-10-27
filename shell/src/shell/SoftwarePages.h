#pragma once

// Settings → Software & updates' Repositories tab and its update settings,
// and Settings → Firmware
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

// pacman's repositories, mirrors, held-back packages and downloads, and
// Flatpak's remotes
void repositoriesTab(ui::Flex* body);
// The Updates tab's foot: firmware, and the automatic checks
void updateSettings(ui::Flex* body);
// fwupd: the devices, their firmware updates and where they come from
void firmwarePage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
