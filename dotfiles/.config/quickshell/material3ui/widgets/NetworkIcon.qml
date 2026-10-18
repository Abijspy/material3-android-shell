import QtQuick
import Quickshell.Widgets
import "../core"
import "../core/functions" as Functions

Item {
    id: root
    
    // Auto-calculate based on strength, or use overrides for global states like ethernet/disconnected
    property int strength: -1
    property string overrideBackground: ""
    property string overrideForeground: ""
    
    property real iconSize: 16 * Appearance.effectiveScale
    property color color: Appearance.colors.colOnLayer1
    property color backgroundColor: Functions.ColorUtils.applyAlpha(root.color, 0.3)
    
    width: iconSize
    height: iconSize
    
    readonly property string finalBg: root.overrideBackground !== "" ? root.overrideBackground : (root.strength >= 0 ? "wifi" : "")
    readonly property string finalFg: root.overrideForeground !== "" ? root.overrideForeground : (
        root.strength > 66 ? "wifi" :
        root.strength > 33 ? "wifi_2_bar" :
        root.strength > 0 ? "wifi_1_bar" : ""
    )
    
    MaterialSymbol {
        anchors.centerIn: parent
        visible: root.finalBg !== ""
        text: root.finalBg
        iconSize: root.iconSize
        fill: 1
        color: root.backgroundColor
    }
    
    MaterialSymbol {
        anchors.centerIn: parent
        visible: root.finalFg !== ""
        text: root.finalFg
        iconSize: root.iconSize
        fill: 1
        color: root.color
    }
}
