import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import "pages"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Wayland

/**
 * Main Onboarding Application Window.
 * Guides new users through Material3UI's features with a refined, compact modal design.
 */
Scope {
    id: root

    FloatingWindow {
        id: onboardingWindow
        visible: GlobalStates.onboardingOpen
        title: "Welcome to Material3UI"
        
        readonly property var screen: Quickshell.screens[0]

        color: "transparent"

        implicitWidth: Math.min(960 * Appearance.effectiveScale, screen.width * 0.85)
        implicitHeight: Math.min(660 * Appearance.effectiveScale, screen.height * 0.8)

        onVisibleChanged: {
            if (!visible) {
                GlobalStates.onboardingOpen = false;
            }
        }

        Component.onCompleted: {
            MaterialThemeLoader.reapplyTheme()
        }

        StyledRectangularShadow {
            target: contentContainer
            radius: contentContainer.radius
            color: Functions.ColorUtils.applyAlpha(Appearance.colors.colShadow, 0.2)
        }

        Rectangle {
            id: contentContainer
            anchors.fill: parent

            focus: visible
            Keys.onEscapePressed: GlobalStates.onboardingOpen = false
            Keys.onRightPressed: {
                if (GlobalStates.onboardingStep < 6) {
                    GlobalStates.onboardingStep++;
                }
            }
            Keys.onLeftPressed: {
                if (GlobalStates.onboardingStep > 0) {
                    GlobalStates.onboardingStep--;
                }
            }
            Keys.onReturnPressed: {
                if (GlobalStates.onboardingStep >= 6) {
                    Config.options.system.onboardingCompleted = true;
                    GlobalStates.onboardingOpen = false;
                } else {
                    GlobalStates.onboardingStep++;
                }
            }

            color: Appearance.colors.colLayer0
            radius: 20 * Appearance.effectiveScale

            TapHandler {}

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale

                // ── Refined Header with Stepper ──
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40 * Appearance.effectiveScale
                    spacing: 16 * Appearance.effectiveScale

                    RowLayout {
                        spacing: 10 * Appearance.effectiveScale
                        Layout.alignment: Qt.AlignVCenter

                        CustomIcon {
                            Layout.alignment: Qt.AlignVCenter
                            width: 22 * Appearance.effectiveScale
                            height: 22 * Appearance.effectiveScale
                            Layout.preferredWidth: 22 * Appearance.effectiveScale
                            Layout.preferredHeight: 22 * Appearance.effectiveScale
                            source: "material3ui-symbolic"
                            colorize: true
                            color: Appearance.colors.colPrimary
                            transform: Translate { y: -2 * Appearance.effectiveScale }
                        }

                        StyledText {
                            Layout.alignment: Qt.AlignVCenter
                            text: "Material3UI Setup"
                            font.pixelSize: Appearance.font.pixelSize.large
                            font.family: Appearance.font.family.title
                            font.weight: Font.DemiBold
                            color: Appearance.colors.colOnLayer0
                        }
                    }

                    // Stepper Dots Indicator
                    Row {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: 12 * Appearance.effectiveScale
                        spacing: 6 * Appearance.effectiveScale

                        Repeater {
                            model: 7
                            Rectangle {
                                required property int index
                                readonly property bool isActive: GlobalStates.onboardingStep === index
                                readonly property bool isPassed: GlobalStates.onboardingStep > index

                                implicitWidth: isActive ? (24 * Appearance.effectiveScale) : (8 * Appearance.effectiveScale)
                                implicitHeight: 8 * Appearance.effectiveScale
                                radius: 4 * Appearance.effectiveScale
                                color: isActive ? Appearance.colors.colPrimary : (isPassed ? Appearance.m3colors.m3primaryContainer : Appearance.colors.colLayer2)

                                Behavior on implicitWidth { NumberAnimation { duration: 250; easing.type: Easing.OutQuad } }
                                Behavior on color { ColorAnimation { duration: 200 } }
                            }
                        }
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    // Close Button
                    RippleButton {
                        implicitWidth: 32 * Appearance.effectiveScale
                        implicitHeight: 32 * Appearance.effectiveScale
                        buttonRadius: 16 * Appearance.effectiveScale
                        colBackground: "transparent"
                        onClicked: GlobalStates.onboardingOpen = false
                        
                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: "close"
                            iconSize: 18 * Appearance.effectiveScale
                            color: Appearance.colors.colSubtext
                        }
                    }
                }

                // Main Content Area
                Rectangle {
                    id: innerContentContainer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Appearance.colors.colLayer1
                    radius: 14 * Appearance.effectiveScale
                    clip: true

                    Loader {
                        id: stepLoader
                        anchors.fill: parent
                        anchors.margins: 16 * Appearance.effectiveScale

                        source: {
                            switch(GlobalStates.onboardingStep) {
                                case 0: return "pages/IntroStep.qml";
                                case 1: return "pages/DependencyStep.qml";
                                case 2: return "pages/WelcomeStep.qml";
                                case 3: return "pages/IslandStep.qml";
                                case 4: return "pages/GesturesStep.qml";
                                case 5: return "pages/IpcStep.qml";
                                case 6: return "pages/FinishStep.qml";
                                default: return "pages/FinishStep.qml";
                            }
                        }
                    }
                }

                // Footer Navigation
                RowLayout {
                    Layout.fillWidth: true
                    visible: GlobalStates.onboardingStep > 0
                    spacing: 12 * Appearance.effectiveScale
                    
                    RippleButton {
                        implicitWidth: 100 * Appearance.effectiveScale
                        implicitHeight: 36 * Appearance.effectiveScale
                        buttonRadius: 18 * Appearance.effectiveScale
                        colBackground: Appearance.colors.colLayer1
                        visible: GlobalStates.onboardingStep > 0
                        onClicked: GlobalStates.onboardingStep--
                        
                        StyledText {
                            anchors.centerIn: parent
                            text: I18nService.tr("Back")
                            font.pixelSize: Math.round(13 * Appearance.effectiveScale)
                            font.weight: Font.Medium
                            color: Appearance.colors.colOnLayer1
                        }
                    }
                    
                    Item {
                        Layout.fillWidth: true
                        
                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 6 * Appearance.effectiveScale
                            opacity: 0.6
                            
                            MaterialSymbol {
                                text: "keyboard"
                                iconSize: 14 * Appearance.effectiveScale
                                color: Appearance.colors.colSubtext
                            }
                            StyledText {
                                text: I18nService.tr("Use ← / → to navigate, Enter to continue, Esc to close")
                                font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                                color: Appearance.colors.colSubtext
                            }
                        }
                    }
                    
                    RippleButton {
                        implicitWidth: 100 * Appearance.effectiveScale
                        implicitHeight: 36 * Appearance.effectiveScale
                        buttonRadius: 18 * Appearance.effectiveScale
                        colBackground: Appearance.colors.colPrimary
                        onClicked: {
                            if (GlobalStates.onboardingStep >= 6) {
                                Config.options.system.onboardingCompleted = true;
                                GlobalStates.onboardingOpen = false;
                            } else {
                                GlobalStates.onboardingStep++;
                            }
                        }
                        
                        StyledText {
                            anchors.centerIn: parent
                            text: GlobalStates.onboardingStep === 0 ? I18nService.tr("Start") : (GlobalStates.onboardingStep >= 6 ? I18nService.tr("Finish") : I18nService.tr("Next"))
                            font.pixelSize: Math.round(13 * Appearance.effectiveScale)
                            font.weight: Font.DemiBold
                            color: Appearance.colors.colOnPrimary
                        }
                    }
                }
            }
        }
    }
}
