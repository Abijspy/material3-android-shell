import "../../../core"
import "../../../widgets"
import "../../../services"
import QtQuick
import QtQuick.Layouts
import Quickshell

Item {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 20 * Appearance.effectiveScale

        Image {
            Layout.alignment: Qt.AlignHCenter
            source: "../../../assets/icons/Material3UI.svg"
            sourceSize.width: 80 * Appearance.effectiveScale
            sourceSize.height: 80 * Appearance.effectiveScale
            fillMode: Image.PreserveAspectFit
            smooth: true
            antialiasing: true
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10 * Appearance.effectiveScale

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: I18nService.tr("Welcome to Material3UI Shell")
                font.pixelSize: Appearance.font.pixelSize.larger
                font.family: Appearance.font.family.title
                font.weight: Font.Bold
                color: Appearance.colors.colOnLayer1
            }

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 540 * Appearance.effectiveScale
                text: I18nService.tr("A modern, Quickshell-based desktop environment tailored specifically for Hyprland. Adopting elegant Android 16 design elements, Material3UI brings robust widgets, deep personalization, and a fluid, highly-customizable workflow directly to your Wayland workspace.")
                font.pixelSize: Appearance.font.pixelSize.normal
                color: Appearance.colors.colSubtext
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.4
            }
            
            Item {
                Layout.preferredHeight: 8 * Appearance.effectiveScale
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12 * Appearance.effectiveScale

                RippleButton {
                    implicitWidth: 100 * Appearance.effectiveScale
                    implicitHeight: 36 * Appearance.effectiveScale
                    buttonRadius: 18 * Appearance.effectiveScale
                    colBackground: Appearance.colors.colLayer0
                    onClicked: {
                        GlobalStates.onboardingStep = 6;
                    }
                    
                    StyledText {
                        anchors.centerIn: parent
                        text: I18nService.tr("Skip")
                        font.pixelSize: Appearance.font.pixelSize.small
                        font.weight: Font.Medium
                        color: Appearance.colors.colOnLayer1
                    }
                }

                RippleButton {
                    implicitWidth: 140 * Appearance.effectiveScale
                    implicitHeight: 36 * Appearance.effectiveScale
                    buttonRadius: 18 * Appearance.effectiveScale
                    colBackground: Appearance.colors.colPrimary
                    onClicked: {
                        GlobalStates.onboardingStep++;
                    }
                    
                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6 * Appearance.effectiveScale
                        
                        StyledText {
                            text: I18nService.tr("Start Tour")
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.DemiBold
                            color: Appearance.colors.colOnPrimary
                        }
                        MaterialSymbol {
                            text: "arrow_forward"
                            iconSize: 18 * Appearance.effectiveScale
                            color: Appearance.colors.colOnPrimary
                        }
                    }
                }
            }
            
            Item {
                Layout.preferredHeight: 6 * Appearance.effectiveScale
            }

            // Keyboard Hint for Intro
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6 * Appearance.effectiveScale
                opacity: 0.6
                
                MaterialSymbol {
                    text: "keyboard"
                    iconSize: 14 * Appearance.effectiveScale
                    color: Appearance.colors.colSubtext
                }
                StyledText {
                    text: "Use ← / → to navigate, Enter to continue, Esc to close"
                    font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                    color: Appearance.colors.colSubtext
                }
            }
        }
    }
}
