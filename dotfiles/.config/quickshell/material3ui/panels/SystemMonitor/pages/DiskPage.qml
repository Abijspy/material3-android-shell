import QtQuick
import QtQuick.Layouts
import "../../../core"
import "../../../services"
import "../../../widgets"
import ".."

/**
 * Disk detail page for System Monitor.
 */
Item {
    id: root

    Flickable {
        anchors.fill: parent
        contentHeight: contentColumn.implicitHeight + (40 * Appearance.effectiveScale)
        clip: true
        interactive: true
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: contentColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20 * Appearance.effectiveScale
            spacing: 20 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Disk Performance")
            font.pixelSize: Appearance.font.pixelSize.huge
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
        }

        // Real-time Disk I/O Card
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 140 * Appearance.effectiveScale
            color: Appearance.colors.colLayer2
            radius: 16 * Appearance.effectiveScale
            border.width: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20 * Appearance.effectiveScale
                spacing: 16 * Appearance.effectiveScale

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        StyledText { text: I18nService.tr("Total Throughput"); font.pixelSize: Appearance.font.pixelSize.small; color: Appearance.m3colors.m3onSurfaceVariant }
                        StyledText { 
                            text: ((SystemData.diskReadRate + SystemData.diskWriteRate) / (1024 * 1024)).toFixed(2) + " MB/s"
                            font.pixelSize: Appearance.font.pixelSize.huge
                            font.family: Appearance.font.family.numbers
                            font.weight: Font.DemiBold
                            color: Appearance.m3colors.m3onSurface
                        }
                    }
                    Item { Layout.fillWidth: true }
                    MaterialSymbol {
                        text: "speed"
                        iconSize: 32 * Appearance.effectiveScale
                        color: Appearance.m3colors.m3error
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 24 * Appearance.effectiveScale
                    
                    ColumnLayout {
                        spacing: 2 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("READ"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText { 
                            text: (SystemData.diskReadRate / (1024 * 1024)).toFixed(2) + " MB/s"
                            font.family: Appearance.font.family.numbers
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.Medium
                            color: Appearance.m3colors.m3onSurface
                        }
                    }
                    
                    Rectangle { Layout.preferredWidth: 1 * Appearance.effectiveScale; Layout.fillHeight: true; color: Appearance.colors.colLayer3; opacity: 0.5 }
                    
                    ColumnLayout {
                        spacing: 2 * Appearance.effectiveScale
                        StyledText { text: I18nService.tr("WRITE"); font.pixelSize: Appearance.font.pixelSize.smallest; font.weight: Font.Medium; color: Appearance.colors.colSubtext }
                        StyledText { 
                            text: (SystemData.diskWriteRate / (1024 * 1024)).toFixed(2) + " MB/s"
                            font.family: Appearance.font.family.numbers
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.Medium
                            color: Appearance.m3colors.m3onSurface
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                }
            }
        }

        StyledText {
            text: I18nService.tr("Disk Operations")
            Layout.topMargin: 12 * Appearance.effectiveScale
            font.pixelSize: Appearance.font.pixelSize.large
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
        }

        // Monitors each disk in the list
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 24 * Appearance.effectiveScale

            Repeater {
                model: SystemData.diskStats
                delegate: ColumnLayout {
                    required property string path
                    required property string label
                    required property bool hasAlias
                    required property real usage
                    required property real total
                    required property real used

                    Layout.fillWidth: true
                    spacing: 8 * Appearance.effectiveScale

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8 * Appearance.effectiveScale
                        MaterialSymbol {
                            text: "storage"
                            iconSize: 18 * Appearance.effectiveScale
                            color: Appearance.m3colors.m3primary
                        }
                        StyledText {
                            text: hasAlias ? I18nService.tr("%1 DISK USAGE").replace("%1", label.toUpperCase()) : I18nService.tr("\"%1\" DISK USAGE").replace("%1", label.toUpperCase())
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.DemiBold
                            color: Appearance.m3colors.m3outline
                            Layout.fillWidth: true
                        }
                        StyledText {
                            text: `${Math.round(usage * 100)}%`
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.DemiBold
                            color: Appearance.m3colors.m3onSurface
                        }
                    }

                    // Large Disk Bar
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 12 * Appearance.effectiveScale
                        radius: 6 * Appearance.effectiveScale
                        color: Appearance.colors.colLayer2
                        clip: true

                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, usage))
                            height: parent.height
                            radius: 6 * Appearance.effectiveScale
                            color: Appearance.m3colors.m3primary
                            visible: usage > 0

                            Behavior on width {
                                NumberAnimation { duration: 500; easing.type: Easing.OutCubic }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        StyledText {
                            text: path
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            color: Appearance.colors.colSubtext
                        }
                        Item { Layout.fillWidth: true }
                        StyledText {
                            text: (used / (1024*1024*1024)).toFixed(1) + " GB / " + (total / (1024*1024*1024)).toFixed(1) + " GB"
                            font.family: Appearance.font.family.numbers
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            color: Appearance.colors.colSubtext
                        }
                    }
                }
            }
        }

        }
    }
}
