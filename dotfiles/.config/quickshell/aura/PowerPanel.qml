import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle { id: root; signal close(); radius: 30; color: shell.glass; border.color: shell.outline
    ColumnLayout { anchors.fill: parent; anchors.margins: 26; spacing: 16; RowLayout { Layout.fillWidth: true; Label { text: "Power menu"; color: shell.text; font.pixelSize: 26; Layout.fillWidth: true }; ToolButton { text: "×"; onClicked: root.close() } }
        Label { text: "Choose an action for this session"; color: shell.muted }
        RowLayout { Layout.fillWidth: true; Repeater { model: [["◐", "Sleep", "loginctl suspend"], ["↻", "Restart", "systemctl reboot"], ["⏻", "Power off", "systemctl poweroff"]]; delegate: Button { Layout.fillWidth: true; implicitHeight: 112; onClicked: shell.run(modelData[2]); contentItem: Column { spacing: 10; anchors.centerIn: parent; Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData[0]; font.pixelSize: 30; color: shell.primary }; Label { text: modelData[1]; color: shell.text } } } } }
        Button { text: "Lock screen"; Layout.fillWidth: true; onClicked: shell.run("hyprlock") }
    }
}
