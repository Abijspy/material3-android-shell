import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import "../../../core"
import "../../../core/functions" as Functions
import "../../../services"
import "../../../widgets"

/**
 * Polished Battery Stats page for System Monitor.
 * Features OpacityMask hero battery bar, native stat cards,
 * and clean Hardware Information details section.
 */
Flickable {
    id: root
    contentHeight: mainCol.implicitHeight + (40 * Appearance.effectiveScale)
    clip: true
    
    // Smooth value for battery bar
    property real displayPercentage: Battery.percentage
    Behavior on displayPercentage { NumberAnimation { duration: 800; easing.type: Easing.OutCubic } }

    ColumnLayout {
        id: mainCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 20 * Appearance.effectiveScale
        spacing: 20 * Appearance.effectiveScale

        // Header Title
        StyledText {
            text: I18nService.tr("Battery & Power")
            font.pixelSize: Appearance.font.pixelSize.huge
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
            color: Appearance.m3colors.m3onSurface
        }

        // ── 1. Hero Battery Overview Card ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: heroCol.implicitHeight + (32 * Appearance.effectiveScale)
            radius: 16 * Appearance.effectiveScale
            color: Appearance.colors.colLayer2
            border.width: Math.max(1, 1 * Appearance.effectiveScale)
            border.color: Functions.ColorUtils.applyAlpha(Appearance.colors.colOutlineVariant, 0.15)

            ColumnLayout {
                id: heroCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: 20 * Appearance.effectiveScale
                anchors.rightMargin: 20 * Appearance.effectiveScale
                anchors.topMargin: 14 * Appearance.effectiveScale
                anchors.bottomMargin: 18 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale

                // Hero Section: Dashboard Circular Gauge + Status Text
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 32 * Appearance.effectiveScale

                    // Left: Circular Progress Gauge
                    Item {
                        width: 120 * Appearance.effectiveScale
                        height: 120 * Appearance.effectiveScale
                        Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter
                        Layout.preferredWidth: 120 * Appearance.effectiveScale
                        Layout.preferredHeight: 120 * Appearance.effectiveScale

                        GappedCircularProgress {
                            anchors.fill: parent
                            progress: root.displayPercentage
                            strokeWidth: 12 * Appearance.effectiveScale
                            
                            readonly property color barColor: {
                                if (Battery.isCritical && !Battery.isCharging) return Appearance.colors.colError;
                                if (Battery.isLow && !Battery.isCharging) return Appearance.colors.colWarning;
                                if (Battery.isCharging) return Appearance.m3colors.m3success;
                                return Appearance.m3colors.m3primary;
                            }
                            colPrimary: barColor
                            colSecondary: Functions.ColorUtils.applyAlpha(Appearance.colors.colOnLayer1, 0.12)
                            
                            Behavior on colPrimary { ColorAnimation { duration: 300 } }
                        }

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 0

                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                visible: Battery.isCharging
                                width: 20 * Appearance.effectiveScale
                                height: 20 * Appearance.effectiveScale
                                Layout.bottomMargin: -4 * Appearance.effectiveScale

                                MaterialSymbol {
                                    anchors.centerIn: parent
                                    text: "bolt"
                                    iconSize: 24 * Appearance.effectiveScale
                                    color: Appearance.m3colors.m3success
                                }
                            }

                            StyledText {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.maximumWidth: 72 * Appearance.effectiveScale
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 16 * Appearance.effectiveScale
                                text: Math.round(root.displayPercentage * 100)
                                font.pixelSize: Math.round(34 * Appearance.effectiveScale)
                                font.weight: Font.DemiBold
                                color: Appearance.m3colors.m3onSurface
                            }
                        }
                    }

                    // Right: Status Column
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 4 * Appearance.effectiveScale

                        StyledText {
                            text: Battery.isCharging ? I18nService.tr("Charging") : (Battery.chargeState === 4 ? I18nService.tr("Fully Charged") : I18nService.tr("Discharging"))
                            font.pixelSize: Math.round(26 * Appearance.effectiveScale)
                            font.weight: Font.DemiBold
                            color: Battery.isCharging ? Appearance.m3colors.m3success : Appearance.m3colors.m3onSurface
                        }

                        StyledText {
                            text: Battery.isPluggedIn ? I18nService.tr("Power Source: AC Adapter") : I18nService.tr("Power Source: Battery")
                            font.pixelSize: Appearance.font.pixelSize.normal
                            font.weight: Font.Medium
                            color: Appearance.colors.colSubtext
                        }

                        StyledText {
                            visible: (Battery.isCharging && Battery.timeToFull > 0) || (!Battery.isCharging && Battery.timeToEmpty > 0)
                            text: {
                                if (Battery.isCharging && Battery.timeToFull > 0) return I18nService.tr("%1 mins until full").replace("%1", Math.round(Battery.timeToFull / 60));
                                if (!Battery.isCharging && Battery.timeToEmpty > 0) return I18nService.tr("%1 mins remaining").replace("%1", Math.round(Battery.timeToEmpty / 60));
                                return "";
                            }
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.Medium
                            color: Appearance.colors.colSubtext
                        }
                    }
                }
            }
        }

        // ── 2. Stat Cards with Unique MaterialShape Badges ──
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 16 * Appearance.effectiveScale
            rowSpacing: 16 * Appearance.effectiveScale

            StatCard {
                Layout.fillWidth: true
                label: I18nService.tr("Health")
                value: Battery.health > 0 ? (Math.round(Battery.health) + "%") : "N/A"
                icon: "favorite"
                materialShape: MaterialShape.Shape.Cookie12Sided
            }

            StatCard {
                Layout.fillWidth: true
                label: I18nService.tr("Usage Rate")
                value: Battery.energyRate > 0 ? (Battery.energyRate.toFixed(1) + " W") : "0.0 W"
                icon: "bolt"
                materialShape: MaterialShape.Shape.SoftBurst
            }

            StatCard {
                Layout.fillWidth: true
                label: I18nService.tr("Voltage")
                value: Battery.voltage > 0 ? (Battery.voltage.toFixed(2) + " V") : "N/A"
                icon: "electric_bolt"
                materialShape: MaterialShape.Shape.Clover4Leaf
            }

            StatCard {
                Layout.fillWidth: true
                label: I18nService.tr("Cycles")
                value: Battery.cycles > 0 ? Battery.cycles.toString() : "0"
                icon: "autorenew"
                materialShape: MaterialShape.Shape.Cookie7Sided
            }
        }

        // ── 3. Technical & Hardware Specifications Card ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: techCol.implicitHeight + (32 * Appearance.effectiveScale)
            radius: 16 * Appearance.effectiveScale
            color: Appearance.colors.colLayer2
            border.width: Math.max(1, 1 * Appearance.effectiveScale)
            border.color: Functions.ColorUtils.applyAlpha(Appearance.colors.colOutlineVariant, 0.15)

            ColumnLayout {
                id: techCol
                anchors.fill: parent
                anchors.margins: 16 * Appearance.effectiveScale
                spacing: 16 * Appearance.effectiveScale

                RowLayout {
                    spacing: 8 * Appearance.effectiveScale
                    MaterialSymbol {
                        text: "info"
                        iconSize: 18 * Appearance.effectiveScale
                        color: Appearance.colors.colPrimary
                    }
                    StyledText {
                        text: I18nService.tr("Hardware Information")
                        font.pixelSize: Appearance.font.pixelSize.normal
                        font.weight: Font.DemiBold
                        color: Appearance.m3colors.m3onSurface
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 16 * Appearance.effectiveScale
                    columnSpacing: 32 * Appearance.effectiveScale

                    TechInfo { label: I18nService.tr("Vendor"); value: Battery.vendor || I18nService.tr("Unknown") }
                    TechInfo { label: I18nService.tr("Model"); value: Battery.model || I18nService.tr("Generic Battery") }
                    TechInfo { label: I18nService.tr("Technology"); value: Battery.technology || I18nService.tr("Lithium-Ion") }
                    TechInfo { label: I18nService.tr("Serial Number"); value: Battery.serial || I18nService.tr("Not Available") }
                    TechInfo { label: I18nService.tr("Design Capacity"); value: (Battery.energyFullDesign || 0).toFixed(2) + " Wh" }
                    TechInfo { label: I18nService.tr("Full Capacity"); value: (Battery.energyFull || 0).toFixed(2) + " Wh" }
                }
            }
        }
    }

    // ── Internal Components ──

    // StatCard with customizable MaterialShape for unique shapes per card
    component StatCard: Rectangle {
        id: cardRoot
        property string label
        property string value
        property string icon
        property var materialShape: MaterialShape.Shape.Cookie12Sided
        
        implicitHeight: Math.round(width * 0.8)
        radius: 16 * Appearance.effectiveScale
        color: Appearance.colors.colLayer2
        border.width: Math.max(1, 1 * Appearance.effectiveScale)
        border.color: Functions.ColorUtils.applyAlpha(Appearance.colors.colOutlineVariant, 0.15)
        
        Item {
            anchors.fill: parent
            anchors.margins: 16 * Appearance.effectiveScale

            // Top-Right Scalloped MaterialShape Icon Badge
            MaterialShapeWrappedMaterialSymbol {
                anchors.right: parent.right
                anchors.top: parent.top
                text: cardRoot.icon
                iconSize: 18 * Appearance.effectiveScale
                padding: 8 * Appearance.effectiveScale
                shape: cardRoot.materialShape
                color: Appearance.colors.colSecondaryContainer
                colSymbol: Appearance.colors.colOnSecondaryContainer
            }

            // Bottom-Left Value & Label Stack
            ColumnLayout {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.rightMargin: 48 * Appearance.effectiveScale
                spacing: 2 * Appearance.effectiveScale

                StyledText {
                    text: cardRoot.value
                    font.pixelSize: Appearance.font.pixelSize.huge
                    font.family: Appearance.font.family.numbers
                    font.weight: Font.DemiBold
                    color: Appearance.m3colors.m3onSurface
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                StyledText {
                    text: cardRoot.label
                    font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.Medium
                    color: Appearance.colors.colSubtext
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }

    component TechInfo: ColumnLayout {
        id: infoRoot
        property string label
        property string value
        spacing: 2 * Appearance.effectiveScale
        Layout.fillWidth: true

        StyledText {
            text: infoRoot.label
            font.pixelSize: Appearance.font.pixelSize.smallest
            font.weight: Font.Medium
            color: Appearance.colors.colSubtext
        }
        StyledText {
            text: infoRoot.value
            font.pixelSize: Appearance.font.pixelSize.small
            font.weight: Font.Medium
            color: Appearance.m3colors.m3onSurface
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
