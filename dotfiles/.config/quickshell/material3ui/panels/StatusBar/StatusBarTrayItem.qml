import QtQuick
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Services.SystemTray
import Quickshell.Widgets
import "../../core"
import "../../core/functions" as Functions
import "../../widgets"

/**
 * System Tray Item with a solid halo (stroke-like) for maximum visibility.
 * Optimized with caching and fixed icon sizing to prevent Steam icon distortion.
 */
MouseArea {
    id: root
    required property SystemTrayItem item
    
    signal menuOpened(var menu)
    signal menuClosed()

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    cursorShape: Qt.PointingHandCursor
    
    // STRICT 16x16 layout to keep Steam icon original
    implicitWidth: 16 * Appearance.effectiveScale
    implicitHeight: 16 * Appearance.effectiveScale

    onPressed: (event) => {
        if (event.button === Qt.LeftButton) {
            item.activate();
        } else if (event.button === Qt.RightButton) {
            if (item.hasMenu) {
                if (GlobalStates.activeTrayItem === root.item) {
                    GlobalStates.activeTrayItem = null;
                } else {
                    GlobalStates.activeTrayItem = root.item;
                }
            }
        }
        event.accepted = true;
    }

    Item {
        anchors.fill: parent
        
        IconImage {
            id: trayIcon
            source: (root.item && root.item.icon) ? root.item.icon : ""
            visible: source !== ""
            anchors.centerIn: parent
            // Keep original 16x16 size
            width: 16 * Appearance.effectiveScale
            height: 16 * Appearance.effectiveScale
            asynchronous: true
            
            // Apply a solid dark outline (stroke) only when status bar text is dark (light wallpaper)
            // This prevents "ugly" white glows on dark wallpapers
            layer.enabled: Appearance.colors.resolvedStatusBarDarkText
            layer.effect: DropShadow {
                transparentBorder: true
                horizontalOffset: 0
                verticalOffset: 0
                radius: 2 * Appearance.effectiveScale 
                samples: 12
                spread: 0.6
                color: Functions.ColorUtils.applyAlpha("#000000", 0.7)
                cached: true
            }
        }
    }

    Loader {
        id: menuLoader
        active: GlobalStates.activeTrayItem === root.item
        onLoaded: {
            root.menuOpened(item);
        }
        sourceComponent: StatusBarTrayMenu {
            trayItemMenuHandle: root.item.menu
            
            anchor {
                window: root.QsWindow.window
                rect: {
                    var pos = root.mapToItem(null, 0, 0); 
                    return Qt.rect(pos.x, pos.y + root.height + (4 * Appearance.effectiveScale), root.width, root.height);
                }
                edges: Edges.Top | Edges.Center
                gravity: Edges.Bottom
            }

            onMenuClosed: {
                if (GlobalStates.activeTrayItem === root.item) {
                    GlobalStates.activeTrayItem = null;
                }
                root.menuClosed();
            }
        }
    }
}
