import "../../core"
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts

/**
 * Status bar plugin for Arch / AUR / Flatpak updates.
 * Left click: Settings → Packages & Updates · Right click: check now ·
 * Middle click: update in a terminal.
 */
Item {
    id: root
    property color color: Appearance.colors.colStatusBarText

    readonly property bool busy: ArchUpdates.checking || ArchUpdates.upgrading

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4 * Appearance.effectiveScale

        MaterialSymbol {
            id: glyph
            Layout.alignment: Qt.AlignVCenter
            text: root.busy ? "progress_activity" : ArchUpdates.count > 0 ? "system_update_alt" : "verified"
            iconSize: 18 * Appearance.effectiveScale
            fill: ArchUpdates.count > 0 ? 1 : 0
            color: root.color
            RotationAnimation on rotation {
                running: root.busy
                from: 0; to: 360; duration: 900; loops: Animation.Infinite
                onRunningChanged: if (!running) glyph.rotation = 0
            }
        }

        StyledText {
            Layout.alignment: Qt.AlignVCenter
            visible: ArchUpdates.count > 0
            text: ArchUpdates.count
            font.pixelSize: Appearance.font.pixelSize.small
            font.weight: Font.Medium
            color: root.color
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -6 * Appearance.effectiveScale
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton) ArchUpdates.check();
            else if (mouse.button === Qt.MiddleButton) ArchUpdates.upgrade();
            else GlobalStates.openAboutPage("archupdates");
        }
    }
}
