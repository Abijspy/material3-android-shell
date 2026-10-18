import QtQuick
import "../core"

/**
 * Password characters drawn as Material shapes (circle, pill, diamond,
 * squircle, petal) that pop in as you type, echoing the lock screen.
 * A ListModel keeps existing shapes stable while typing.
 */
Item {
    id: root

    required property int length
    property real charSize: 18 * Theme.s
    property color color: Theme.primary
    property bool active: true

    clip: true

    ListModel { id: dotModel }
    onLengthChanged: {
        while (dotModel.count > length) dotModel.remove(dotModel.count - 1);
        while (dotModel.count < length) dotModel.append({ shape: Math.floor(Math.random() * 5) });
    }

    Row {
        id: dots
        anchors.verticalCenter: parent.verticalCenter
        x: Math.min(12 * Theme.s, root.width - width - 12 * Theme.s)
        spacing: 4 * Theme.s
        Behavior on x { NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic } }

        Repeater {
            model: dotModel
            delegate: Item {
                id: dot
                required property int shape
                width: root.charSize
                height: root.charSize

                Rectangle {
                    anchors.centerIn: parent
                    color: root.color
                    // 0 circle, 1 pill, 2 diamond, 3 squircle, 4 petal
                    width: dot.shape === 1 ? parent.width * 0.55 : dot.shape === 2 ? parent.width * 0.7 : parent.width * 0.85
                    height: dot.shape === 1 ? parent.height : width
                    radius: dot.shape === 3 ? width * 0.3 : dot.shape === 2 ? width * 0.15 : dot.shape === 4 ? width * 0.5 : width / 2
                    rotation: dot.shape === 2 ? 45 : dot.shape === 4 ? 30 : dot.shape === 1 ? 20 : 0
                    antialiasing: true

                    scale: 0
                    Component.onCompleted: scale = 1
                    Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                }
            }
        }
    }

    // Caret
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        x: dots.x + dots.width + (root.length > 0 ? 3 * Theme.s : 0)
        width: Math.max(1, 2 * Theme.s)
        height: root.charSize
        radius: width / 2
        color: root.color
        visible: root.active
        SequentialAnimation on opacity {
            running: root.active
            loops: Animation.Infinite
            NumberAnimation { to: 0; duration: 500; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1; duration: 500; easing.type: Easing.InOutSine }
        }
    }
}
