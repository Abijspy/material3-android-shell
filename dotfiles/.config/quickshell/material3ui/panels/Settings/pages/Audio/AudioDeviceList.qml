import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import "../../../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

/**
 * Auto-height list of audio devices.
 * Selecting an entry makes it the default sink/source via the `selected` signal.
 */
Column {
    id: root

    property var model
    property bool isSink: true
    signal selected(var node)

    spacing: 2 * Appearance.effectiveScale

    StyledText {
        visible: !root.model || root.model.length === 0
        text: I18nService.tr("No devices found")
        font.pixelSize: Appearance.font.pixelSize.small
        color: Appearance.colors.colSubtext
        leftPadding: 12 * Appearance.effectiveScale
        topPadding: 8 * Appearance.effectiveScale
        bottomPadding: 8 * Appearance.effectiveScale
    }

    Repeater {
        model: root.model

        delegate: RippleButton {
            id: audioItem

            required property var modelData

            readonly property bool isActive: root.isSink
                ? (Audio.sink === modelData)
                : (Audio.source === modelData)

            width: root.width
            implicitHeight: 44 * Appearance.effectiveScale
            buttonRadius: 10 * Appearance.effectiveScale

            colBackground: audioItem.isActive
                ? Functions.ColorUtils.transparentize(Appearance.colors.colPrimary, 0.85)
                : "transparent"
            colBackgroundHover: audioItem.isActive
                ? Functions.ColorUtils.transparentize(Appearance.colors.colPrimary, 0.75)
                : Appearance.colors.colLayer2

            onClicked: root.selected(audioItem.modelData)

            contentItem: RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12 * Appearance.effectiveScale
                anchors.rightMargin: 12 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale

                MaterialSymbol {
                    text: {
                        if (!root.isSink) return "mic"
                        const desc = audioItem.modelData.description.toLowerCase();
                        if (desc.includes("headset") || desc.includes("headphone")) return "headphones"
                        if (desc.includes("hdmi") || desc.includes("tv")) return "tv"
                        return "speaker"
                    }
                    iconSize: 20 * Appearance.effectiveScale
                    fill: audioItem.isActive ? 1 : 0
                    color: audioItem.isActive ? Appearance.colors.colPrimary : Appearance.m3colors.m3onSurfaceVariant
                }

                StyledText {
                    Layout.fillWidth: true
                    text: Audio.friendlyDeviceName(audioItem.modelData)
                    font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: audioItem.isActive ? Font.DemiBold : Font.Normal
                    color: Appearance.colors.colOnLayer1
                    elide: Text.ElideRight
                }

                MaterialSymbol {
                    visible: audioItem.isActive
                    text: "check_circle"
                    iconSize: 18 * Appearance.effectiveScale
                    fill: 1
                    color: Appearance.colors.colPrimary
                }
            }
        }
    }
}
