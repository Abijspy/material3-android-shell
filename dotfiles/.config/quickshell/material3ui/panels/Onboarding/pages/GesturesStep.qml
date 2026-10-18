import "../../../core"
import "../../../widgets"
import "../../../services"
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell

ColumnLayout {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 12 * Appearance.effectiveScale

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Step 3: Desktop & Dock Gestures")
            font.pixelSize: Appearance.font.pixelSize.larger
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
            color: Appearance.colors.colOnLayer1
        }
        
        StyledText {
            text: I18nService.tr("Material3UI comes with powerful mouse and swipe gestures built-in. Learn how to navigate your desktop effortlessly!")
            font.pixelSize: Appearance.font.pixelSize.small
            color: Appearance.colors.colSubtext
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    // ── Desktop Mockup Container ──
    Rectangle {
        id: mockupContainer
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: Appearance.colors.colLayer0
        radius: 12 * Appearance.effectiveScale
        border.width: Math.max(1, 1 * Appearance.effectiveScale)
        border.color: Appearance.colors.colOutlineVariant
        clip: true

        Image {
            anchors.fill: parent
            source: "file://" + Quickshell.env("HOME") + "/.local/src/material3ui/dotfiles/.config/quickshell/material3ui/assets/wallpapers/default_wallpaper.png"
            fillMode: Image.PreserveAspectCrop
            opacity: 0.2
        }

        // Layout representing Desktop (top) & Dock (bottom)
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14 * Appearance.effectiveScale
            spacing: 8 * Appearance.effectiveScale

            // --- Desktop Gestures (Top Area) ---
            RowLayout {
                spacing: 10 * Appearance.effectiveScale
                Rectangle {
                    width: 32 * Appearance.effectiveScale
                    height: 32 * Appearance.effectiveScale
                    radius: 16 * Appearance.effectiveScale
                    color: Appearance.colors.colPrimary
                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "ads_click"
                        iconSize: 16 * Appearance.effectiveScale
                        color: Appearance.colors.colOnPrimary
                    }
                }
                ColumnLayout {
                    spacing: 1 * Appearance.effectiveScale
                    StyledText {
                        text: I18nService.tr("Right Click Desktop")
                        font.weight: Font.DemiBold
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colOnLayer1
                    }
                    StyledText {
                        text: I18nService.tr("Opens Context Menu for Spotlight, Terminal, SysMon, or Wallpaper Settings.")
                        font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                        color: Appearance.colors.colSubtext
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                spacing: 10 * Appearance.effectiveScale
                Rectangle {
                    width: 32 * Appearance.effectiveScale
                    height: 32 * Appearance.effectiveScale
                    radius: 16 * Appearance.effectiveScale
                    color: Appearance.colors.colPrimary
                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "swipe_up"
                        iconSize: 16 * Appearance.effectiveScale
                        color: Appearance.colors.colOnPrimary
                    }
                }
                ColumnLayout {
                    spacing: 1 * Appearance.effectiveScale
                    StyledText {
                        text: I18nService.tr("Swipe Up on Desktop")
                        font.weight: Font.DemiBold
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colOnLayer1
                    }
                    StyledText {
                        text: I18nService.tr("Opens the App Launcher (App Drawer).")
                        font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                        color: Appearance.colors.colSubtext
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            RowLayout {
                spacing: 10 * Appearance.effectiveScale
                Rectangle {
                    width: 32 * Appearance.effectiveScale
                    height: 32 * Appearance.effectiveScale
                    radius: 16 * Appearance.effectiveScale
                    color: Appearance.colors.colPrimary
                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "swipe_down"
                        iconSize: 16 * Appearance.effectiveScale
                        color: Appearance.colors.colOnPrimary
                    }
                }
                ColumnLayout {
                    spacing: 1 * Appearance.effectiveScale
                    StyledText {
                        text: I18nService.tr("Swipe Down on Desktop (3 Regions)")
                        font.weight: Font.DemiBold
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colOnLayer1
                    }
                    StyledText {
                        text: I18nService.tr("Swipe down on Left ⅓ for Notification Center, Middle ⅓ for Dashboard, or Right ⅓ for Quick Settings.")
                        font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                        color: Appearance.colors.colSubtext
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Item { Layout.fillHeight: true } // Flexible spacer to push Dock gestures to bottom

            // --- Dock Gestures (Bottom Area) ---
            RowLayout {
                spacing: 10 * Appearance.effectiveScale
                Rectangle {
                    width: 32 * Appearance.effectiveScale
                    height: 32 * Appearance.effectiveScale
                    radius: 16 * Appearance.effectiveScale
                    color: Appearance.colors.colPrimary
                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "dock"
                        iconSize: 16 * Appearance.effectiveScale
                        color: Appearance.colors.colOnPrimary
                    }
                }
                ColumnLayout {
                    spacing: 1 * Appearance.effectiveScale
                    StyledText {
                        text: I18nService.tr("Right Click on Dock")
                        font.weight: Font.DemiBold
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colOnLayer1
                    }
                    StyledText {
                        text: I18nService.tr("Opens Dock Context Menu to manage apps (Pin, Close, New Window) & power actions.")
                        font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                        color: Appearance.colors.colSubtext
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            // Mock Dock Illustration at the very bottom
            Rectangle {
                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                implicitWidth: 160 * Appearance.effectiveScale
                implicitHeight: 36 * Appearance.effectiveScale
                radius: 18 * Appearance.effectiveScale
                color: Appearance.colors.colLayer2
                opacity: 0.9

                Row {
                    anchors.centerIn: parent
                    spacing: 10 * Appearance.effectiveScale
                    Repeater {
                        model: 4
                        Rectangle {
                            width: 24 * Appearance.effectiveScale
                            height: 24 * Appearance.effectiveScale
                            radius: 12 * Appearance.effectiveScale
                            color: Appearance.colors.colPrimary
                            opacity: 0.8
                        }
                    }
                }
            }
        }
    }
}
