#pragma once

// Settings → Hyprland: every compositor option, grouped into pages
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void hyprWindowsPage(ui::Flex* page, Popup&);
void hyprDecorationPage(ui::Flex* page, Popup&);
void hyprInputPage(ui::Flex* page, Popup&);
void hyprCursorPage(ui::Flex* page, Popup&);
void hyprGesturesPage(ui::Flex* page, Popup&);
void hyprLayoutsPage(ui::Flex* page, Popup&);
void hyprGroupsPage(ui::Flex* page, Popup&);
void hyprMiscPage(ui::Flex* page, Popup&);
void hyprRenderPage(ui::Flex* page, Popup&);
void hyprDebugPage(ui::Flex* page, Popup&);
void hyprAnimationsPage(ui::Flex* page, Popup&);
void hyprMonitorsPage(ui::Flex* page, Popup&);
void hyprStartupPage(ui::Flex* page, Popup&);
void hyprEnvironmentPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
