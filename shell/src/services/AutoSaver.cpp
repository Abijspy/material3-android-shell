#include "services/AutoSaver.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Process.h"
#include "services/Network.h"

#include <algorithm>
#include <format>

namespace m3 {

AutoSaver& AutoSaver::get() {
    static AutoSaver instance;
    return instance;
}

bool AutoSaver::enabled() {
    return Config::get().value<bool>("/battery/autoSaver", true);
}

int AutoSaver::threshold() {
    return std::clamp(Config::get().value<int>("/battery/autoSaverAt", 20), 5, 50);
}

void AutoSaver::start() {
    Power::get().changed.connectForever([this] { check(); });
    check();
}

void AutoSaver::check() {
    auto& p = Power::get();
    auto& c = Config::get();
    if (!p.hasBattery() || p.profiles().empty()) return;
    const bool ours = c.value<bool>("/battery/autoSaverOn", false); // power saver is on because of this
    if (p.pluggedIn()) {
        if (!ours) return;
        // Charging again: back as it was, unless the mode was changed by hand meanwhile
        c.set("/battery/autoSaverOn", false);
        const std::string before = c.value<std::string>("/battery/autoSaverWas", "balanced");
        if (p.profile() == "power-saver" && before != "power-saver") {
            log::info("power saver: back to {} on the charger", before);
            p.setProfile(before);
        }
        return;
    }
    if (!enabled() || ours || p.percent() > threshold() || p.profile() == "power-saver") return;
    if (std::ranges::find(p.profiles(), std::string("power-saver")) == p.profiles().end()) return;
    log::info("power saver: on at {}%", p.percent());
    c.set("/battery/autoSaverWas", p.profile());
    c.set("/battery/autoSaverOn", true);
    p.setProfile("power-saver");
    run({"notify-send", "-a", "Battery", "-i", "battery-caution", "Power saver is on", std::format("Battery at {}%: back to the usual mode once you plug in", p.percent())},
        nullptr);
}

} // namespace m3
