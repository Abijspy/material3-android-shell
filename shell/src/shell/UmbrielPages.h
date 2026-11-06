#pragma once

// Umbriel Settings, the window Niri Settings is on niri: Umbriel's options,
// read from what's in effect (your config.toml over what it includes, else
// Umbriel's default) and written into config.toml as they change
// (services/UmbrielConfig). Umbriel reloads it as it's saved
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void umbrielLayoutPage(ui::Flex* page, Popup&);
void umbrielDecorationPage(ui::Flex* page, Popup&);
void umbrielAnimationsPage(ui::Flex* page, Popup&);
void umbrielInputPage(ui::Flex* page, Popup&);
void umbrielBehaviourPage(ui::Flex* page, Popup&);
void umbrielStartupPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
