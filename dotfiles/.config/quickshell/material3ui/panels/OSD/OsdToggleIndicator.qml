import "../../core"
import "../../widgets"
import "../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts
import Quickshell

/**
 * Refactored OSD Toggle Indicator (Power Mode/Layout/Charging)
 * Simplified structure to eliminate rendering noise.
 */
Item {
    id: root
    
    // Required properties
    property string icon: ""
    property string name: ""
    property string statusText: ""
    
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

            // ── Slot Tengah: Main Content (Text Pill) ──
            Rectangle {
                id: textWrapper
                Layout.fillWidth: true
                Layout.preferredHeight: 36 * Appearance.effectiveScale
                Layout.alignment: Qt.AlignVCenter
                radius: 18 * Appearance.effectiveScale
                color: Appearance.m3colors.m3surfaceContainerHighest
                
                Text {
                    anchors.centerIn: parent
                    text: root.statusText !== "" ? root.statusText : root.name
                    font.pixelSize: Math.round(15 * Appearance.effectiveScale)
                    font.weight: Font.Medium
                    color: Appearance.colors.colOnLayer1
                    elide: Text.ElideRight
                    
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    renderType: Text.NativeRendering
                }
            }
        }
    }
}
