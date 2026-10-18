pragma ComponentBehavior: Bound

import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland

Scope {
    id: root

    Loader {
        active: GlobalStates.timePickerOpen
        sourceComponent: Variants {
            model: Quickshell.screens
            delegate: PanelWindow {
                id: panelWindow
                required property var modelData
                screen: modelData

                readonly property bool isActive: GlobalStates.activeScreen === modelData
                visible: GlobalStates.timePickerOpen && isActive

                anchors { top: true; left: true; right: true; bottom: true }
                color: "transparent"
                WlrLayershell.namespace: "material3ui:timepicker"
                WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand
                WlrLayershell.layer: GlobalStates.timePickerOpen && isActive ? WlrLayer.Overlay : WlrLayer.Background
                exclusionMode: ExclusionMode.Ignore

                Shortcut {
                    sequence: "Escape"
                    onActivated: {
                        if (GlobalStates.timePickerOnCancelled)
                            GlobalStates.timePickerOnCancelled()
                        GlobalStates.timePickerOpen = false
                    }
                }

                Component.onCompleted: GlobalFocusGrab.addDismissable(panelWindow)
                Component.onDestruction: GlobalFocusGrab.removeDismissable(panelWindow)

                Connections {
                    target: GlobalFocusGrab
                    function onDismissed() {
                        if (GlobalStates.timePickerOnCancelled)
                            GlobalStates.timePickerOnCancelled()
                        GlobalStates.timePickerOpen = false
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: Functions.ColorUtils.applyAlpha(Appearance.colors.colLayer0, 0.6)
                    opacity: GlobalStates.timePickerOpen && isActive ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (GlobalStates.timePickerOnCancelled)
                                GlobalStates.timePickerOnCancelled()
                            GlobalStates.timePickerOpen = false
                        }
                    }
                }

                TimePicker {
                    id: picker
                    anchors.centerIn: parent
                    currentTimeStr: GlobalStates.timePickerCurrentTime
                    is24Hour: GlobalStates.timePickerIs24Hour

                    onTimeSelected: timeStr => {
                        if (GlobalStates.timePickerOnSelected)
                            GlobalStates.timePickerOnSelected(timeStr)
                        GlobalStates.timePickerOpen = false
                    }

                    onCancelled: {
                        if (GlobalStates.timePickerOnCancelled)
                            GlobalStates.timePickerOnCancelled()
                        GlobalStates.timePickerOpen = false
                    }
                }
            }
        }
    }
}
