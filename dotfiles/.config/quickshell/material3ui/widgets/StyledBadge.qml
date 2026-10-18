import QtQuick
import "../core"

Rectangle {
    id: rootBadge
    
    // Properties
    property int count: 0
    property int maxCount: 99
    
    // Styling properties (defaulting to M3 standard)
    property int badgeHeight: 16
    property int fontSize: 11
    property int horizontalPadding: 4
    
    // Only show if count > 0
    visible: count > 0
    
    // M3 dimensions
    width: Math.max(badgeHeight * Appearance.effectiveScale, badgeText.implicitWidth + (horizontalPadding * 2 * Appearance.effectiveScale))
    height: badgeHeight * Appearance.effectiveScale
    radius: (badgeHeight / 2) * Appearance.effectiveScale
    
    // M3 Colors
    color: Appearance.m3colors.m3error

    StyledText {
        id: badgeText
        anchors.centerIn: parent
        text: rootBadge.count > rootBadge.maxCount ? rootBadge.maxCount + "+" : rootBadge.count.toString()
        font.pixelSize: Math.round(rootBadge.fontSize * Appearance.effectiveScale)
        font.weight: Font.DemiBold
        color: Appearance.m3colors.m3onError
    }
}
