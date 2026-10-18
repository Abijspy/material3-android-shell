import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Services.Pipewire

/**
 * Per-channel volume editor for a PipeWire node: a left/right balance slider
 * for stereo devices, one slider per channel, a reset that re-centres, and
 * (for outputs) speaker tests that play on a single side.
 */
ColumnLayout {
    id: root
    property PwNode node
    property bool isSink: true

    readonly property var channels: Array.from(root.node?.audio?.channels ?? [])
    readonly property var volumes: Array.from(root.node?.audio?.volumes ?? [])
    readonly property bool stereo: root.channels.length === 2
    readonly property real peak: root.volumes.length ? Math.max(...root.volumes) : 0
    readonly property bool centred: root.volumes.every(v => Math.abs(v - root.peak) < 0.005)

    // -1 full left … 0 centre … 1 full right
    readonly property real balance: {
        if (!root.stereo || root.peak <= 0) return 0;
        const l = root.volumes[0], r = root.volumes[1];
        return l >= r ? -(1 - r / l) : 1 - l / r;
    }

    visible: root.channels.length > 1
    spacing: 10 * Appearance.effectiveScale

    SearchHandler {
        searchString: "Balance"
        aliases: ["Left and right channels", "Audio channels", "Stereo balance", "Speaker test", "Left", "Right"]
    }

    function channelName(ch, index) {
        const names = {
            FrontLeft: I18nService.tr("Left"), FrontRight: I18nService.tr("Right"),
            FrontCenter: I18nService.tr("Center"), LowFrequency: I18nService.tr("Subwoofer"),
            RearLeft: I18nService.tr("Rear left"), RearRight: I18nService.tr("Rear right"),
            RearCenter: I18nService.tr("Rear center"),
            SideLeft: I18nService.tr("Side left"), SideRight: I18nService.tr("Side right"),
            Mono: I18nService.tr("Mono")
        };
        return names[PwAudioChannel.toString(ch)] || I18nService.tr("Channel %1").arg(index + 1);
    }

    function setBalance(b) {
        const p = root.peak > 0 ? root.peak : root.node.audio.volume;
        root.node.audio.volumes = [b > 0 ? p * (1 - b) : p, b < 0 ? p * (1 + b) : p];
    }

    function setChannel(index, v) {
        const vols = root.volumes.slice();
        vols[index] = v;
        root.node.audio.volumes = vols;
    }

    function centre() {
        root.node.audio.volumes = root.volumes.map(() => root.peak);
    }

    // Speech from the freedesktop theme, panned to one side with ffmpeg
    function testSide(left) {
        const f = "/usr/share/sounds/freedesktop/stereo/audio-channel-front-" + (left ? "left" : "right") + ".oga";
        Quickshell.execDetached(["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet",
            "-af", left ? "pan=stereo|c0=c0|c1=0*c0" : "pan=stereo|c0=0*c0|c1=c0", f]);
    }

    // Header
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 2 * Appearance.effectiveScale
        Layout.topMargin: 8 * Appearance.effectiveScale
        spacing: 8 * Appearance.effectiveScale
        StyledText {
            text: I18nService.tr("Channels")
            font.pixelSize: Appearance.font.pixelSize.small
            font.weight: Font.Medium
            color: Appearance.colors.colOnLayer1
            Layout.fillWidth: true
        }
        RippleButton {
            visible: !root.centred
            implicitHeight: 28 * Appearance.effectiveScale
            implicitWidth: resetRow.implicitWidth + 20 * Appearance.effectiveScale
            buttonRadius: 14 * Appearance.effectiveScale
            colBackground: Appearance.m3colors.m3surfaceContainerLow
            onClicked: root.centre()
            RowLayout {
                id: resetRow
                anchors.centerIn: parent
                spacing: 4 * Appearance.effectiveScale
                MaterialSymbol { text: "align_horizontal_center"; iconSize: 16 * Appearance.effectiveScale; color: Appearance.colors.colOnLayer1 }
                StyledText { text: I18nService.tr("Centre"); font.pixelSize: Appearance.font.pixelSize.smaller; color: Appearance.colors.colOnLayer1 }
            }
        }
    }

    // Balance (stereo only)
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.stereo
        spacing: 4 * Appearance.effectiveScale

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 2 * Appearance.effectiveScale
            Layout.rightMargin: 2 * Appearance.effectiveScale
            StyledText { text: "L"; font.pixelSize: Appearance.font.pixelSize.smaller; font.weight: Font.DemiBold; color: Appearance.colors.colSubtext }
            StyledText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Math.abs(root.balance) < 0.01 ? I18nService.tr("Balance · Centred")
                    : I18nService.tr("Balance · %1% %2").arg(Math.round(Math.abs(root.balance) * 100))
                        .arg(root.balance < 0 ? I18nService.tr("left") : I18nService.tr("right"))
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.colors.colSubtext
            }
            StyledText { text: "R"; font.pixelSize: Appearance.font.pixelSize.smaller; font.weight: Font.DemiBold; color: Appearance.colors.colSubtext }
        }
        StyledSlider {
            Layout.fillWidth: true
            configuration: StyledSlider.Configuration.S
            from: -1
            to: 1
            value: root.balance
            defaultValue: 0
            usePercentTooltip: false
            tooltipContent: Math.abs(value) < 0.01 ? "C" : (value < 0 ? "L " : "R ") + Math.round(Math.abs(value) * 100)
            // Snap to centre near the middle
            onMoved: root.setBalance(Math.abs(value) < 0.04 ? 0 : value)
        }
    }

    // Per-channel levels
    Repeater {
        model: root.channels.length
        delegate: ColumnLayout {
            id: channelItem
            required property int index
            readonly property real level: root.volumes[index] ?? 0
            Layout.fillWidth: true
            spacing: 4 * Appearance.effectiveScale

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 2 * Appearance.effectiveScale
                Layout.rightMargin: 2 * Appearance.effectiveScale
                spacing: 8 * Appearance.effectiveScale
                StyledText {
                    text: root.channelName(root.channels[channelItem.index], channelItem.index)
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    font.weight: Font.DemiBold
                    color: Appearance.colors.colOnLayer1
                    Layout.fillWidth: true
                }
                StyledText {
                    text: Math.round(channelItem.level * 100) + "%"
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colSubtext
                }
                RippleButton {
                    // Test tones exist for front left / right
                    visible: root.isSink && root.stereo
                    implicitWidth: 28 * Appearance.effectiveScale
                    implicitHeight: 28 * Appearance.effectiveScale
                    buttonRadius: 14 * Appearance.effectiveScale
                    colBackground: "transparent"
                    onClicked: root.testSide(channelItem.index === 0)
                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "speaker"
                        iconSize: 18 * Appearance.effectiveScale
                        color: Appearance.colors.colPrimary
                    }
                    StyledToolTip {
                        extraVisibleCondition: parent.hovered || parent.realHovered
                        text: I18nService.tr("Play a test sound on this side")
                    }
                }
            }
            StyledSlider {
                Layout.fillWidth: true
                configuration: StyledSlider.Configuration.S
                value: channelItem.level
                stopIndicatorValues: []
                onMoved: root.setChannel(channelItem.index, value)
            }
        }
    }
}
