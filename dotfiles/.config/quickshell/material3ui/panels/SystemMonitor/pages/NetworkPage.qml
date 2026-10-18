import QtQuick
import QtQuick.Layouts
import "../../../core"
import "../../../services"
import "../../../widgets"
import ".."

/**
 * Network detail page for System Monitor.
 */
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20 * Appearance.effectiveScale
        spacing: 20 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Network Activity")
            font.pixelSize: Appearance.font.pixelSize.huge
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Appearance.colors.colLayer2
            radius: 16 * Appearance.effectiveScale
            border.width: 0
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20 * Appearance.effectiveScale
                spacing: 0
                
                RowLayout {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 20 * Appearance.effectiveScale
                    ColumnLayout {
                        spacing: 0
                        StyledText { text: I18nService.tr("Network Bandwidth"); font.pixelSize: Appearance.font.pixelSize.large; font.weight: Font.Medium; color: Appearance.m3colors.m3onSurface; font.family: Appearance.font.family.title }
                        StyledText { text: I18nService.tr("Total Activity: %1 MB/s").replace("%1", ((SystemData.networkTotalRate) / (1024 * 1024)).toFixed(2)); color: Appearance.colors.colSubtext; font.pixelSize: Appearance.font.pixelSize.smaller }
                    }
                    Item { Layout.fillWidth: true }
                    RowLayout {
                        spacing: 24 * Appearance.effectiveScale
                        ColumnLayout {
                            spacing: 0
                            StyledText { text: I18nService.tr("DOWNLOAD"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: "#81C995" }
                            StyledText { text: (SystemData.networkRxRate / (1024 * 1024)).toFixed(2) + " MB/s"; font.pixelSize: Appearance.font.pixelSize.normal; font.weight: Font.DemiBold; color: Appearance.m3colors.m3onSurface; font.family: Appearance.font.family.numbers }
                        }
                        ColumnLayout {
                            spacing: 0
                            StyledText { text: I18nService.tr("UPLOAD"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: "#FF8A65" }
                            StyledText { text: (SystemData.networkTxRate / (1024 * 1024)).toFixed(2) + " MB/s"; font.pixelSize: Appearance.font.pixelSize.normal; font.weight: Font.DemiBold; color: Appearance.m3colors.m3onSurface; horizontalAlignment: Text.AlignRight; font.family: Appearance.font.family.numbers }
                        }
                    }
                }
                
                PerformanceGraph {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 1
                    history: SystemData.networkRxHistory
                    lineColor: "#81C995"
                    fillColor: "#81C995"
                    maxValue: 1024 * 5 // 5MB/s scaling
                }

                // Center line
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 2 * Appearance.effectiveScale
                    color: Appearance.m3colors.m3outlineVariant
                    opacity: 0.5
                }

                PerformanceGraph {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 1
                    history: SystemData.networkTxHistory
                    lineColor: "#FF8A65"
                    fillColor: "#FF8A65"
                    inverted: true
                    maxValue: 1024 * 5
                }
            }
        }
    }
}
