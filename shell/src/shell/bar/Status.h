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

class WifiGlyph;
class BatteryPill;
class StatusGlyph;

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
    StatusGlyph* m_netIcon;
    // Each reacts when its state flips: Bluetooth on or off, sound muted or
    // not, Do not disturb, a new notification (StatusGlyph)
    StatusGlyph *m_btIcon, *m_volIcon, *m_bellIcon;
    WifiGlyph* m_wifi; // Wi-Fi in Android 16's style; the glyph for the rest
    bool m_known = false, m_btOn = false, m_muted = false, m_dnd = false;
    size_t m_unread = 0;
    Connection m_a, m_b, m_c, m_d;
};

// Charge in the bar, as Material 3 Expressive draws it (BatteryPill): red
// below 20%, yellow while charging, green once charged; opens the battery
// popup (status and power mode)
class BatteryStatus : public ui::Button {
public:
    explicit BatteryStatus(OpenPopup open, bool vertical = false);

private:
    void sync();
    BatteryPill* m_pill;
    Connection m_conn, m_theme;
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
