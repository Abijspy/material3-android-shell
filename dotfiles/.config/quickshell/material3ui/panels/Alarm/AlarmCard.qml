import "../../core"
import "../../services"
import "../../widgets"
import "../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts

/**
 * Android-style alarm card — shared by AlarmPanel (desktop overlay) and
 * LockSurface (embedded while the session is locked).
 * Shows a big live clock, the alarm message, and Snooze/Stop pill buttons.
 * Keyboard: Esc stops the alarm, S snoozes it.
 */
Rectangle {
    id: root

    // Adapts to the lockscreen palette when embedded in the lock surface
    property bool isLockscreen: false

    implicitWidth: 440 * Appearance.effectiveScale
    implicitHeight: cardColumn.implicitHeight + (48 * Appearance.effectiveScale)
    radius: Appearance.rounding.large * 1.5
    color: isLockscreen ? Appearance.lockM3colors.m3surfaceContainer : Appearance.colors.colLayer1

    // Follows Config timeStyle; 12-hour shows plain digits without AM/PM text (Android style).
    // Qt only renders "hh" as 12-hour when the format contains ap/AP — so compute it manually.
    function formatClock(d) {
        const is24 = (Config.ready && Config.options.time) ? Config.options.time.timeStyle === "24H" : true;
        if (is24) return Qt.formatTime(d, "HH:mm");
        let h = d.getHours() % 12;
        if (h === 0) h = 12;
        return String(h).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
    }

    // Change notifier so currentTime re-syncs when the time style config changes
    readonly property string timeStyle: (Config.ready && Config.options.time) ? Config.options.time.timeStyle : "24H"
    property string currentTime: formatClock(new Date())
    onTimeStyleChanged: currentTime = formatClock(new Date())

    Timer {
        interval: 1000
        repeat: true
        running: AlarmService.ringing
        onTriggered: root.currentTime = root.formatClock(new Date())
    }

    // Sync immediately when the alarm starts ringing (initial value may be stale)
    Connections {
        target: AlarmService
        function onRingingChanged() {
            if (AlarmService.ringing) root.currentTime = root.formatClock(new Date());
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            AlarmService.stop();
            event.accepted = true;
        } else if (event.key === Qt.Key_S) {
            AlarmService.snooze();
            event.accepted = true;
        }
    }

    Component.onCompleted: {
        opacity = 0;
        scale = 0.95;
        slide.y = -24 * Appearance.effectiveScale;
        entryAnim.restart();
    }

    ParallelAnimation {
        id: entryAnim
        NumberAnimation { target: root; property: "opacity"; from: 0; to: 1; duration: 250; easing.type: Easing.OutQuint }
        NumberAnimation { target: slide; property: "y"; from: -24 * Appearance.effectiveScale; to: 0; duration: 400; easing.type: Easing.OutQuint }
        NumberAnimation { target: root; property: "scale"; from: 0.95; to: 1; duration: 400; easing.type: Easing.OutBack }
    }

    transform: Translate { id: slide }

    StyledRectangularShadow { target: root; z: -1 }

    // Block click-through / eat stray clicks inside the card
    MouseArea { anchors.fill: parent }

    ColumnLayout {
        id: cardColumn
        anchors.fill: parent
        anchors.margins: 24 * Appearance.effectiveScale
        spacing: 4 * Appearance.effectiveScale

        Item { Layout.fillHeight: true }

        StyledText {
            text: root.currentTime
            Layout.alignment: Qt.AlignHCenter
            font.family: Appearance.font.family.main
            font.pixelSize: Math.round(64 * Appearance.effectiveScale)
            font.weight: Font.Medium
            color: root.isLockscreen ? Appearance.lockM3colors.m3onSurface : Appearance.colors.colOnLayer1
        }

        StyledText {
            text: AlarmService.message !== "" ? AlarmService.message : I18nService.tr("Alarm")
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            font.pixelSize: Math.round(15 * Appearance.effectiveScale)
            color: root.isLockscreen ? Appearance.lockM3colors.m3onSurfaceVariant : Appearance.colors.colSubtext
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }

        Item { Layout.preferredHeight: 8 * Appearance.effectiveScale }

        // --- Snooze / Stop buttons (M3 button large: 96h, 48h-padding, 24 text, 2 gap) ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 2 * Appearance.effectiveScale

            RippleButton {
                Layout.fillWidth: true
                implicitHeight: 96 * Appearance.effectiveScale
                buttonRadius: 48 * Appearance.effectiveScale
                colBackground: root.isLockscreen ? Appearance.lockM3colors.m3secondaryContainer : Appearance.m3colors.m3secondaryContainer
                colRipple: Functions.ColorUtils.applyAlpha(root.isLockscreen ? Appearance.lockM3colors.m3onSecondaryContainer : Appearance.m3colors.m3onSecondaryContainer, 0.12)
                onClicked: AlarmService.snooze()

                StyledText {
                    anchors.centerIn: parent
                    text: I18nService.tr("Snooze")
                    font.pixelSize: 24 * Appearance.effectiveScale
                    font.weight: Font.Medium
                    color: root.isLockscreen ? Appearance.lockM3colors.m3onSecondaryContainer : Appearance.m3colors.m3onSecondaryContainer
                }
            }

            RippleButton {
                Layout.fillWidth: true
                implicitHeight: 96 * Appearance.effectiveScale
                buttonRadius: 48 * Appearance.effectiveScale
                colBackground: root.isLockscreen ? Appearance.lockM3colors.m3tertiaryContainer : Appearance.m3colors.m3tertiaryContainer
                colRipple: Functions.ColorUtils.applyAlpha(root.isLockscreen ? Appearance.lockM3colors.m3onTertiaryContainer : Appearance.m3colors.m3onTertiaryContainer, 0.12)
                onClicked: AlarmService.stop()

                StyledText {
                    anchors.centerIn: parent
                    text: I18nService.tr("Stop")
                    font.pixelSize: 24 * Appearance.effectiveScale
                    font.weight: Font.Medium
                    color: root.isLockscreen ? Appearance.lockM3colors.m3onTertiaryContainer : Appearance.m3colors.m3onTertiaryContainer
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
