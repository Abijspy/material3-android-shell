import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell.Io

Rectangle {
    id: root
    property bool expanded: false
    property string media: "No media playing"
    implicitWidth: expanded ? 430 : 260
    implicitHeight: expanded ? 104 : 34
    radius: expanded ? 23 : 18
    color: Qt.rgba(shell.surfaceHigh.r, shell.surfaceHigh.g, shell.surfaceHigh.b, shell.glassOpacity)
    border.color: shell.outline
    border.width: 1
    Behavior on implicitWidth { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on implicitHeight { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    Component.onCompleted: mediaQuery.running = true
    Timer { interval: 4000; running: true; repeat: true; onTriggered: { root.media = "No media playing"; mediaQuery.running = true } }
    Process { id: mediaQuery; command: ["sh", "-lc", "playerctl metadata --format '{{artist}} — {{title}}' 2>/dev/null || true"]
        stdout: SplitParser { onRead: data => { if (data.length > 0) root.media = data } }
    }
    MouseArea { anchors.fill: parent; onClicked: root.expanded = !root.expanded }
    ColumnLayout { anchors.fill: parent; anchors.margins: 8; spacing: 4
        RowLayout { Layout.fillWidth: true; spacing: 8
            Label { text: "●"; color: shell.primary; font.pixelSize: 17 }
            Label { text: root.media; color: shell.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: Qt.formatDateTime(new Date(), "HH:mm"); color: shell.muted; font.pixelSize: 12 }
        }
        RowLayout { visible: root.expanded; Layout.fillWidth: true; spacing: 10
            Button { text: "◀"; onClicked: shell.run("playerctl previous") }
            Button { text: "▶ / ❚❚"; Layout.fillWidth: true; onClicked: shell.run("playerctl play-pause") }
            Button { text: "▶"; onClicked: shell.run("playerctl next") }
            Button { text: "⌁"; onClicked: shell.toggle("control") }
        }
    }
}
