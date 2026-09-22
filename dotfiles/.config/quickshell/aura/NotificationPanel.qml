import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: root; signal close(); radius: 28; color: shell.glass; border.color: shell.outline
    ColumnLayout { anchors.fill: parent; anchors.margins: 22; RowLayout { Layout.fillWidth: true; Label { text: "Notifications"; font.pixelSize: 24; color: shell.text; Layout.fillWidth: true }; Switch { text: "Do not disturb"; checked: shell.dnd; onToggled: shell.dnd = checked }; ToolButton { text: "×"; onClicked: root.close() } }
        Repeater { model: [["System update", "A system update is ready to install."], ["LibreWolf", "Downloads completed."]]; delegate: Rectangle { Layout.fillWidth: true; implicitHeight: 92; radius: 18; color: shell.surfaceHigh; Column { anchors.fill: parent; anchors.margins: 15; spacing: 7; Text { text: modelData[0]; color: shell.text; font.bold: true }; Text { text: modelData[1]; color: shell.muted; wrapMode: Text.Wrap } } } }
        Item { Layout.fillHeight: true }; Button { text: "Clear all"; Layout.alignment: Qt.AlignRight; onClicked: root.close() }
    }
}
