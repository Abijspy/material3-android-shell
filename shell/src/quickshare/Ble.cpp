#include "quickshare/Ble.h"

#include "core/Log.h"
#include "dbus/Bus.h"
#include "quickshare/Crypto.h"
#include "services/Network.h"

#include <systemd/sd-bus.h>

namespace m3::quickshare {

namespace {

constexpr const char* kBlueZ = "org.bluez";
constexpr const char* kPath = "/org/material3ui/quickshare/beacon";
constexpr const char* kUuid = "fe2c";

int getType(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "s", "broadcast");
}

int getUuids(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
    return sd_bus_message_append(reply, "as", 1, kUuid);
}

// {"fe2c": <ay>}
int getServiceData(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* self, sd_bus_error*) {
    const std::string& data = *static_cast<std::string*>(self);
    int r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_open_container(reply, 'e', "sv");
    if (r >= 0) r = sd_bus_message_append(reply, "s", kUuid);
    if (r >= 0) r = sd_bus_message_open_container(reply, 'v', "ay");
    if (r >= 0) r = sd_bus_message_append_array(reply, 'y', data.data(), data.size());
    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_message_close_container(reply);
    if (r >= 0) r = sd_bus_message_close_container(reply);
    return r;
}

int onRelease(sd_bus_message* m, void*, sd_bus_error*) {
    return sd_bus_reply_method_return(m, "");
}

const sd_bus_vtable kVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", onRelease, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Type", "s", getType, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", getUuids, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceData", "a{sv}", getServiceData, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

} // namespace

BleBeacon::BleBeacon() {
    auto& bt = Bluetooth::get();
    if (!bt.available() || bt.adapter().empty()) {
        m_error = "No Bluetooth adapter";
        return;
    }
    if (!bt.powered()) {
        m_error = "Bluetooth is off";
        return;
    }
    auto& bus = dbus::Bus::system();
    m_adapter = bt.adapter();
    // Fixed bytes, then 10 random ones (as Android's share sheet sends)
    m_data = std::string("\xfc\x12\x8e\x01\x42\x00\x00\x00\x00\x00\x00\x00\x00\x00", 14) + crypto::random(10);
    if (sd_bus_add_object_vtable(bus.raw(), &m_object, kPath, "org.bluez.LEAdvertisement1", kVtable, &m_data) < 0) {
        m_error = "Couldn't make the Bluetooth advertisement";
        return;
    }
    std::weak_ptr<bool> alive = m_alive;
    bus.call(kBlueZ, m_adapter, "org.bluez.LEAdvertisingManager1", "RegisterAdvertisement",
             [](sd_bus_message* m) {
                 sd_bus_message_append(m, "o", kPath);
                 sd_bus_message_append(m, "a{sv}", 0);
             },
             [this, alive](const dbus::Value&, const std::string& err) {
                 if (alive.expired()) {
                     // Gone before BlueZ answered: take it back
                     if (err.empty())
                         dbus::Bus::system().call(kBlueZ, Bluetooth::get().adapter(), "org.bluez.LEAdvertisingManager1",
                                                  "UnregisterAdvertisement", dbus::args(dbus::ObjectPath{kPath}));
                     return;
                 }
                 if (!err.empty()) {
                     m_error = "Bluetooth couldn't advertise (" + err + ")";
                     log::warn("quick share: {}", m_error);
                     return;
                 }
                 m_registered = true;
             });
}

BleBeacon::~BleBeacon() {
    m_alive.reset();
    if (m_registered)
        dbus::Bus::system().call(kBlueZ, m_adapter, "org.bluez.LEAdvertisingManager1", "UnregisterAdvertisement",
                                 dbus::args(dbus::ObjectPath{kPath}));
    if (m_object) sd_bus_slot_unref(m_object);
}

} // namespace m3::quickshare
