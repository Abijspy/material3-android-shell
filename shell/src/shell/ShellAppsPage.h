#pragma once

// Settings → Material3UI apps: the shell's own apps, and the removed ones
// brought back (their packages reinstalled)
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void shellAppsPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
