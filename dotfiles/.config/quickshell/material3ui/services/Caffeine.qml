pragma Singleton
import "../core"
import Quickshell
import Quickshell.Wayland
import QtQuick

/**
 * Caffeine service to prevent the system from idling.
 * Syncs directly with Config.options.quickSettings.caffeineActive.
 */
Item {
    id: root

    // One-way binding from Config to this service
    readonly property bool active: Config.ready ? Config.options.quickSettings.caffeineActive : false

    IdleInhibitor {
        id: inhibitor
        enabled: root.active
        
        // IdleInhibitor requires a surface to be active on some compositors.
        window: PanelWindow {
            implicitWidth: 0
            implicitHeight: 0
            color: "transparent"
            exclusionMode: ExclusionMode.Ignore
            
            // Just in case...
            anchors {
                right: true
                bottom: true
            }
            // Make it not interactable
            mask: Region {
                item: null
            }
        }
    }
}
