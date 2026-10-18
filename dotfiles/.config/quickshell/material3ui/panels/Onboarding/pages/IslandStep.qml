import "../../../core"
import "../../../widgets"
import "../../../services"
import "../../StatusBar"
import QtQuick
import QtQuick.Layouts
import Quickshell

ColumnLayout {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 14 * Appearance.effectiveScale
    
    property string simulatedState: "idle"

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Step 2: Status Bar & Dynamic Island")
            font.pixelSize: Appearance.font.pixelSize.larger
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
            color: Appearance.colors.colOnLayer1
        }
        
        StyledText {
            text: I18nService.tr("The Status Bar is your central hub for Notifications, Dashboard, and Quick Settings. The middle notch acts as an interactive Dynamic Island. Try simulating different states below!")
            font.pixelSize: Appearance.font.pixelSize.small
            color: Appearance.colors.colSubtext
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // Interactive Area
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: Appearance.colors.colLayer0
        radius: 12 * Appearance.effectiveScale
        border.width: Math.max(1, 1 * Appearance.effectiveScale)
        border.color: Appearance.colors.colOutlineVariant
        clip: true

        // ── The Notch ──
        Item {
            anchors.fill: parent
            
            Item {
                anchors.centerIn: parent
                width: parent.width
                height: 36 * Appearance.effectiveScale

                // Centered Dynamic Island
                DynamicIsland {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    islandStateOverride: root.simulatedState
                    indicatorWidth: 90 * Appearance.effectiveScale 
                }

                // Mock Workspace Indicator
                Row {
                    anchors.centerIn: parent
                    spacing: 6 * Appearance.effectiveScale
                    Repeater {
                        model: 5
                        Rectangle {
                            width: 8 * Appearance.effectiveScale
                            height: 8 * Appearance.effectiveScale
                            radius: 4 * Appearance.effectiveScale
                            color: index === 0 ? Appearance.colors.colPrimary : Appearance.colors.colLayer2
                        }
                    }
                }
            }
        }

        // ── Controls & Legend ──
        ColumnLayout {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 14 * Appearance.effectiveScale
            spacing: 12 * Appearance.effectiveScale
            
            // Gestures Hint
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 16 * Appearance.effectiveScale
                
                RowLayout {
                    spacing: 4 * Appearance.effectiveScale
                    MaterialSymbol { text: "swipe_up"; iconSize: 14 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                    StyledText { text: I18nService.tr("Scroll: Workspaces"); font.pixelSize: Math.round(11 * Appearance.effectiveScale); color: Appearance.colors.colSubtext }
                }
                RowLayout {
                    spacing: 4 * Appearance.effectiveScale
                    MaterialSymbol { text: "mouse"; iconSize: 14 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                    StyledText { text: I18nService.tr("Mid Click: Layout"); font.pixelSize: Math.round(11 * Appearance.effectiveScale); color: Appearance.colors.colSubtext }
                }
                RowLayout {
                    spacing: 4 * Appearance.effectiveScale
                    MaterialSymbol { text: "ads_click"; iconSize: 14 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                    StyledText { text: I18nService.tr("Right Click: Overview"); font.pixelSize: Math.round(11 * Appearance.effectiveScale); color: Appearance.colors.colSubtext }
                }
            }

            // State Simulators
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 2 * Appearance.effectiveScale
                
                SegmentedButton {
                    buttonText: I18nService.tr("Idle")
                    checked: root.simulatedState === "idle"
                    onClicked: root.simulatedState = "idle"
                }

                SegmentedButton {
                    buttonText: I18nService.tr("Media")
                    checked: root.simulatedState === "media"
                    onClicked: root.simulatedState = "media"
                }

                SegmentedButton {
                    buttonText: I18nService.tr("Notification")
                    checked: root.simulatedState === "notification"
                    onClicked: root.simulatedState = "notification"
                }

                SegmentedButton {
                    buttonText: I18nService.tr("Pomodoro")
                    checked: root.simulatedState === "pomodoro"
                    onClicked: root.simulatedState = "pomodoro"
                }
            }
        }
    }
}
