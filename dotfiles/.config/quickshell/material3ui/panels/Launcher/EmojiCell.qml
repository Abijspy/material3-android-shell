import QtQuick
import "../../widgets"
import "../../core"
import "../../services"

RippleButton {
    id: root

    property var emoji: modelData
    property bool selected: false

    colBackground: root.selected ? Qt.alpha(Appearance.m3colors.m3primary, 0.18) : "transparent"
    buttonRadius: Appearance.rounding.small

    onClicked: {
        if (emoji) LauncherSearch.useEmoji(emoji);
    }

    StyledText {
        anchors.centerIn: parent
        text: (emoji && emoji.emoji) ? emoji.emoji : ""
        font.pixelSize: Math.round(26 * Appearance.effectiveScale)
    }
}
