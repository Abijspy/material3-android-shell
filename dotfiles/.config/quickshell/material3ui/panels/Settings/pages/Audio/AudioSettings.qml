import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import "../../../../core/functions" as Functions
import "."
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Widgets

/**
 * Functional Audio Settings page.
 * Two-column layout: Input on the left, Output on the right.
 * Each side is a single card containing master volume (with mute),
 * device selection and per-application volume.
 */
Flickable {
    id: root
    contentHeight: mainCol.implicitHeight + (48 * Appearance.effectiveScale)
    clip: true

    ColumnLayout {
        id: mainCol
        width: parent.width - (24 * Appearance.effectiveScale)
        spacing: 32 * Appearance.effectiveScale

        // ── Header ──
        ColumnLayout {
            spacing: 4 * Appearance.effectiveScale
            StyledText {
                text: I18nService.tr("Audio")
                font.pixelSize: Appearance.font.pixelSize.huge
                font.family: Appearance.font.family.title
                font.weight: Font.DemiBold
                color: Appearance.colors.colOnLayer1
            }
            StyledText {
                text: I18nService.tr("Adjust volume levels and manage your audio input/output devices.")
                font.pixelSize: Appearance.font.pixelSize.normal
                color: Appearance.colors.colSubtext
            }
        }

        // ── Input / Output columns ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 24 * Appearance.effectiveScale

            // ══ Left column — Input ══
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 8 * Appearance.effectiveScale

                // Section header (standard style)
                RowLayout {
                    spacing: 12 * Appearance.effectiveScale
                    Layout.bottomMargin: 4 * Appearance.effectiveScale
                    MaterialSymbol {
                        text: Audio.microphoneMuted ? "mic_off" : "mic"
                        iconSize: 24 * Appearance.effectiveScale
                        color: Appearance.colors.colPrimary
                    }
                    StyledText {
                        text: I18nService.tr("Input")
                        font.pixelSize: Appearance.font.pixelSize.large
                        font.family: Appearance.font.family.title
                        font.weight: Font.Medium
                        color: Appearance.colors.colOnLayer1
                        Layout.fillWidth: true
                    }
                    AndroidToggle {
                        checked: !Audio.microphoneMuted
                        onToggled: Audio.toggleMicMute()
                    }
                }

                // Main card: volume + devices + applications
                Rectangle {
                    id: inputCard
                    Layout.fillWidth: true
                    implicitHeight: inputCardCol.implicitHeight + (32 * Appearance.effectiveScale)
                    radius: 20 * Appearance.effectiveScale
                    color: Appearance.m3colors.m3surfaceContainerHigh

                    ColumnLayout {
                        id: inputCardCol
                        anchors.fill: parent
                        anchors.margins: 16 * Appearance.effectiveScale
                        anchors.topMargin: 12 * Appearance.effectiveScale
                        anchors.bottomMargin: 12 * Appearance.effectiveScale
                        spacing: 14 * Appearance.effectiveScale

                        // Master volume
                        RowLayout {
                            spacing: 8 * Appearance.effectiveScale
                            Layout.leftMargin: 2 * Appearance.effectiveScale
                            Layout.rightMargin: 2 * Appearance.effectiveScale
                            MaterialSymbol {
                                text: Audio.microphoneMuted ? "mic_off" : "mic"
                                iconSize: 20 * Appearance.effectiveScale
                                color: Audio.microphoneMuted ? Appearance.m3colors.m3error : Appearance.colors.colPrimary
                            }
                            StyledText {
                                text: I18nService.tr("Microphone")
                                font.pixelSize: Appearance.font.pixelSize.small
                                font.weight: Font.DemiBold
                                color: Appearance.colors.colOnLayer1
                                Layout.fillWidth: true
                            }
                            StyledText {
                                text: Audio.microphoneMuted ? I18nService.tr("Muted") : Math.round(Audio.microphoneVolume * 100) + "%"
                                font.pixelSize: Appearance.font.pixelSize.small
                                color: Audio.microphoneMuted ? Appearance.m3colors.m3error : Appearance.colors.colSubtext
                            }
                        }
                        StyledSlider {
                            Layout.fillWidth: true
                            opacity: Audio.microphoneMuted ? 0.4 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            value: Audio.microphoneVolume
                            stopIndicatorValues: []
                            onMoved: Audio.setMicrophoneVolume(value)
                        }

                        // Devices
                        StyledText {
                            text: I18nService.tr("Devices")
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.Medium
                            color: Appearance.colors.colOnLayer1
                            Layout.leftMargin: 2 * Appearance.effectiveScale
                            Layout.topMargin: 8 * Appearance.effectiveScale
                        }

                        AudioDeviceList {
                            Layout.fillWidth: true
                            model: Audio.inputDevices
                            isSink: false
                            onSelected: (node) => Audio.setDefaultSource(node)
                        }

                        // Per-app streams
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: Audio.micStreamNodes.length > 0
                            spacing: 14 * Appearance.effectiveScale

                            StyledText {
                                text: I18nService.tr("Applications")
                                font.pixelSize: Appearance.font.pixelSize.small
                                font.weight: Font.Medium
                                color: Appearance.colors.colOnLayer1
                                Layout.leftMargin: 2 * Appearance.effectiveScale
                                Layout.topMargin: 8 * Appearance.effectiveScale
                            }

                            Repeater {
                                model: Audio.micStreamNodes

                                delegate: ColumnLayout {
                                    id: inputStreamItem
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6 * Appearance.effectiveScale

                                    RowLayout {
                                        spacing: 8 * Appearance.effectiveScale

                                        Item {
                                            width: 20 * Appearance.effectiveScale
                                            height: 20 * Appearance.effectiveScale

                                            IconImage {
                                                id: inputAppIcon
                                                anchors.fill: parent
                                                source: Quickshell.iconPath(Audio.appNodeIconName(inputStreamItem.modelData), "image-missing")
                                                visible: status === Image.Ready
                                            }

                                            MaterialSymbol {
                                                anchors.centerIn: parent
                                                text: "settings_input_component"
                                                iconSize: 18 * Appearance.effectiveScale
                                                color: Appearance.colors.colPrimary
                                                visible: inputAppIcon.status !== Image.Ready
                                            }
                                        }

                                        StyledText {
                                            text: Audio.appNodeDisplayName(inputStreamItem.modelData)
                                            font.pixelSize: Appearance.font.pixelSize.small
                                            font.weight: Font.DemiBold
                                            color: Appearance.colors.colOnLayer1
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        StyledText {
                                            text: Math.round(inputStreamItem.modelData.audio.volume * 100) + "%"
                                            font.pixelSize: Appearance.font.pixelSize.smaller
                                            color: Appearance.colors.colSubtext
                                        }
                                    }

                                    StyledSlider {
                                        Layout.fillWidth: true
                                        configuration: StyledSlider.Configuration.S
                                        value: inputStreamItem.modelData.audio.volume
                                        stopIndicatorValues: []
                                        onMoved: Audio.setNodeVolume(inputStreamItem.modelData, value)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ══ Right column — Output ══
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 8 * Appearance.effectiveScale

                // Section header (standard style)
                RowLayout {
                    spacing: 12 * Appearance.effectiveScale
                    Layout.bottomMargin: 4 * Appearance.effectiveScale
                    MaterialSymbol {
                        text: Audio.muted ? "volume_off" : "volume_up"
                        iconSize: 24 * Appearance.effectiveScale
                        color: Appearance.colors.colPrimary
                    }
                    StyledText {
                        text: I18nService.tr("Output")
                        font.pixelSize: Appearance.font.pixelSize.large
                        font.family: Appearance.font.family.title
                        font.weight: Font.Medium
                        color: Appearance.colors.colOnLayer1
                        Layout.fillWidth: true
                    }
                    AndroidToggle {
                        checked: !Audio.muted
                        onToggled: Audio.toggleMute()
                    }
                }

                // Main card: volume + devices + applications
                Rectangle {
                    id: outputCard
                    Layout.fillWidth: true
                    implicitHeight: outputCardCol.implicitHeight + (32 * Appearance.effectiveScale)
                    radius: 20 * Appearance.effectiveScale
                    color: Appearance.m3colors.m3surfaceContainerHigh

                    ColumnLayout {
                        id: outputCardCol
                        anchors.fill: parent
                        anchors.margins: 16 * Appearance.effectiveScale
                        anchors.topMargin: 12 * Appearance.effectiveScale
                        anchors.bottomMargin: 12 * Appearance.effectiveScale
                        spacing: 14 * Appearance.effectiveScale

                        // Master volume
                        RowLayout {
                            spacing: 8 * Appearance.effectiveScale
                            Layout.leftMargin: 2 * Appearance.effectiveScale
                            Layout.rightMargin: 2 * Appearance.effectiveScale
                            MaterialSymbol {
                                text: Audio.muted ? "volume_off" : (Audio.volume > 0.5 ? "volume_up" : (Audio.volume > 0 ? "volume_down" : "volume_mute"))
                                iconSize: 20 * Appearance.effectiveScale
                                color: Audio.muted ? Appearance.m3colors.m3error : Appearance.colors.colPrimary
                            }
                            StyledText {
                                text: I18nService.tr("Master Volume")
                                font.pixelSize: Appearance.font.pixelSize.small
                                font.weight: Font.DemiBold
                                color: Appearance.colors.colOnLayer1
                                Layout.fillWidth: true
                            }
                            StyledText {
                                text: Audio.muted ? I18nService.tr("Muted") : Math.round(Audio.volume * 100) + "%"
                                font.pixelSize: Appearance.font.pixelSize.small
                                color: Audio.muted ? Appearance.m3colors.m3error : Appearance.colors.colSubtext
                            }
                        }
                        StyledSlider {
                            Layout.fillWidth: true
                            opacity: Audio.muted ? 0.4 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            value: Audio.volume
                            stopIndicatorValues: []
                            onMoved: Audio.setVolume(value)
                        }

                        // Devices
                        StyledText {
                            text: I18nService.tr("Devices")
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.Medium
                            color: Appearance.colors.colOnLayer1
                            Layout.leftMargin: 2 * Appearance.effectiveScale
                            Layout.topMargin: 8 * Appearance.effectiveScale
                        }

                        AudioDeviceList {
                            Layout.fillWidth: true
                            model: Audio.outputDevices
                            isSink: true
                            onSelected: (node) => Audio.setDefaultSink(node)
                        }

                        // Per-app playback streams
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: Audio.streamNodes.length > 0
                            spacing: 14 * Appearance.effectiveScale

                            StyledText {
                                text: I18nService.tr("Applications")
                                font.pixelSize: Appearance.font.pixelSize.small
                                font.weight: Font.Medium
                                color: Appearance.colors.colOnLayer1
                                Layout.leftMargin: 2 * Appearance.effectiveScale
                                Layout.topMargin: 8 * Appearance.effectiveScale
                            }

                            Repeater {
                                model: Audio.streamNodes

                                delegate: ColumnLayout {
                                    id: outputStreamItem
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 6 * Appearance.effectiveScale

                                    RowLayout {
                                        spacing: 8 * Appearance.effectiveScale

                                        Item {
                                            width: 20 * Appearance.effectiveScale
                                            height: 20 * Appearance.effectiveScale

                                            IconImage {
                                                id: outputAppIcon
                                                anchors.fill: parent
                                                source: Quickshell.iconPath(Audio.appNodeIconName(outputStreamItem.modelData), "image-missing")
                                                visible: status === Image.Ready
                                            }

                                            MaterialSymbol {
                                                anchors.centerIn: parent
                                                text: "settings_input_component"
                                                iconSize: 18 * Appearance.effectiveScale
                                                color: Appearance.colors.colPrimary
                                                visible: outputAppIcon.status !== Image.Ready
                                            }
                                        }

                                        StyledText {
                                            text: Audio.appNodeDisplayName(outputStreamItem.modelData)
                                            font.pixelSize: Appearance.font.pixelSize.small
                                            font.weight: Font.DemiBold
                                            color: Appearance.colors.colOnLayer1
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        StyledText {
                                            text: Math.round(outputStreamItem.modelData.audio.volume * 100) + "%"
                                            font.pixelSize: Appearance.font.pixelSize.smaller
                                            color: Appearance.colors.colSubtext
                                        }
                                    }

                                    StyledSlider {
                                        Layout.fillWidth: true
                                        configuration: StyledSlider.Configuration.S
                                        value: outputStreamItem.modelData.audio.volume
                                        stopIndicatorValues: []
                                        onMoved: Audio.setNodeVolume(outputStreamItem.modelData, value)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── Sounds (theme, notification & ringtone) ──
        SysSounds { Layout.fillWidth: true }
    }
}
