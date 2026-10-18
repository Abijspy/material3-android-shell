import QtQuick
import QtQuick.Layouts
import "../../../core"
import "../../../services"
import "../../../widgets"
import ".."

/**
 * GPU detail page for System Monitor.
 * Formatted identically to CpuPage for design consistency and height symmetry.
 */
Item {
    id: root

    readonly property var currentGpu: SystemData.availableGpus.length > 0 ? SystemData.availableGpus[0] : null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20 * Appearance.effectiveScale
        spacing: 20 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("GPU Performance")
            font.pixelSize: Appearance.font.pixelSize.huge
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
        }

        // Main GPU Card (Matching CpuPage dimensions, padding, and layout)
        Rectangle {
            visible: SystemData.hasValidGpuData
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Appearance.colors.colLayer2
            radius: 16 * Appearance.effectiveScale
            border.width: 0
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20 * Appearance.effectiveScale
                
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 0
                        StyledText {
                            text: root.currentGpu ? root.currentGpu.name : "GPU"
                            font.pixelSize: Appearance.font.pixelSize.normal
                            font.weight: Font.Medium
                            color: Appearance.m3colors.m3onSurface
                        }
                        StyledText { 
                            text: root.currentGpu ? `${root.currentGpu.vendor} ` + I18nService.tr("Graphics Engine") : I18nService.tr("Graphics Card")
                            color: Appearance.colors.colSubtext
                            font.pixelSize: Appearance.font.pixelSize.smaller
                        }
                    }
                    Item { Layout.fillWidth: true }
                    StyledText { 
                        text: root.currentGpu ? Math.round(root.currentGpu.usage || 0) + "%" : "0%"
                        font.pixelSize: Math.round(32 * Appearance.effectiveScale)
                        font.weight: Font.DemiBold
                        color: Appearance.m3colors.m3tertiary
                    }
                }
                
                PerformanceGraph {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    history: SystemData.gpuHistory
                    lineColor: Appearance.m3colors.m3tertiary
                    fillColor: Appearance.m3colors.m3tertiary
                    maxValue: 100
                }
                
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 20 * Appearance.effectiveScale
                    
                    ColumnLayout {
                        spacing: 2 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("TEMPERATURE"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText {
                            text: (root.currentGpu && root.currentGpu.temp > 0) ? Math.round(root.currentGpu.temp) + "°C" : "--°C"
                            font.weight: Font.Medium
                            font.pixelSize: Appearance.font.pixelSize.small
                        }
                    }

                    ColumnLayout {
                        spacing: 2 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("VENDOR"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText {
                            text: root.currentGpu ? root.currentGpu.vendor : "--"
                            font.weight: Font.Medium
                            font.pixelSize: Appearance.font.pixelSize.small
                        }
                    }

                    Item { Layout.fillWidth: true }

                    ColumnLayout {
                        spacing: 2 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("UPTIME"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText { text: SystemData.uptime; font.weight: Font.Medium; font.pixelSize: Appearance.font.pixelSize.small; horizontalAlignment: Text.AlignRight }
                    }
                }
            }
        }

        // Fallback Card (Matching CpuPage height)
        Rectangle {
            visible: !SystemData.hasValidGpuData
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Appearance.colors.colLayer2
            radius: 16 * Appearance.effectiveScale
            border.width: 0

            ColumnLayout {
                anchors.centerIn: parent
                Layout.preferredWidth: parent.width * 0.8
                spacing: 12 * Appearance.effectiveScale
                MaterialSymbol {
                    text: "videogame_asset_off"
                    iconSize: 48 * Appearance.effectiveScale
                    color: Appearance.colors.colSubtext
                    Layout.alignment: Qt.AlignCenter
                }
                StyledText {
                    text: I18nService.tr("GPU performance data not available")
                    font.pixelSize: Appearance.font.pixelSize.normal
                    font.weight: Font.Medium
                    color: Appearance.m3colors.m3onSurface
                    Layout.alignment: Qt.AlignCenter
                }
                StyledText {
                    text: I18nService.tr("Your GPU does not report usage or temperature data to system sensors.")
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.m3colors.m3onSurfaceVariant
                    Layout.alignment: Qt.AlignCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
