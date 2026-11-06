#include "services/BatteryAlerts.h"

#include "core/Config.h"
#include "core/Process.h"
#include "services/Network.h"

#include <format>
#include <string>

namespace m3 {

BatteryAlerts& BatteryAlerts::get() {
    static BatteryAlerts b;
    return b;
}

void BatteryAlerts::start() {
    if (m_started) return;
    m_started = true;
    Power::get().changed.connectForever([this] { check(); });
    check();
}

void BatteryAlerts::check() {
    auto& p = Power::get();
    if (!p.hasBattery()) return;
    const auto& c = Config::get();
    const int percent = p.percent();
    auto notify = [](const std::string& icon, bool critical, const std::string& title, const std::string& body) {
        std::vector<std::string> cmd = {"notify-send", "-a", "Battery", "-i", icon};
        if (critical) cmd.insert(cmd.end(), {"-u", "critical"});
        cmd.insert(cmd.end(), {title, body});
        run(cmd, nullptr);
    };

    if (p.pluggedIn()) {
        m_lowTold = 101;
        // Battery care: enough, before it's full
        const int unplugAt = c.value<int>("/battery/unplugAt", 0);
        if (unplugAt > 0 && percent >= unplugAt && !m_unplugTold && p.charging()) {
            m_unplugTold = true;
            notify("battery-full-charging", false, std::format("Charged to {}%", percent),
                   "Unplug the charger to keep the battery healthy");
        }
        if (p.full() && !m_fullTold && c.value<bool>("/battery/fullAlert", false)) {
            m_fullTold = true;
            notify("battery-full-charged", false, "Fully charged", "You can unplug the charger");
        }
        return;
    }
    // On battery: told again next time it charges
    m_unplugTold = m_fullTold = false;
    if (!c.value<bool>("/battery/lowAlerts", true)) return;
    const long long left = p.secondsLeft();
    const std::string time = left >= 120 ? std::format(", about {}", left >= 3600 ? std::format("{} h {} min", left / 3600, left % 3600 / 60)
                                                                                    : std::format("{} min", left / 60))
                                         : "";
    if (percent <= 5 && m_lowTold > 5) {
        m_lowTold = 5;
        notify("battery-empty", true, "Battery very low",
               std::format("{}% left{}. Plug in now, or save your work: the computer turns off soon.", percent, time));
    } else if (percent <= 10 && m_lowTold > 10) {
        m_lowTold = 10;
        notify("battery-caution", true, "Battery low", std::format("{}% left{}. Plug in soon.", percent, time));
    }
}

} // namespace m3
