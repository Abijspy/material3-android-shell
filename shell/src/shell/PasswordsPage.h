#pragma once

// Settings → Passwords: Wi-Fi networks' passwords and what apps saved in
// the keyring, shown only when asked
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void passwordsPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
