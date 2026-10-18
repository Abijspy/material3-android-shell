import QtQuick
import QtQuick.Shapes

/**
 * Decorative concave round corner piece.
 * Draws the "negative space" corner so a rectangular background visually
 * matches the rounded screen edges below it.
 *
 * Usage:
 *   RoundCorner {
 *       implicitSize: 20
 *       color: barBackground.color
 *       corner: RoundCorner.CornerEnum.BottomLeft
 *   }
 */
Item {
    id: root

    enum CornerEnum { TopLeft, TopRight, BottomLeft, BottomRight }
    property var corner: RoundCorner.CornerEnum.BottomLeft
    property real implicitSize: 20
    property color color: "transparent"

    implicitWidth: implicitSize
    implicitHeight: implicitSize

    readonly property bool isTop:    corner === RoundCorner.CornerEnum.TopLeft    || corner === RoundCorner.CornerEnum.TopRight
    readonly property bool isLeft:   corner === RoundCorner.CornerEnum.TopLeft    || corner === RoundCorner.CornerEnum.BottomLeft
    readonly property bool isBottom: corner === RoundCorner.CornerEnum.BottomLeft || corner === RoundCorner.CornerEnum.BottomRight
    readonly property bool isRight:  corner === RoundCorner.CornerEnum.TopRight   || corner === RoundCorner.CornerEnum.BottomRight

    Item {
        anchors.fill: parent
        clip: true

        Rectangle {
            width: root.implicitSize * 4
            height: root.implicitSize * 4
            radius: root.implicitSize * 2
            color: "transparent"
            border.color: root.color
            border.width: root.implicitSize
            
            x: root.isLeft ? -root.implicitSize : -root.implicitSize * 2
            y: root.isTop ? -root.implicitSize : -root.implicitSize * 2
        }
    }
}
