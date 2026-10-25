#pragma once

#include "core/Signal.h"
#include "ui/Node.h"

#include <functional>
#include <map>
#include <string>

namespace m3::shell {

// Callback a bar widget uses to open its popup, with where along the bar to
// anchor it (x on a top or bottom bar, y on a side one: alongBar)
using OpenPopup = std::function<void(const std::string& name, float along)>;

// Network, Bluetooth and volume icons in one capsule; each opens its popup.
// `vertical`: stacked, for a side bar (as the others below)
class StatusIcons : public ui::Flex {
public:
    explicit StatusIcons(OpenPopup open, bool vertical = false);

private:
    void sync();
    ui::Button* m_net;
    ui::Button* m_bt;
    ui::Button* m_vol;
    ui::Button* m_bell;
    ui::Icon *m_netIcon, *m_btIcon, *m_volIcon, *m_bellIcon;
    Connection m_a, m_b, m_c, m_d;
};

// Charge in the bar; opens the battery popup (status and power mode)
class BatteryStatus : public ui::Button {
public:
    explicit BatteryStatus(OpenPopup open, bool vertical = false);

private:
    void sync();
    ui::Icon* m_icon;
    ui::Label* m_label;
    bool m_vertical;
    Connection m_conn;
};

// Now playing: icon + "Title · Artist"; click toggles playback, scroll skips.
// On a side bar, the ring alone (the rest in its tooltip)
class Media : public ui::Button {
public:
    explicit Media(bool vertical = false);

private:
    void sync();
    ui::Icon* m_icon;
    ui::Label* m_label;
    Connection m_conn;
};

// StatusNotifier items as icons.
class TrayIcons : public ui::Flex {
public:
    explicit TrayIcons(OpenPopup open, bool vertical = false);

private:
    void sync();
    OpenPopup m_open;
    Connection m_conn;
};

} // namespace m3::shell
