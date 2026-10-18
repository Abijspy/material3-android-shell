pragma ComponentBehavior: Bound

import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland

Scope {
    id: root

    Loader {
        active: DialogService.active
        sourceComponent: Variants {
            model: Quickshell.screens
            delegate: PanelWindow {
                id: panelWindow
                required property var modelData
                screen: modelData
                
                readonly property bool isActive: GlobalStates.activeScreen === modelData
                visible: DialogService.active && isActive

                anchors {
                    top: true
                    left: true
                    right: true
                    bottom: true
                }

                color: "transparent"
                WlrLayershell.namespace: "material3ui:dialog"
                // Exclusive: guarantees the dialog owns keyboard focus so ESC works
                WlrLayershell.keyboardFocus: (DialogService.active && isActive) ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
                WlrLayershell.layer: (DialogService.active && isActive) ? WlrLayer.Overlay : WlrLayer.Background
                exclusionMode: ExclusionMode.Ignore

                Component.onCompleted: GlobalFocusGrab.addDismissable(panelWindow)
                Component.onDestruction: GlobalFocusGrab.removeDismissable(panelWindow)

                Connections {
                    target: GlobalFocusGrab
                    function onDismissed() {
                        DialogService.cancel()
                    }
                }

                // ── Scrim ──
                Rectangle {
                    anchors.fill: parent
                    color: Functions.ColorUtils.applyAlpha(Appearance.colors.colLayer0, 0.6)
                    opacity: (DialogService.active && isActive) ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 200 } }

                    // Click backdrop to dismiss (nested overlays like the alarm
                    // time picker reparent above this, so they intercept first)
                    MouseArea {
                        anchors.fill: parent
                        onClicked: DialogService.cancel()
                    }
                }

                // ── Auth Dialog ──
                Rectangle {
                    id: dialog
                    anchors.centerIn: parent
                    width: {
                        const maxW = Math.min(parent.width - 48 * Appearance.effectiveScale, 560 * Appearance.effectiveScale);
                        if (DialogService.contentComponent !== null && DialogService.contentWidth > 0)
                            return Math.max(280 * Appearance.effectiveScale, Math.min(maxW, DialogService.contentWidth * Appearance.effectiveScale));
                        return Math.max(280 * Appearance.effectiveScale, maxW);
                    }
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

                        // ── Custom content mode ──
                        Loader {
                            Layout.fillWidth: true
                            // An unloaded Loader keeps the last item's implicit
                            // size, which would stretch a follow-up standard
                            // dialog (e.g. confirm opened from custom content)
                            // with a big empty gap — pin the height to 0 while
                            // inactive
                            Layout.preferredHeight: active ? implicitHeight : 0
                            active: DialogService.contentComponent !== null
                            sourceComponent: DialogService.contentComponent
                        }

                        // Icon
                        MaterialSymbol {
                            Layout.alignment: Qt.AlignHCenter
                            text: DialogService.iconText
                            iconSize: 24 * Appearance.effectiveScale
                            color: Appearance.m3colors.m3secondary
                            visible: DialogService.contentComponent === null && text !== ""
                        }

                        // Title
                        StyledText {
                            Layout.fillWidth: true
                            Layout.topMargin: DialogService.iconText !== "" ? 16 * Appearance.effectiveScale : 0
                            horizontalAlignment: DialogService.iconText !== "" ? Text.AlignHCenter : Text.AlignLeft
                            text: DialogService.titleText
                            font.pixelSize: Appearance.font.pixelSize.huge || 24 * Appearance.effectiveScale
                            font.family: Appearance.font.family.title
                            font.weight: Font.Normal
                            color: Appearance.colors.colOnLayer1
                            wrapMode: Text.Wrap
                            visible: DialogService.contentComponent === null
                        }

                        // Message
                        StyledText {
                            Layout.fillWidth: true
                            Layout.topMargin: 16 * Appearance.effectiveScale
                            horizontalAlignment: DialogService.iconText !== "" ? Text.AlignHCenter : Text.AlignLeft
                            text: DialogService.messageText
                            font.pixelSize: Appearance.font.pixelSize.small || 14 * Appearance.effectiveScale
                            color: Appearance.m3colors.m3onSurfaceVariant
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                            visible: DialogService.contentComponent === null
                        }
                        
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 4 * Appearance.effectiveScale
                            visible: DialogService.contentComponent === null
                        }

                        // Buttons
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 24 * Appearance.effectiveScale
                            spacing: 8 * Appearance.effectiveScale
                            visible: DialogService.contentComponent === null

                            Item { Layout.fillWidth: true } // spacer

                            RippleButton {
                                implicitHeight: 40 * Appearance.effectiveScale
                                buttonRadius: 20 * Appearance.effectiveScale
                                buttonText: DialogService.cancelText
                                colBackground: "transparent"
                                colBackgroundHover: Appearance.colors.colLayer2Hover
                                colText: Appearance.m3colors.m3primary
                                onClicked: DialogService.cancel()
                            }

                            RippleButton {
                                implicitHeight: 40 * Appearance.effectiveScale
                                buttonRadius: 20 * Appearance.effectiveScale
                                buttonText: DialogService.confirmText
                                colBackground: "transparent"
                                colBackgroundHover: Appearance.colors.colLayer2Hover
                                colText: Appearance.m3colors.m3primary
                                onClicked: DialogService.submit()
                            }
                        }
                    }

                    // Key Handling
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Escape) {
                            if (DialogService.escapeHandler !== null) DialogService.escapeHandler();
                            else DialogService.cancel();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                            // Custom content handles its own submission
                            if (DialogService.contentComponent === null)
                                DialogService.submit();
                            event.accepted = true;
                        }
                    }

                    // Window-scoped fallback: works even if the item never got focus
                    Shortcut {
                        sequence: "Escape"
                        onActivated: {
                            if (DialogService.escapeHandler !== null) DialogService.escapeHandler();
                            else DialogService.cancel();
                        }
                    }

                    // Focus handling
                    onVisibleChanged: {
                        if (visible) {
                            forceActiveFocus()
                        }
                    }
                    Component.onCompleted: if (visible) forceActiveFocus()
                }
            }
        }
    }
}
