import "../../core"
import "../../services"
import QtQuick
import Quickshell
import Quickshell.Wayland
import Quickshell.Hyprland

/**
 * Android-style alarm card — top-center floating panel.
 * Hosts AlarmCard while ringing on an unlocked desktop.
 * When the session is locked this window hides itself: a session-lock surface
 * sits above every layer-shell surface, so this overlay would be invisible
 * AND unclickable — LockSurface embeds the same card instead.
 */
Scope {
    id: root

    property var focusedScreen: Quickshell.screens.find(s => s.name === Hyprland.focusedMonitor?.name) ?? Quickshell.screens[0]

    PanelWindow {
        id: window
        visible: AlarmService.ringing && !GlobalStates.screenLocked

        color: "transparent"
        exclusionMode: ExclusionMode.Ignore
        WlrLayershell.namespace: "material3ui:alarm"
        WlrLayershell.layer: WlrLayer.Overlay
        // Modal while ringing: Exclusive guarantees Esc/S reach the card
        // without any click first (Hyprland only hands out OnDemand keyboard
        // on click); None otherwise so it never competes for keyboard input.
        WlrLayershell.keyboardFocus: visible ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None

        anchors { top: true; bottom: true; left: true; right: true }

        screen: root.focusedScreen
        Connections {
            target: root
            function onFocusedScreenChanged() { window.screen = root.focusedScreen; }
        }

        // Only the card is interactive
        mask: Region { item: alarmLoader.item }

        // While ringing, clicks on the alarm card must NOT count as
        // "clicking outside" for HyprlandFocusGrab (which would close panels)
        Component.onDestruction: GlobalFocusGrab.removePersistent(window)

        implicitWidth: (alarmLoader.item?.implicitWidth ?? 0) + (40 * Appearance.effectiveScale)
        implicitHeight: (alarmLoader.item?.implicitHeight ?? 0) + (40 * Appearance.effectiveScale)

        onVisibleChanged: {
            if (visible) {
                GlobalFocusGrab.addPersistent(window);
            } else {
                GlobalFocusGrab.removePersistent(window);
            }
        }

        Loader {
            id: alarmLoader
            anchors {
                horizontalCenter: parent.horizontalCenter
                top: parent.top
                topMargin: ((Config.options?.statusBar?.height ?? 40) * Appearance.effectiveScale) + (16 * Appearance.effectiveScale)
            }
            active: window.visible
            sourceComponent: AlarmCard {}
            onLoaded: item.forceActiveFocus()
        }
    }
}
