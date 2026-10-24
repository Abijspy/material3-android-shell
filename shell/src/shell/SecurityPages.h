#pragma once

// Settings → VPN and Settings → Firewall
namespace m3::ui {
class Flex;
}

namespace m3::shell {
class Popup;
}

namespace m3::shell::panels {

void vpnPage(ui::Flex* page, Popup&);
void firewallPage(ui::Flex* page, Popup&);

} // namespace m3::shell::panels
