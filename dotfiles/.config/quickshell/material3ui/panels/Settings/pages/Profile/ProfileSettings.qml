import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import "."
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell

Flickable {
    id: root
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    contentHeight: mainCol.implicitHeight + (48 * Appearance.effectiveScale)
    clip: true

    ScrollBar.vertical: StyledScrollBar {}

    ColumnLayout {
        id: mainCol
        width: parent.width - (24 * Appearance.effectiveScale)
        spacing: 24 * Appearance.effectiveScale

        // ── Header ──
        ColumnLayout {
            spacing: 4 * Appearance.effectiveScale
            StyledText {
                text: I18nService.tr("Profile")
                font.pixelSize: Appearance.font.pixelSize.huge
                font.family: Appearance.font.family.title
                font.weight: Font.DemiBold
                color: Appearance.colors.colOnLayer1
            }
            StyledText {
                text: I18nService.tr("Avatar, display name, hostname, and configuration presets.")
                font.pixelSize: Appearance.font.pixelSize.normal
                color: Appearance.colors.colSubtext
            }
        }

        PrAvatar { Layout.fillWidth: true }
        PrIdentity { Layout.fillWidth: true }
        PrPresets { Layout.fillWidth: true }

        Item { Layout.fillHeight: true; Layout.preferredHeight: 32 * Appearance.effectiveScale }
    }
}
