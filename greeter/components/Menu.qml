import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import "../core"

// Floating Material 3 menu card listing choices (users or sessions).
Item {
    id: root

    property var model: []          // [{ label, sublabel, icon }]
    property int currentIndex: -1
    property bool open: false
    signal picked(int index)

    implicitWidth: 280 * Theme.s
    implicitHeight: list.implicitHeight + 16 * Theme.s
    visible: opacity > 0
    opacity: open ? 1 : 0
    scale: open ? 1 : 0.92
    transformOrigin: Item.Bottom
    Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
    Behavior on scale { NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutBack } }

    RectangularShadow {
        anchors.fill: card
        radius: card.radius
        offset: Qt.vector2d(0, 6 * Theme.s)
        blur: 18 * Theme.s
        color: Qt.rgba(0, 0, 0, 0.3)
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: 24 * Theme.s
        color: Theme.surfaceContainerHigh
    }

    ColumnLayout {
        id: list
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 * Theme.s }
        spacing: 2 * Theme.s

        Repeater {
            model: root.model
            delegate: Rectangle {
                id: item
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex

                Layout.fillWidth: true
                implicitHeight: 52 * Theme.s
                radius: 18 * Theme.s
                color: selected ? Theme.secondaryContainer : hover.containsMouse ? Theme.withAlpha(Theme.onSurface, 0.08) : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14 * Theme.s
                    anchors.rightMargin: 14 * Theme.s
                    spacing: 12 * Theme.s

                    Icon {
                        text: item.modelData.icon || "radio_button_unchecked"
                        fill: item.selected ? 1 : 0
                        color: item.selected ? Theme.onSecondaryContainer : Theme.onSurfaceVariant
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: item.modelData.label
                            font.weight: Font.Medium
                            color: item.selected ? Theme.onSecondaryContainer : Theme.onSurface
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !!item.modelData.sublabel
                            text: item.modelData.sublabel || ""
                            font.pixelSize: 12 * Theme.s
                            color: Theme.onSurfaceVariant
                        }
                    }
                    Icon {
                        visible: item.selected
                        text: "check"
                        color: Theme.onSecondaryContainer
                    }
                }

                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.picked(item.index)
                }
            }
        }
    }
}
