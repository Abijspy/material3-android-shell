import QtQuick
import "../core"

// Round (or pill) button with hover/press state layers, like the shell's RippleButton.
Rectangle {
    id: root

    property string icon: ""
    property real iconSize: 20 * Theme.s
    property color background: "transparent"
    property color foreground: Theme.onSurfaceVariant
    property bool spinning: false
    signal clicked()

    implicitWidth: 40 * Theme.s
    implicitHeight: 40 * Theme.s
    radius: height / 2
    color: background

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: root.foreground
        opacity: !root.enabled ? 0 : mouse.pressed ? 0.16 : mouse.containsMouse ? 0.08 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
    }

    onSpinningChanged: if (!spinning) glyph.rotation = 0

    Icon {
        id: glyph
        anchors.centerIn: parent
        text: root.icon
        iconSize: root.iconSize
        color: root.foreground
        RotationAnimation on rotation {
            running: root.spinning
            from: 0; to: 360; duration: 900
            loops: Animation.Infinite
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
