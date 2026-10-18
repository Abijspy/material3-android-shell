import "../../core"
import "../../services"
import "../../widgets"
import "../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland

/**
 * Material 3 Snackbar panel — bottom-center, above the dock.
 * Driven entirely by SnackbarService. DND-proof transient feedback.
 */
Scope {
    id: root

    property var focusedScreen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name) ?? Quickshell.screens[0]

    PanelWindow {
        id: window
        visible: SnackbarService.visible

        color: "transparent"
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.namespace: "material3ui:snackbar"
        WlrLayershell.layer: WlrLayer.Overlay
        WlrLayershell.keyboardFocus: WlrKeyboardFocus.None

        anchors { top: true; bottom: true; left: true; right: true }

        // Sync screen with Hyprland focus
        screen: root.focusedScreen
        Connections {
            target: root
            function onFocusedScreenChanged() { window.screen = root.focusedScreen; }
        }

        // Interactive snackbars only accept clicks on the card itself;
        // inert snackbars are fully click-through (game-safe)
        mask: SnackbarService.interactive ? cardRegion : emptyRegion

        Region { id: cardRegion; item: snackbarCard }
        Region { id: emptyRegion }

        // While visible, clicks on the snackbar must NOT count as
        // "clicking outside" for HyprlandFocusGrab (which would close panels)
        Component.onDestruction: GlobalFocusGrab.removePersistent(window)

        implicitWidth: snackbarCard.implicitWidth + (40 * Appearance.effectiveScale)
        implicitHeight: snackbarCard.implicitHeight + (24 * Appearance.effectiveScale)

        // Entry animation
        onVisibleChanged: {
            if (visible) {
                if (SnackbarService.interactive) GlobalFocusGrab.addPersistent(window);
                snackbarCard.opacity = 0;
                snackbarSlide.y = 16 * Appearance.effectiveScale;
                entryAnim.restart();
            } else {
                GlobalFocusGrab.removePersistent(window);
            }
        }

        ParallelAnimation {
            id: entryAnim
            NumberAnimation { target: snackbarCard; property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutQuint }
            NumberAnimation { target: snackbarSlide; property: "y"; from: 16 * Appearance.effectiveScale; to: 0; duration: 300; easing.type: Easing.OutQuint }
        }

        // --- M3 Snackbar card ---
        Rectangle {
            id: snackbarCard
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: 12 * Appearance.effectiveScale
            }
            transform: Translate { id: snackbarSlide }

            implicitHeight: 48 * Appearance.effectiveScale
            implicitWidth: contentRow.implicitWidth + (32 * Appearance.effectiveScale)
            radius: 8 * Appearance.effectiveScale
            color: Appearance.m3colors.m3inverseSurface

            StyledRectangularShadow { target: snackbarCard; z: -1 }

            RowLayout {
                id: contentRow
                anchors.fill: parent
                anchors {
                    leftMargin: 16 * Appearance.effectiveScale
                    rightMargin: 8 * Appearance.effectiveScale
                }
                spacing: 8 * Appearance.effectiveScale

                StyledText {
                    text: SnackbarService.text
                    font.pixelSize: Math.round(13 * Appearance.effectiveScale)
                    color: Appearance.m3colors.m3inverseOnSurface
                    elide: Text.ElideRight
                    Layout.maximumWidth: 420 * Appearance.effectiveScale
                    Layout.fillWidth: true
                }

                RippleButton {
                    visible: SnackbarService.interactive && SnackbarService.actionLabel !== ""
                    implicitHeight: 36 * Appearance.effectiveScale
                    implicitWidth: actionText.implicitWidth + (16 * Appearance.effectiveScale)
                    buttonRadius: 18 * Appearance.effectiveScale
                    colBackground: "transparent"
                    colRipple: Functions.ColorUtils.applyAlpha(Appearance.m3colors.m3inversePrimary, 0.15)
                    onClicked: SnackbarService.triggerAction()

                    StyledText {
                        id: actionText
                        anchors.centerIn: parent
                        text: SnackbarService.actionLabel
                        font.pixelSize: Math.round(13 * Appearance.effectiveScale)
                        font.weight: Font.Medium
                        color: Appearance.m3colors.m3inversePrimary
                    }
                }

                RippleButton {
                    visible: SnackbarService.interactive
                    implicitWidth: 32 * Appearance.effectiveScale
                    implicitHeight: 32 * Appearance.effectiveScale
                    buttonRadius: 16 * Appearance.effectiveScale
                    colBackground: "transparent"
                    colRipple: Functions.ColorUtils.applyAlpha(Appearance.m3colors.m3inverseOnSurface, 0.12)
                    onClicked: SnackbarService.dismiss()

                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "close"
                        iconSize: 16 * Appearance.effectiveScale
                        color: Appearance.m3colors.m3inverseOnSurface
                    }
                }
            }
        }
    }
}
