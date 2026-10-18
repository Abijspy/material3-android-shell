import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: rootMediaWidget
    visible: Config.ready && Config.options.appearance && Config.options.appearance.mediaWidget
    Layout.fillWidth: true
    implicitHeight: 120 * Appearance.effectiveScale
    radius: 20 * Appearance.effectiveScale
    color: Appearance.m3colors.m3surfaceContainerHigh

    SearchHandler { 
        searchString: "Media Player"
        visible: rootMediaWidget.visible
        aliases: ["Widget", "Media", "Player", "Music", "Spotify"]
    }

    // Whole-tile click toggles the widget
    RippleButton {
        anchors.fill: parent
        buttonRadius: parent.radius
        colBackground: "transparent"
        onClicked: {
            if (Config.ready && Config.options.appearance && Config.options.appearance.mediaWidget) {
                Config.options.appearance.mediaWidget.showOnDesktop = !Config.options.appearance.mediaWidget.showOnDesktop
            }
        }
    }

    // Top row container (Icon & Toggle)
    RowLayout {
        id: topRow
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            topMargin: 16 * Appearance.effectiveScale
            leftMargin: 16 * Appearance.effectiveScale
            rightMargin: 16 * Appearance.effectiveScale
        }

        MaterialSymbol {
            text: "play_circle"
            iconSize: 24 * Appearance.effectiveScale
            color: Appearance.colors.colPrimary
        }
        
        Item { Layout.fillWidth: true } // Spacer

        AndroidToggle {
            checked: Config.ready && Config.options.appearance && Config.options.appearance.mediaWidget && Config.options.appearance.mediaWidget.showOnDesktop
            onToggled: {
                if (Config.ready && Config.options.appearance && Config.options.appearance.mediaWidget) {
                    Config.options.appearance.mediaWidget.showOnDesktop = !Config.options.appearance.mediaWidget.showOnDesktop
                }
            }
        }
    }

    // Bottom row container (Title/Status & Reset Link)
    RowLayout {
        id: bottomRow
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            bottomMargin: 16 * Appearance.effectiveScale
            leftMargin: 16 * Appearance.effectiveScale
            rightMargin: 16 * Appearance.effectiveScale
        }

        ColumnLayout {
            spacing: 0
            
            StyledText {
                text: I18nService.tr("Media Player")
                color: Appearance.colors.colOnLayer1
            }
            StyledText {
                id: statusText
                text: (Config.ready && Config.options.appearance && Config.options.appearance.mediaWidget && Config.options.appearance.mediaWidget.showOnDesktop) ? I18nService.tr("Enabled") : I18nService.tr("Disabled")
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.colors.colSubtext
            }
        }

        Item { Layout.fillWidth: true } // Spacer

        StyledText {
            text: I18nService.tr("Reset Position")
            font.pixelSize: Appearance.font.pixelSize.small
            color: maResetMedia.containsMouse ? Appearance.colors.colPrimaryHover : Appearance.colors.colPrimary
            Layout.alignment: Qt.AlignBottom
            // Adjust baseline shift to align vertically with the bottom subtext
            Layout.bottomMargin: 1 * Appearance.effectiveScale

            MouseArea {
                id: maResetMedia
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (Config.ready && Config.options.appearance && Config.options.appearance.mediaWidget) {
                        Config.options.appearance.mediaWidget.desktopX = -1;
                        Config.options.appearance.mediaWidget.desktopY = -1;
                    }
                }
            }
        }
    }
}
