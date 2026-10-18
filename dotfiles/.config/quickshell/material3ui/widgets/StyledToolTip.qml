import "../core"
import "."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolTip {
    id: root
    property bool extraVisibleCondition: true
    property bool alternativeVisibleCondition: false
    property bool nowrap: false

    readonly property bool internalVisibleCondition: (extraVisibleCondition && (
        (parent && (parent.hovered || parent.realHovered)) ||
        (parent && parent.parent && (parent.parent.hovered || parent.parent.realHovered)) ||
        (parent && parent.parent && parent.parent.parent && (parent.parent.parent.hovered || parent.parent.parent.realHovered))
    )) || alternativeVisibleCondition
    
    verticalPadding: 5
    horizontalPadding: 10
    background: null
    
    font {
        family: Appearance.font.family.main
        variableAxes: Appearance.font.variableAxes.main
        pixelSize: Appearance.font.pixelSize.smaller
        hintingPreference: Font.PreferNoHinting
    }

    delay: 0
    visible: internalVisibleCondition
    
    contentItem: StyledToolTipContent {
        id: contentItem
        font: root.font
        text: root.text
        shown: root.internalVisibleCondition
        verticalPadding: root.verticalPadding
        horizontalPadding: root.horizontalPadding
        nowrap: root.nowrap
    }
}
