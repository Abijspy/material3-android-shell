import "../core"
import "."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    required property string text
    property bool shown: false
    property alias font: tooltipTextObject.font

    property real horizontalPadding: 10
    property real verticalPadding: 5
    property bool nowrap: false

    implicitWidth: backgroundRectangle.implicitWidth
    implicitHeight: backgroundRectangle.implicitHeight

    Rectangle {
        id: backgroundRectangle
        anchors.bottom: root.bottom
        anchors.horizontalCenter: root.horizontalCenter
        color: Appearance.colors.colTooltip
        radius: Appearance.rounding.verysmall
        opacity: shown ? 1 : 0
        implicitWidth: shown ? (tooltipTextObject.implicitWidth + 2 * root.horizontalPadding) : 0
        implicitHeight: shown ? (tooltipTextObject.implicitHeight + 2 * root.verticalPadding) : 0
        clip: true

        Behavior on implicitWidth {
            animation: Appearance.animation.elementMoveFast.numberAnimation.createObject(this)
        }
        Behavior on implicitHeight {
            animation: Appearance.animation.elementMoveFast.numberAnimation.createObject(this)
        }
        Behavior on opacity {
            animation: Appearance.animation.elementMoveFast.numberAnimation.createObject(this)
        }

        StyledText {
            id: tooltipTextObject
            anchors.centerIn: parent
            text: root.text
            font.pixelSize: Appearance.font.pixelSize.smaller
            font.hintingPreference: Font.PreferNoHinting
            color: Appearance.colors.colOnTooltip
            wrapMode: root.nowrap ? Text.NoWrap : Text.Wrap
        }
    }
}
