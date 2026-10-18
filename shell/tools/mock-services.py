#!/usr/bin/env python3
"""Mock desktop services for headless tests of the native shell.

  mock-services.py system   NetworkManager, UPower and BlueZ on the system bus
  mock-services.py session  an MPRIS player and two tray items on the session bus

Only the properties and methods the shell uses are implemented.
"""
import sys
import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

DBusGMainLoop(set_as_default=True)
PROPS = "org.freedesktop.DBus.Properties"


class PropObject(dbus.service.Object):
    """An object with GetAll/Get/Set over a dict of interface → properties."""

    def __init__(self, bus, path, props):
        super().__init__(bus, path)
        self.props = props

    @dbus.service.method(PROPS, in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):
        return self.props.get(iface, {})

    @dbus.service.method(PROPS, in_signature="ss", out_signature="v")
    def Get(self, iface, name):
        return self.props[iface][name]

    @dbus.service.method(PROPS, in_signature="ssv")
    def Set(self, iface, name, value):
        self.props[iface][name] = value
        self.PropertiesChanged(iface, {name: value}, [])

    @dbus.service.signal(PROPS, signature="sa{sv}as")
    def PropertiesChanged(self, iface, changed, invalidated):
        pass


def system():
    bus = dbus.SystemBus()
    NM = "org.freedesktop.NetworkManager"
    names = [dbus.service.BusName(n, bus) for n in (NM, "org.freedesktop.UPower", "org.bluez")]
    o = dbus.ObjectPath
    objs = [
        PropObject(bus, "/org/freedesktop/NetworkManager", {NM: {
            "State": dbus.UInt32(70), "WirelessEnabled": True,
            "PrimaryConnection": o("/org/freedesktop/NetworkManager/ActiveConnection/1")}}),
        PropObject(bus, "/org/freedesktop/NetworkManager/ActiveConnection/1", {NM + ".Connection.Active": {
            "Type": "802-11-wireless", "Id": "Home",
            "Devices": dbus.Array([o("/org/freedesktop/NetworkManager/Devices/1")], signature="o")}}),
        PropObject(bus, "/org/freedesktop/NetworkManager/Devices/1", {NM + ".Device.Wireless": {
            "ActiveAccessPoint": o("/org/freedesktop/NetworkManager/AccessPoint/1")}}),
        PropObject(bus, "/org/freedesktop/NetworkManager/AccessPoint/1", {NM + ".AccessPoint": {
            "Strength": dbus.Byte(72), "Ssid": dbus.ByteArray(b"Home-5G")}}),
        PropObject(bus, "/org/freedesktop/UPower/devices/DisplayDevice", {"org.freedesktop.UPower.Device": {
            "IsPresent": True, "Type": dbus.UInt32(2), "Percentage": 76.0, "State": dbus.UInt32(2),
            "TimeToEmpty": dbus.Int64(9000), "TimeToFull": dbus.Int64(0)}}),
    ]

    class BlueZ(dbus.service.Object):
        @dbus.service.method("org.freedesktop.DBus.ObjectManager", out_signature="a{oa{sa{sv}}}")
        def GetManagedObjects(self):
            return {
                o("/org/bluez/hci0"): {"org.bluez.Adapter1": {"Powered": True}},
                o("/org/bluez/hci0/dev_00"): {"org.bluez.Device1": {"Connected": True, "Alias": "WH-1000XM4"}},
            }

    objs.append(BlueZ(bus, "/"))
    return names, objs


def session():
    bus = dbus.SessionBus()
    player_iface = "org.mpris.MediaPlayer2.Player"

    class Player(PropObject):
        @dbus.service.method(player_iface)
        def PlayPause(self):
            p = self.props[player_iface]
            p["PlaybackStatus"] = "Paused" if p["PlaybackStatus"] == "Playing" else "Playing"
            self.PropertiesChanged(player_iface, {"PlaybackStatus": p["PlaybackStatus"]}, [])

        @dbus.service.method(player_iface)
        def Next(self):
            md = dbus.Dictionary({"xesam:title": "Save Your Tears", "xesam:artist": dbus.Array(["The Weeknd"])},
                                 signature="sv")
            self.props[player_iface]["Metadata"] = md
            self.PropertiesChanged(player_iface, {"Metadata": md}, [])

    player_name = dbus.service.BusName("org.mpris.MediaPlayer2.mockplayer", bus)
    player = Player(bus, "/org/mpris/MediaPlayer2", {
        "org.mpris.MediaPlayer2": {"Identity": "Mock Player"},
        player_iface: {"PlaybackStatus": "Playing", "CanGoNext": True, "CanGoPrevious": True,
                       "Metadata": dbus.Dictionary({"xesam:title": "Blinding Lights",
                                                    "xesam:artist": dbus.Array(["The Weeknd"])}, signature="sv")}})

    # A 32×32 pixmap: an orange disc (ARGB, network order)
    px = bytearray()
    for y in range(32):
        for x in range(32):
            inside = (x - 15.5) ** 2 + (y - 15.5) ** 2 <= 14 ** 2
            px += bytes([255, 255, 140, 40]) if inside else bytes([0, 0, 0, 0])
    item_iface = "org.kde.StatusNotifierItem"
    items = []
    for n, props in enumerate([
        {"Id": "mock-pixmap", "Title": "Pixmap item", "Status": "Active", "IconName": "",
         "IconPixmap": dbus.Array([(dbus.Int32(32), dbus.Int32(32), dbus.ByteArray(bytes(px)))], signature="(iiay)")},
        {"Id": "mock-named", "Title": "Named icon", "Status": "Active", "IconName": "folder"},
    ]):
        class Item(PropObject):
            @dbus.service.method(item_iface, in_signature="ii")
            def Activate(self, x, y):
                print(f"tray activate {self.props[item_iface]['Id']} at {x},{y}", flush=True)

        # Registered by object path, so the watcher must combine it with the sender
        items.append((f"/StatusNotifierItem/{n}", Item(bus, f"/StatusNotifierItem/{n}", {item_iface: props})))

    def register():
        watcher = bus.get_object("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher")
        for name, _ in items:
            watcher.RegisterStatusNotifierItem(name, dbus_interface="org.kde.StatusNotifierWatcher")
        print("tray items registered", flush=True)
        return False

    # The shell must own the watcher first
    GLib.timeout_add(float(sys.argv[2]) * 1000 if len(sys.argv) > 2 else 1500, register)
    return player_name, player, items


keep = system() if sys.argv[1] == "system" else session()
GLib.MainLoop().run()
