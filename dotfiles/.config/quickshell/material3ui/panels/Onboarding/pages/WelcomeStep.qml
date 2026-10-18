import "../../../core"
import "../../../widgets"
import "../../../services"
import "../../Settings/pages/WallpaperStyle"
import QtQuick
import QtQuick.Layouts
import Quickshell

ColumnLayout {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 14 * Appearance.effectiveScale

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Step 1: Customize")
            font.pixelSize: Appearance.font.pixelSize.larger
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
            color: Appearance.colors.colOnLayer1
        }
        
        StyledText {
            text: I18nService.tr("Let's start by personalizing your workspace. Choose a wallpaper and pick your favorite theme colors.")
            font.pixelSize: Appearance.font.pixelSize.small
            color: Appearance.colors.colSubtext
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // Embed the Wallpaper & Style Settings component
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: "transparent"
        clip: true

        WallpaperStyleSettings {
            id: wsSettings
            anchors.fill: parent
            anchors.leftMargin: 24 * Appearance.effectiveScale
            isOnboarding: true
        }
    }
}
