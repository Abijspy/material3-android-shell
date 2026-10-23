#pragma once

#include <memory>
#include <string>

struct sd_bus_slot;

namespace m3::quickshare {

// The Bluetooth LE nudge before sending: phones only make themselves
// findable on the network (their mDNS service) after hearing a Quick Share
// advertisement nearby, service 0xFE2C with Google's fixed bytes. Through
// BlueZ's LEAdvertisingManager1, for as long as this lives.
class BleBeacon {
public:
    BleBeacon();
    ~BleBeacon();
    BleBeacon(const BleBeacon&) = delete;
    BleBeacon& operator=(const BleBeacon&) = delete;

    bool on() const { return m_registered; }
    const std::string& error() const { return m_error; }

private:
    sd_bus_slot* m_object = nullptr;
    std::string m_adapter, m_error;
    std::string m_data; // the service data
    bool m_registered = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace m3::quickshare
