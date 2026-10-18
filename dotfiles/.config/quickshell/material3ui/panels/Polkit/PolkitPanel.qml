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
 * Polkit authentication panel.
 * Mirroring the 'ii' example's fullscreen overlay style.
 */
Scope {
    id: root

    Loader {
        active: PolkitService.active
        sourceComponent: Variants {
            model: Quickshell.screens
            delegate: PanelWindow {
                id: panelWindow
                required property var modelData
                screen: modelData
                
                readonly property bool isActive: GlobalStates.activeScreen === modelData
                visible: PolkitService.active && isActive

                anchors {
                    top: true
                    left: true
                    right: true
                    bottom: true
                }

                color: "transparent"
                WlrLayershell.namespace: "material3ui:polkit"
                WlrLayershell.keyboardFocus: (PolkitService.active && isActive) ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None
                WlrLayershell.layer: (PolkitService.active && isActive) ? WlrLayer.Overlay : WlrLayer.Background
                exclusionMode: ExclusionMode.Ignore

                // ── Scrim ──
                Rectangle {
                    anchors.fill: parent
                    color: Functions.ColorUtils.applyAlpha(Appearance.colors.colLayer0, 0.6)
                    opacity: (PolkitService.active && isActive) ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: PolkitService.cancel()
                    }
                }

                // ── Auth Dialog ──
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

                    // Block click-through: clicks on the card body must not
                    // reach the scrim's dismiss MouseArea
                    MouseArea { anchors.fill: parent }

                    ColumnLayout {
                        id: contentCol
                        anchors.fill: parent
                        anchors.margins: 24 * Appearance.effectiveScale
                        spacing: 0

                        // Icon
                        MaterialSymbol {
                            Layout.alignment: Qt.AlignHCenter
                            text: "security"
                            iconSize: 24 * Appearance.effectiveScale
                            color: Appearance.m3colors.m3secondary
                        }

                        // Title
                        StyledText {
                            Layout.fillWidth: true
                            Layout.topMargin: 16 * Appearance.effectiveScale
                            horizontalAlignment: Text.AlignHCenter
                            text: I18nService.tr("Authentication Required")
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
                            text: PolkitService.cleanMessage
                            font.pixelSize: Appearance.font.pixelSize.small
                            color: Appearance.m3colors.m3onSurfaceVariant
                            wrapMode: Text.Wrap
                        }

                        // Password Field Section
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 24 * Appearance.effectiveScale
                            spacing: 8 * Appearance.effectiveScale

                            Rectangle {
                                id: inputContainer
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52 * Appearance.effectiveScale
                                radius: 8 * Appearance.effectiveScale // Reduced rounding as requested
                                color: "transparent" // Same as background
                                border.width: passwordInput.activeFocus || PolkitService.failed ? Math.max(1, 2 * Appearance.effectiveScale) : Math.max(1, 1 * Appearance.effectiveScale)
                                border.color: PolkitService.failed ? Appearance.m3colors.m3error : (passwordInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline)

                                // Floating Label (Simulated)
                                Rectangle {
                                    x: 12 * Appearance.effectiveScale
                                    y: -8 * Appearance.effectiveScale
                                    width: labelText.width + (8 * Appearance.effectiveScale)
                                    height: 16 * Appearance.effectiveScale
                                    color: Appearance.m3colors.m3surfaceContainerHigh // Match dialog background
                                    
                                    StyledText {
                                        id: labelText
                                        anchors.centerIn: parent
                                        text: I18nService.tr("Password")
                                        font.pixelSize: Appearance.font.pixelSize.smaller
                                        font.weight: Font.Medium
                                        color: PolkitService.failed ? Appearance.m3colors.m3error : (passwordInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline)
                                    }
                                }

                                StyledTextInput {
                                    id: passwordInput
                                    anchors.fill: parent
                                    anchors.leftMargin: 16 * Appearance.effectiveScale
                                    anchors.rightMargin: 16 * Appearance.effectiveScale
                                    inputRadius: 0
                                    backgroundColor: "transparent"
                                    borderInactiveWidth: 0
                                    showActiveBorder: false
                                    leftMargin: 0
                                    rightMargin: 0
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    echoMode: PolkitService.flow?.responseVisible ? TextInput.Normal : TextInput.Password
                                    enabled: PolkitService.interactionAvailable
                                    
                                    focus: true
                                    placeholder: PolkitService.cleanPrompt
                                    onAccepted: PolkitService.submit(text)
                                    onTextChanged: if (PolkitService.failed) PolkitService.failed = false
                                }
                            }

                            // Error Message
                            StyledText {
                                Layout.fillWidth: true
                                visible: PolkitService.failed
                                text: I18nService.tr("Authentication failed, please try again")
                                color: Appearance.m3colors.m3error
                                font.pixelSize: Appearance.font.pixelSize.smaller
                                horizontalAlignment: Text.AlignLeft
                                leftPadding: 4 * Appearance.effectiveScale
                            }
                        }

                        // Buttons
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
                                onClicked: PolkitService.cancel()
                            }

                            RippleButton {
                                implicitHeight: 40 * Appearance.effectiveScale
                                buttonRadius: 20 * Appearance.effectiveScale
                                buttonText: I18nService.tr("OK")
                                colBackground: "transparent"
                                colBackgroundHover: Appearance.colors.colLayer2Hover
                                colText: Appearance.m3colors.m3primary
                                enabled: PolkitService.interactionAvailable
                                onClicked: PolkitService.submit(passwordInput.text)
                            }
                        }
                    }

                    // Key Handling
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Escape) {
                            PolkitService.cancel();
                            event.accepted = true;
                        }
                    }

                    Connections {
                        target: PolkitService
                        function onInteractionAvailableChanged() {
                            if (PolkitService.interactionAvailable) {
                                passwordInput.text = "";
                                passwordInput.forceActiveFocus();
                            }
                        }
                    }
                }
            }
        }
    }
}
