import "../../../core"
import "../../../widgets"
import "../../../services"
import QtQuick
import QtQuick.Layouts
import Quickshell

Item {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 20 * Appearance.effectiveScale

        MaterialSymbol {
            Layout.alignment: Qt.AlignHCenter
            text: "task_alt"
            iconSize: 64 * Appearance.effectiveScale
            color: Appearance.colors.colPrimary
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10 * Appearance.effectiveScale

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                text: I18nService.tr("You're All Set!")
                font.pixelSize: Appearance.font.pixelSize.larger
                font.family: Appearance.font.family.title
                font.weight: Font.Bold
                color: Appearance.colors.colOnLayer1
            }

            StyledText {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 520 * Appearance.effectiveScale
                text: I18nService.tr("Material3UI is now fully configured and ready to use. Remember, you can always revisit this onboarding later from Settings > About. Enjoy your new workspace!")
                font.pixelSize: Appearance.font.pixelSize.normal
                color: Appearance.colors.colSubtext
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.4
            }
        }
    }
}
