#pragma once

// Settings' pages on niri, where they'd send you to Hyprland Settings on
// Hyprland: the touchpad and mouse, keyboard layouts and keys, monitors.
// They write the shell's settings for niri (services/NiriConfig)
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void niriMouse(ui::Flex* page);
void niriKeyboard(ui::Flex* page);
void niriMonitors(ui::Flex* page);
// When niri doesn't read the shell's settings (material3ui.kdl isn't
// included, or niri refused the file): why, and what to do
void niriNotice(ui::Flex* page);

// Niri Settings, the window Hyprland Settings is on Hyprland: how windows
// look and behave. Each option shows what's set here, else your
// config.kdl's value, else niri's default
void niriLayoutPage(ui::Flex* page, Popup&);
void niriDecorationPage(ui::Flex* page, Popup&);
void niriAnimationsPage(ui::Flex* page, Popup&);
void niriBehaviourPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
