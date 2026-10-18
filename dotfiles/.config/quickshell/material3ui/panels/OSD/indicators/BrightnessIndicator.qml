import "../../../services"
import QtQuick
import Quickshell
import Quickshell.Hyprland
import "../../widgets"
import ".." 

OsdValueIndicator {
    id: root
    property var focusedScreen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name)
    property var brightnessMonitor: Brightness.getMonitorForScreen(focusedScreen)
    readonly property bool dimming: Hyprsunset.gamma !== 100

    icon: dimming ? "wb_twilight" : "light_mode"
    name: dimming ? "Dim" : "Brightness"
    value: dimming
        ? (Hyprsunset.gamma - Hyprsunset.gammaLowerLimit) / (100 - Hyprsunset.gammaLowerLimit)
        : (root.brightnessMonitor !== undefined ? root.brightnessMonitor.brightness : 0.5)
}
