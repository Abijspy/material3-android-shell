import QtQuick
import QtQuick.Effects
import "../core"

// Circular avatar from the synced picture, falling back to the user's initial.
Item {
    id: root
    required property string username
    property string displayName: username
    property real size: 72 * Theme.s

    implicitWidth: size
    implicitHeight: size

    readonly property string source: {
        const a = Theme.avatars[root.username];
        return a ? "file://" + a : "";
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.primaryContainer
        visible: img.status !== Image.Ready

        Label {
            anchors.centerIn: parent
            text: (root.displayName || "?").charAt(0).toUpperCase()
            font.pixelSize: root.size * 0.42
            font.weight: Font.Medium
            color: Theme.onPrimaryContainer
        }
    }

    Image {
        id: img
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectCrop
        visible: false
        asynchronous: true
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: width / 2
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: img
        maskEnabled: true
        maskSource: mask
        visible: img.status === Image.Ready
    }
}
