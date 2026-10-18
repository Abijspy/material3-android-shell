import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell

/**
 * Navigation sidebar for the Settings panel.
 * Uses a NavigationRail style common in modern Android apps.
 */
StyledNavigationRail {
    id: root
    
    // Fixed width rail
    expandable: false
    expanded: false
    showMenuButton: false
    
    signal pageSelected(int index)
    onItemClicked: (index) => {
        root.pageSelected(index);
    }
    
    model: [
        { name: I18nService.tr("Network"), icon: "wifi" },
        { name: I18nService.tr("Bluetooth"), icon: "bluetooth" },
        { name: I18nService.tr("Audio"), icon: "volume_up" },
        { name: I18nService.tr("Display"), icon: "monitor" },
        { name: I18nService.tr("Customize"), icon: "palette" },
        { name: I18nService.tr("Widgets"), icon: "widgets" },
        { name: I18nService.tr("System"), icon: "settings_applications" },
        { name: I18nService.tr("Services"), icon: "cloud" },
        { name: I18nService.tr("About"), icon: "info" }
    ]
    
    topComponent: Component {
        Item {
            // Give extra height below the FAB to increase the gap to the rail items.
            // FAB is 56px. Adding 24px extra space + 16px from layout spacing = 40px total gap.
            implicitHeight: (56 + 24) * Appearance.effectiveScale
            implicitWidth: 56 * Appearance.effectiveScale
            Layout.alignment: Qt.AlignHCenter

            RippleButton {
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                implicitWidth: 56 * Appearance.effectiveScale
                implicitHeight: 56 * Appearance.effectiveScale
                buttonRadius: 16 * Appearance.effectiveScale // Squircle / FAB shape
                colBackground: Appearance.m3colors.m3primaryContainer
                colBackgroundHover: Functions.ColorUtils.mix(Appearance.m3colors.m3primaryContainer, Appearance.m3colors.m3onPrimaryContainer, 0.9)
                colRipple: Functions.ColorUtils.applyAlpha(Appearance.m3colors.m3onPrimaryContainer, 0.15)
                
                onClicked: {
                    let path = Directories.shellConfigPath;
                    if (!Qt.openUrlExternally("file://" + path)) {
                        Quickshell.execDetached(["xdg-open", path]);
                    }
                }

                MaterialSymbol {
                    anchors.centerIn: parent
                    text: "edit" // Config icon
                    iconSize: 24 * Appearance.effectiveScale
                    color: Appearance.m3colors.m3onPrimaryContainer
                }

                StyledToolTip { text: I18nService.tr("Open config file"); extraVisibleCondition: parent.hovered || parent.realHovered }
            }
        }
    }
}
