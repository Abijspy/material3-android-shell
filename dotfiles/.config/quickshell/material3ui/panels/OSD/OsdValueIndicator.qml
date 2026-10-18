import "../../core"
import "../../widgets"
import "../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts
import Quickshell

/**
 * Refactored OSD Value Indicator (Volume/Brightness)
 * Simplified structure to eliminate rendering noise.
 */
Item {
    id: root
    
    // Required properties
    property real value: 0
    property string icon: ""
    property string name: ""

    // Root dimensions for the Loader/PanelWindow
    implicitWidth: 360 * Appearance.effectiveScale
    implicitHeight: 56 * Appearance.effectiveScale

    StyledRectangularShadow {
        target: valueIndicator
        z: -1
    }

    Rectangle {
        id: valueIndicator
        anchors.fill: parent
        radius: height / 2
        color: Appearance.m3colors.m3surfaceContainer

        RowLayout {
            id: valueRow
            anchors.fill: parent
            anchors.leftMargin: 8 * Appearance.effectiveScale
            anchors.rightMargin: 8 * Appearance.effectiveScale
            anchors.topMargin: 10 * Appearance.effectiveScale
            anchors.bottomMargin: 10 * Appearance.effectiveScale
            spacing: 8 * Appearance.effectiveScale

            // ── Slot Kiri: Icon Wrapper ──
            Item {
                Layout.preferredWidth: 36 * Appearance.effectiveScale
                Layout.preferredHeight: 36 * Appearance.effectiveScale
                Layout.alignment: Qt.AlignVCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: height / 2
                    color: Appearance.m3colors.m3primaryContainer
                    
                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: root.icon
                        iconSize: 22 * Appearance.effectiveScale
                        color: Appearance.m3colors.m3onPrimaryContainer
                    }
                }
            }

            // ── Slot Tengah: Main Content (Sleeker StyledSlider) ──
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 36 * Appearance.effectiveScale
                Layout.alignment: Qt.AlignVCenter
                
                StyledSlider {
                    id: portedSlider
                    anchors.centerIn: parent
                    width: parent.width
                    
                    value: root.value
                    from: 0
                    to: 1
                    enabled: false
                    
                    trackWidth: 36 * Appearance.effectiveScale
                    trackRadius: 12 * Appearance.effectiveScale
                    handleHeight: 48 * Appearance.effectiveScale
                    showTrailingDot: false
                    animateValue: true
                    
                    handleMargins: 4 * Appearance.effectiveScale
                    highlightColor: Appearance.m3colors.m3primary
                    trackColor: Appearance.m3colors.m3surfaceContainerHighest
                    handleColor: Appearance.m3colors.m3primary
                }
            }

            // ── Slot Kanan: Value Indicator (Text Only) ──
            Item {
                id: valueSlot
                Layout.preferredWidth: 36 * Appearance.effectiveScale
                Layout.preferredHeight: 36 * Appearance.effectiveScale
                Layout.alignment: Qt.AlignVCenter
                
                Text {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 1 * Appearance.effectiveScale
                    text: Math.round(root.value * 100)
                    font.pixelSize: Math.round(15 * Appearance.effectiveScale)
                    font.family: Appearance.font.family.numbers
                    font.features: { "tnum": 1 }
                    font.weight: Font.DemiBold
                    color: Appearance.m3colors.m3onSurface
                    
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    renderType: Text.NativeRendering
                }
            }
        }
    }
}
