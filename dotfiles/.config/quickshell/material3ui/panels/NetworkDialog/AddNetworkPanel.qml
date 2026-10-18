pragma ComponentBehavior: Bound

import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland

/**
 * Add Network Panel.
 * Acts as a modal dialog across the entire screen.
 */
Scope {
    id: root

    Loader {
        active: GlobalStates.addNetworkDialogOpen
        sourceComponent: Variants {
            model: Quickshell.screens
            delegate: PanelWindow {
                id: panelWindow
                required property var modelData
                screen: modelData

                readonly property bool isActive: GlobalStates.activeScreen === modelData
                visible: GlobalStates.addNetworkDialogOpen && isActive

                anchors {
                    top: true
                    left: true
                    right: true
                    bottom: true
                }

                color: "transparent"
                WlrLayershell.namespace: "material3ui:networkadd"
                WlrLayershell.keyboardFocus: (GlobalStates.addNetworkDialogOpen && isActive) ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None
                WlrLayershell.layer: (GlobalStates.addNetworkDialogOpen && isActive) ? WlrLayer.Overlay : WlrLayer.Background
                exclusionMode: ExclusionMode.Ignore

                // State
                property bool isHidden: false
                property bool showPassword: false

                function closeDialog() {
                    ssidInput.text = "";
                    hiddenPassInput.text = "";
                    isHidden = false;
                    showPassword = false;
                    GlobalStates.addNetworkDialogOpen = false;
                }

                // Close when clicking outside
                MouseArea {
                    anchors.fill: parent
                    onClicked: panelWindow.closeDialog()
                }

                // Esc to close
                Shortcut {
                    sequence: "Escape"
                    onActivated: panelWindow.closeDialog()
                    enabled: panelWindow.visible
                }

                // Scrim
                Rectangle {
                    anchors.fill: parent
                    color: Functions.ColorUtils.applyAlpha(Appearance.colors.colLayer0, 0.6)
                    opacity: (GlobalStates.addNetworkDialogOpen && isActive) ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }

                // Dialog Content
                Rectangle {
                    id: dialog
                    anchors.centerIn: parent
                    width: Math.max(280 * Appearance.effectiveScale, Math.min(parent.width - 48 * Appearance.effectiveScale, 560 * Appearance.effectiveScale))
                    implicitHeight: contentCol.implicitHeight + (48 * Appearance.effectiveScale)
                    radius: 28 * Appearance.effectiveScale
                    color: Appearance.m3colors.m3surfaceContainerHigh

                    StyledRectangularShadow {
                        target: dialog
                        z: -1
                    }

                    MouseArea {
                        anchors.fill: parent
                        // Prevent click-through to the background scrim
                    }

                    // No shadow required for fullscreen modals

                    ColumnLayout {
                        id: contentCol
                        anchors.fill: parent
                        anchors.margins: 24 * Appearance.effectiveScale
                        spacing: 0

                        // Icon
                        MaterialSymbol {
                            Layout.alignment: Qt.AlignHCenter
                            text: "network_wifi"
                            iconSize: 24 * Appearance.effectiveScale
                            color: Appearance.m3colors.m3secondary
                        }
                        
                        // Title
                        StyledText {
                            Layout.fillWidth: true
                            Layout.topMargin: 16 * Appearance.effectiveScale
                            horizontalAlignment: Text.AlignHCenter
                            text: I18nService.tr("Add Network")
                            font.pixelSize: Appearance.font.pixelSize.huge
                            font.family: Appearance.font.family.title
                            font.weight: Font.Normal
                            color: Appearance.colors.colOnLayer1
                        }
                        
                        // Message
                        StyledText {
                            Layout.fillWidth: true
                            Layout.topMargin: 16 * Appearance.effectiveScale
                            horizontalAlignment: Text.AlignHCenter
                            text: I18nService.tr("Enter the details of the network you want to join.")
                            font.pixelSize: Appearance.font.pixelSize.small
                            color: Appearance.m3colors.m3onSurfaceVariant
                            wrapMode: Text.Wrap
                        }

                        // Inputs
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 24 * Appearance.effectiveScale
                            spacing: 20 * Appearance.effectiveScale

                            // SSID Input
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52 * Appearance.effectiveScale
                                radius: 8 * Appearance.effectiveScale
                                color: "transparent"
                                border.width: ssidInput.input.activeFocus ? Math.max(1, 2 * Appearance.effectiveScale) : Math.max(1, 1 * Appearance.effectiveScale)
                                border.color: ssidInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline

                                // Floating Label
                                Rectangle {
                                    x: 12 * Appearance.effectiveScale
                                    y: -8 * Appearance.effectiveScale
                                    width: ssidLabel.width + 8 * Appearance.effectiveScale
                                    height: 16 * Appearance.effectiveScale
                                    color: Appearance.m3colors.m3surfaceContainerHigh
                                    
                                    StyledText {
                                        id: ssidLabel
                                        anchors.centerIn: parent
                                        text: I18nService.tr("Network Name")
                                        font.pixelSize: Appearance.font.pixelSize.smaller
                                        font.weight: Font.Medium
                                        color: ssidInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline
                                    }
                                }

                                StyledTextInput {
                                    id: ssidInput
                                    anchors.fill: parent
                                    anchors.leftMargin: 16 * Appearance.effectiveScale
                                    anchors.rightMargin: 16 * Appearance.effectiveScale
                                    placeholder: I18nService.tr("SSID")
                                    backgroundColor: "transparent"
                                    inputRadius: 0
                                    borderInactiveWidth: 0
                                    showActiveBorder: false
                                    leftMargin: 0
                                    rightMargin: 0
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                }
                            }

                            // Password Input
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52 * Appearance.effectiveScale
                                radius: 8 * Appearance.effectiveScale
                                color: "transparent"
                                border.width: hiddenPassInput.input.activeFocus ? Math.max(1, 2 * Appearance.effectiveScale) : Math.max(1, 1 * Appearance.effectiveScale)
                                border.color: hiddenPassInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline

                                // Floating Label
                                Rectangle {
                                    x: 12 * Appearance.effectiveScale
                                    y: -8 * Appearance.effectiveScale
                                    width: passLabel.width + 8 * Appearance.effectiveScale
                                    height: 16 * Appearance.effectiveScale
                                    color: Appearance.m3colors.m3surfaceContainerHigh
                                    
                                    StyledText {
                                        id: passLabel
                                        anchors.centerIn: parent
                                        text: I18nService.tr("Password")
                                        font.pixelSize: Appearance.font.pixelSize.smaller
                                        font.weight: Font.Medium
                                        color: hiddenPassInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 16 * Appearance.effectiveScale
                                    anchors.rightMargin: 8 * Appearance.effectiveScale
                                    
                                    StyledTextInput {
                                        id: hiddenPassInput
                                        Layout.fillWidth: true
                                        echoMode: panelWindow.showPassword ? TextInput.Normal : TextInput.Password
                                        placeholder: I18nService.tr("Optional")
                                        backgroundColor: "transparent"
                                        inputRadius: 0
                                        borderInactiveWidth: 0
                                        showActiveBorder: false
                                        leftMargin: 0
                                        rightMargin: 0
                                        font.pixelSize: Appearance.font.pixelSize.normal
                                    }

                                    RippleButton {
                                        implicitWidth: 32 * Appearance.effectiveScale
                                        implicitHeight: 32 * Appearance.effectiveScale
                                        buttonRadius: 16 * Appearance.effectiveScale
                                        colBackground: "transparent"
                                        onClicked: panelWindow.showPassword = !panelWindow.showPassword
                                        contentItem: MaterialSymbol {
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            text: panelWindow.showPassword ? "visibility_off" : "visibility"
                                            iconSize: 20 * Appearance.effectiveScale
                                            color: Appearance.colors.colSubtext
                                        }
                                    }
                                }
                            }
                        }

                        // Options (Interactive Hidden Toggle)
                        StyledCheckbox {
                            Layout.fillWidth: true
                            Layout.topMargin: 16 * Appearance.effectiveScale
                            text: I18nService.tr("Hidden network")
                            checked: panelWindow.isHidden
                            onToggled: panelWindow.isHidden = checked
                            textColor: Appearance.colors.colOnLayer1
                            font.pixelSize: Appearance.font.pixelSize.normal
                        }

                        // Actions
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 24 * Appearance.effectiveScale
                            spacing: 8 * Appearance.effectiveScale
                            
                            Item { Layout.fillWidth: true }
                            
                            RippleButton {
                                implicitHeight: 40 * Appearance.effectiveScale
                                buttonRadius: 20 * Appearance.effectiveScale
                                buttonText: I18nService.tr("Cancel")
                                colBackground: "transparent"
                                colBackgroundHover: Appearance.colors.colLayer2Hover
                                colText: Appearance.m3colors.m3primary
                                onClicked: panelWindow.closeDialog()
                            }
                            
                            RippleButton {
                                implicitHeight: 40 * Appearance.effectiveScale
                                buttonRadius: 20 * Appearance.effectiveScale
                                buttonText: I18nService.tr("Connect")
                                colBackground: "transparent"
                                colBackgroundHover: Appearance.colors.colLayer2Hover
                                colText: Appearance.m3colors.m3primary
                                enabled: ssidInput.text.length > 0
                                onClicked: {
                                    Network.connectWithPassword(ssidInput.text, hiddenPassInput.text, panelWindow.isHidden);
                                    panelWindow.closeDialog();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
