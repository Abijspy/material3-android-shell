import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import "../core"

// The lock screen's floating island: surfaceContainer pill with a soft shadow.
Item {
    id: root
    default property alias contents: row.data
    property alias spacing: row.spacing
    property real padding: 8 * Theme.s

    implicitHeight: 56 * Theme.s
    implicitWidth: row.implicitWidth + padding * 2

    RectangularShadow {
        anchors.fill: bg
        radius: bg.radius
        offset: Qt.vector2d(0, 4 * Theme.s)
        blur: 12 * Theme.s
        color: Qt.rgba(0, 0, 0, 0.25)
    }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: height / 2
        color: Theme.surfaceContainer
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: root.padding
        spacing: 4 * Theme.s
    }
}
