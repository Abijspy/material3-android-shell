#pragma once

namespace m3 {

// Notifications about the battery (Settings → Battery → Alerts): low at
// 10% and very low at 5% on battery (once each per discharge), a reminder
// to unplug at 80, 85 or 90% for the battery's health (off until chosen),
// and fully charged (off until switched on)
class BatteryAlerts {
public:
    static BatteryAlerts& get();
    void start(); // once

private:
    BatteryAlerts() = default;
    void check();
    int m_lowTold = 101; // the lowest level told of since unplugging
    bool m_unplugTold = false, m_fullTold = false, m_started = false;
};

} // namespace m3
