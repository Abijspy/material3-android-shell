//@ pragma UseQApplication
//@ pragma Env QS_NO_RELOAD_POPUP=1
//@ pragma Env QT_QUICK_CONTROLS_STYLE=Basic

// Material3UI greeter for greetd. Launched by greet-hyprland.sh inside a
// minimal Hyprland session; quitting Quickshell ends that session.

import QtQuick
import Quickshell
import Quickshell.Wayland
import "core"

ShellRoot {
    Variants {
        model: Quickshell.screens

        PanelWindow {
            id: window
            required property var modelData
            readonly property bool primary: modelData === Quickshell.screens[0]

            screen: modelData
            anchors { top: true; bottom: true; left: true; right: true }
            exclusionMode: ExclusionMode.Ignore
            color: Theme.surfaceContainerLowest
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "material3ui-greeter"
            WlrLayershell.keyboardFocus: primary ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None

            GreeterSurface {
                anchors.fill: parent
                primary: window.primary
            }
        }
    }
}
