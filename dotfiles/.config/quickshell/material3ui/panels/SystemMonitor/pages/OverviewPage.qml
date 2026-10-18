import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../../../core"
import "../../../core/functions" as Functions
import "../../../services"
import "../../../widgets"
import ".."

/**
 * Overview page for the System Monitor.
 * Displays a summary of all key metrics.
 */
Item {
    id: root

    Flickable {
        anchors.fill: parent
        contentHeight: mainLayout.implicitHeight + (40 * Appearance.effectiveScale)
        clip: true
        
        ScrollBar.vertical: StyledScrollBar {}

        ColumnLayout {
            id: mainLayout
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                margins: 20 * Appearance.effectiveScale
            }
            spacing: 24 * Appearance.effectiveScale

            // --- Header Row (Title on Left, Stats on Right via justify-between) ---
            RowLayout {
                Layout.fillWidth: true
                spacing: 16 * Appearance.effectiveScale

                StyledText {
                    text: I18nService.tr("System Overview")
                    font.pixelSize: Appearance.font.pixelSize.huge
                    font.family: Appearance.font.family.title
                    font.weight: Font.DemiBold
                    color: Appearance.m3colors.m3onSurface
                    Layout.alignment: Qt.AlignVCenter
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    spacing: 24 * Appearance.effectiveScale
                    Layout.alignment: Qt.AlignVCenter

                    ColumnLayout {
                        spacing: 1 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("UPTIME"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText { text: SystemData.uptime || "--"; font.pixelSize: Appearance.font.pixelSize.small; font.weight: Font.Medium; color: Appearance.m3colors.m3onSurface }
                    }

                    ColumnLayout {
                        spacing: 1 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("LOAD AVG"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText { text: SystemData.loadAverage || "--"; font.pixelSize: Appearance.font.pixelSize.small; font.weight: Font.Medium; color: Appearance.m3colors.m3onSurface }
                    }

                    ColumnLayout {
                        spacing: 1 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("PROCESSES"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText { text: `${SystemData.processCount} (${SystemData.threadCount} ` + I18nService.tr("threads") + `)`; font.pixelSize: Appearance.font.pixelSize.small; font.weight: Font.Medium; color: Appearance.m3colors.m3onSurface }
                    }
                }
            }

            // Top Row: Performance Graphs
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: 16 * Appearance.effectiveScale
                rowSpacing: 16 * Appearance.effectiveScale

                GraphCard {
                    title: I18nService.tr("CPU Usage")
                    value: Math.round(SystemData.cpuUsage * 100) + "%"
                    subValue: `${SystemData.cpuTemperature}°C`
                    history: SystemData.cpuHistory
                    accentColor: Appearance.colors.colPrimary
                    Layout.fillWidth: true
                }

                GraphCard {
                    title: I18nService.tr("GPU Usage")
                    value: SystemData.availableGpus.length > 0 ? Math.round(SystemData.availableGpus[0].usage) + "%" : "0%"
                    subValue: SystemData.availableGpus.length > 0 && SystemData.availableGpus[0].temp > 0 ? `${SystemData.availableGpus[0].temp}°C` : ""
                    history: SystemData.gpuHistory
                    accentColor: Appearance.m3colors.m3tertiary
                    Layout.fillWidth: true
                }


                GraphCard {
                    title: I18nService.tr("Memory")
                    value: Math.round(SystemData.memUsage * 100) + "%"
                    subValue: `${SystemData.usedMemoryMB}MB / ${SystemData.totalMemoryMB}MB`
                    history: SystemData.memHistory
                    accentColor: "#8AB4F8"
                    Layout.fillWidth: true
                }

                GraphCard {
                    title: I18nService.tr("Network")
                    value: (SystemData.networkRxRate / (1024 * 1024)).toFixed(2) + " MB/s"
                    subValue: `↓${(SystemData.networkRxRate / 1024).toFixed(0)}KB/s ↑${(SystemData.networkTxRate / 1024).toFixed(0)}KB/s`
                    accentColor: "#81C995"
                    Layout.fillWidth: true
                    
                    // Mirrored graph
                    customGraph: Component {
                        Item {
                            anchors.fill: parent
                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0
                                
                                PerformanceGraph {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.preferredHeight: 1
                                    history: SystemData.networkRxHistory
                                    lineColor: "#81C995"
                                    fillColor: "#81C995"
                                    maxValue: 1024 * 5
                                }
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 2 * Appearance.effectiveScale
                                    color: Appearance.m3colors.m3primary
                                    opacity: 0.5
                                    z: 10
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

                GraphCard {
                    title: I18nService.tr("Disk I/O")
                    value: (SystemData.diskTotalRate / (1024 * 1024)).toFixed(2) + " MB/s"
                    subValue: `R:${(SystemData.diskReadRate / (1024 * 1024)).toFixed(1)}MB/s W:${(SystemData.diskWriteRate / (1024 * 1024)).toFixed(1)}MB/s`
                    history: SystemData.diskReadHistory
                    accentColor: Appearance.m3colors.m3error
                    Layout.fillWidth: true
                    Layout.columnSpan: 2
                    Layout.preferredHeight: 140 * Appearance.effectiveScale
                }
            }
            
            Item { Layout.fillHeight: true }
        }
    }

    // Helper Card Component
    component GraphCard: Rectangle {
        id: card
        property string title
        property string value
        property string subValue: ""
        property var history
        property color accentColor
        property Component customGraph: null
        
        Layout.preferredHeight: 180 * Appearance.effectiveScale
        radius: 16 * Appearance.effectiveScale
        color: Appearance.colors.colLayer2
        border.color: Functions.ColorUtils.applyAlpha(card.accentColor, 0.25)
        border.width: Math.max(1, 1 * Appearance.effectiveScale)
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16 * Appearance.effectiveScale
            spacing: 0
            
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 12 * Appearance.effectiveScale
                ColumnLayout {
                    spacing: -2 * Appearance.effectiveScale
                    StyledText {
                        text: I18nService.tr(card.title)
                        font.pixelSize: Appearance.font.pixelSize.small
                        font.weight: Font.Medium
                        color: Appearance.m3colors.m3onSurfaceVariant
                    }
                    StyledText {
                        visible: card.subValue !== ""
                        text: card.subValue
                        font.pixelSize: Appearance.font.pixelSize.smallest
                        font.weight: Font.Medium
                        color: Appearance.colors.colSubtext
                    }
                }
                Item { Layout.fillWidth: true }
                StyledText {
                    text: card.value
                    font.pixelSize: Appearance.font.pixelSize.large
                    font.family: Appearance.font.family.numbers
                    font.weight: Font.DemiBold
                    color: Appearance.m3colors.m3onSurface
                }
            }
            
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: card.customGraph ? card.customGraph : defaultGraph
                
                Component {
                    id: defaultGraph
                    PerformanceGraph {
                        anchors.fill: parent
                        history: card.history
                        lineColor: card.accentColor
                        fillColor: card.accentColor
                        maxValue: 100
                    }
                }
            }
        }
    }
}
