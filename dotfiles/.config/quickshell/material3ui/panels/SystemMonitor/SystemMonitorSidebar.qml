import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"

StyledNavigationRail {
    id: root
    
    // Navigation Rail properties
    expandable: true
    showMenuButton: false // The menu button is in the global header
    
    currentIndex: GlobalStates.systemMonitorIndex
    
    model: [
        { name: I18nService.tr("Performance"), icon: "monitoring", stackIndex: 0 },
        { name: I18nService.tr("Battery"), icon: "battery_charging_full", stackIndex: 1, visible: Battery.available },
        { name: I18nService.tr("Processes"), icon: "list", stackIndex: 2 }
    ]
    
    onItemClicked: (index) => {
        GlobalStates.systemMonitorIndex = index
    }
    
    bottomComponent: Component {
        UserProfile {
            compact: !root.expanded
            onClicked: {
                GlobalStates.systemMonitorOpen = false
                GlobalStates.settingsPageIndex = 9
                GlobalStates.activateSettings()
            }
        }
    }
}
