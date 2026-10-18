import "../../core"
import "../../widgets"
import QtQuick

RippleButton {
    id: button

    property string buttonText: ""
    property string tooltipText: ""
    property bool forceCircle: false
    property bool compact: false

    implicitHeight: (compact ? 30 : 36) * Appearance.effectiveScale
    implicitWidth: forceCircle ? implicitHeight : (contentItem.implicitWidth + (10 * 2) * Appearance.effectiveScale)
    background.anchors.fill: button
    buttonRadius: Appearance.rounding.full
    colBackground: Appearance.colors.colLayer2
    colBackgroundHover: Appearance.colors.colLayer2Hover
    colRipple: Appearance.colors.colLayer2Active

    StyledToolTip {
        text: tooltipText
        extraVisibleCondition: tooltipText.length > 0
    }

    Behavior on implicitWidth {
        SmoothedAnimation {
            velocity: Appearance.animation.elementMove.velocity
        }

    }

    contentItem: StyledText {
        text: buttonText
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Appearance.font.pixelSize.larger
        font.family: Appearance.font.family.title
        color: Appearance.colors.colOnLayer1
    }

}
