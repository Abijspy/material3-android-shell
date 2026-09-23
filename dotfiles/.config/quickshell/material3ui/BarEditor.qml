import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    visible: shell.page === "Bar settings"
    Layout.fillWidth: true
    implicitHeight: editor.implicitHeight + 32
    radius: 20
    color: shell.surfaceContainer
    property var widgets: [
        ["launcher", "App launcher", "Open apps and commands"],
        ["workspaces", "Workspaces", "Hyprland workspace switcher"],
        ["activeWindow", "Active window", "Focused Hyprland window title"],
        ["dynamicIsland", "Dynamic Island", "Media and quick controls"],
        ["screenshot", "Screenshot", "Capture a selected area"],
        ["clipboard", "Clipboard", "Open cliphist history"],
        ["notifications", "Notifications", "Unread notification indicator"],
        ["quickSettings", "Quick settings", "Connectivity and audio panel"],
        ["clock", "Clock & calendar", "Date and time"],
        ["battery", "Battery", "UPower percentage and state"],
        ["profile", "Profile", "Account and power menu"]
    ]
    ColumnLayout { id: editor; anchors.fill: parent; anchors.margins: 16; spacing: 4
        Label { text: "Bar editor"; color: shell.text; font.pixelSize: 18; font.weight: Font.DemiBold }
        Label { text: "Toggle widgets. The layout updates immediately and is saved for the next session."; color: shell.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Rectangle { Layout.fillWidth: true; implicitHeight: styleControls.implicitHeight + 18; radius: 16; color: shell.surfaceHigh
            ColumnLayout { id: styleControls; anchors.fill: parent; anchors.margins: 10; spacing: 4
                RowLayout { Layout.fillWidth: true; Label { text: "Blur / translucency"; color: shell.text; Layout.fillWidth: true }; Label { text: Math.round(shell.glassOpacity * 100) + "%"; color: shell.muted } }
                Slider { Layout.fillWidth: true; from: 0.55; to: 1; value: shell.glassOpacity; onMoved: shell.glassOpacity = value; onPressedChanged: if (!pressed) shell.run("material3ui-system bar-style opacity " + value.toFixed(2)) }
                RowLayout { Layout.fillWidth: true; Label { text: "Bar height"; color: shell.text; Layout.fillWidth: true }; Label { text: shell.barHeight + " px"; color: shell.muted } }
                Slider { Layout.fillWidth: true; from: 36; to: 72; stepSize: 1; value: shell.barHeight; onMoved: shell.barHeight = Math.round(value); onPressedChanged: if (!pressed) shell.run("material3ui-system bar-style height " + Math.round(value)) }
                RowLayout { Layout.fillWidth: true; Label { text: "Placement"; color: shell.text; Layout.fillWidth: true }; ComboBox { model: ["top", "bottom"]; currentIndex: shell.barPosition === "bottom" ? 1 : 0; onActivated: { shell.barPosition = currentText; shell.run("material3ui-system bar-style position " + currentText) } } }
            }
        }
        Repeater { model: root.widgets
            delegate: RowLayout { Layout.fillWidth: true; Layout.topMargin: 5
                ColumnLayout { Layout.fillWidth: true; Label { text: modelData[1]; color: shell.text; font.pixelSize: 15 }; Label { text: modelData[2]; color: shell.muted; font.pixelSize: 11 } }
                Switch { checked: shell.widgetEnabled(modelData[0]); onToggled: { shell.setBarWidget(modelData[0], checked); shell.run("material3ui-system bar-widget " + modelData[0] + " " + checked) } }
            }
        }
    }
}
