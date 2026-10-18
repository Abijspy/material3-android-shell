pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell
import Quickshell.Wayland
import "../core"
import "../services"
import "../core/functions" as Functions
import "."

PanelWindow {
    id: root
    
    WlrLayershell.namespace: "quickshell:accentPicker"
    WlrLayershell.layer: WlrLayer.Overlay 
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive
    exclusionMode: ExclusionMode.Ignore
    
    // Explicitly cover everything
    anchors {
        left: true
        right: true
        top: true
        bottom: true
    }

    color: "black"
    visible: GlobalStates.accentPickerOpen

    property bool isPicking: false

    onVisibleChanged: {
        if (!visible) {
            isPicking = false;
        }
    }

    // A more reliable way to catch ESC globally in a window
    Shortcut {
        sequence: "Escape"
        onActivated: GlobalStates.accentPickerOpen = false
    }

    Image {
        id: bgImage
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        source: {
            // If picking for lockscreen, show lockscreen wallpaper (if it exists and separate wallpaper is enabled)
            if (GlobalStates.accentPickerTarget === "lock") {
                if (Config.options.lock && Config.options.lock.useSeparateWallpaper && Config.options.lock.wallpaperPath !== "") {
                    return Config.options.lock.wallpaperPath;
                }
                return Config.options.appearance.background.wallpaperPath;
            }

            // If picking for desktop, check active live wallpaper screenshot/frame
            if (WallpaperEngineService.active && WallpaperEngineService.screenshotPath !== "") {
                return "file://" + WallpaperEngineService.screenshotPath + "?v=" + WallpaperEngineService.screenshotVersion;
            }
            if (MpvpaperService.active && MpvpaperService.framePath !== "") {
                return "file://" + MpvpaperService.framePath + "?v=" + MpvpaperService.frameVersion;
            }

            return Config.options.appearance.background.wallpaperPath;
        }
        asynchronous: true
    }

    // Subtle dim overlay to help text readability at the bottom
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 250 * Appearance.effectiveScale
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Functions.ColorUtils.applyAlpha(Appearance.m3colors.m3surface, 0.95) }
        }
    }

    // Transparent MouseArea to block clicks to layers below while Picker is active
    MouseArea {
        anchors.fill: parent
        // Just consume the events here so they don't propagate underneath
        hoverEnabled: true
    }

    ColumnLayout {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 32 * Appearance.effectiveScale
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 16 * Appearance.effectiveScale
        visible: !root.isPicking

        StyledText {
            text: I18nService.tr("Select Matugen Scheme")
            font.pixelSize: Appearance.font.pixelSize.large
            font.family: Appearance.font.family.title
            font.weight: Font.Medium
            color: Appearance.m3colors.m3onSurface
            Layout.alignment: Qt.AlignHCenter
        }
        
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8 * Appearance.effectiveScale
            
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8 * Appearance.effectiveScale
                
                Repeater {
                    model: LauncherSearch.matugenSchemes.filter(s => s.id !== "scheme-monochrome").slice(0, 4)
                    
                    delegate: RippleButton {
                        required property var modelData
                        
                        Layout.preferredWidth: 100 * Appearance.effectiveScale
                        Layout.preferredHeight: 32 * Appearance.effectiveScale
                        
                        property bool isSelected: Config.ready && Config.options.appearance.background.matugenScheme === modelData.id
                        
                        buttonRadius: isSelected ? 16 * Appearance.effectiveScale : 8 * Appearance.effectiveScale
                        colBackground: isSelected ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerHigh
                        colBackgroundHover: isSelected ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerHighest
                        
                        StyledText {
                            anchors.centerIn: parent
                            text: parent.modelData.name
                            font.pixelSize: Appearance.font.pixelSize.small
                            color: parent.isSelected ? Appearance.colors.colOnPrimary : Appearance.m3colors.m3onSurface
                        }
                        
                        onClicked: {
                            if (Config.ready) {
                                Config.options.appearance.background.matugenScheme = modelData.id;
                            }
                        }
                    }
                }
            }
            
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8 * Appearance.effectiveScale
                
                Repeater {
                    model: LauncherSearch.matugenSchemes.filter(s => s.id !== "scheme-monochrome").slice(4)
                    
                    delegate: RippleButton {
                        required property var modelData
                        
                        Layout.preferredWidth: 100 * Appearance.effectiveScale
                        Layout.preferredHeight: 32 * Appearance.effectiveScale
                        
                        property bool isSelected: Config.ready && Config.options.appearance.background.matugenScheme === modelData.id
                        
                        buttonRadius: isSelected ? 16 * Appearance.effectiveScale : 8 * Appearance.effectiveScale
                        colBackground: isSelected ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerHigh
                        colBackgroundHover: isSelected ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerHighest
                        
                        StyledText {
                            anchors.centerIn: parent
                            text: parent.modelData.name
                            font.pixelSize: Appearance.font.pixelSize.small
                            color: parent.isSelected ? Appearance.colors.colOnPrimary : Appearance.m3colors.m3onSurface
                        }
                        
                        onClicked: {
                            if (Config.ready) {
                                Config.options.appearance.background.matugenScheme = modelData.id;
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 16 * Appearance.effectiveScale

            RippleButton {
                Layout.preferredHeight: 48 * Appearance.effectiveScale
                Layout.preferredWidth: 150 * Appearance.effectiveScale
                colBackground: Appearance.m3colors.m3surfaceContainerHigh
                colBackgroundHover: Appearance.m3colors.m3surfaceContainerHighest
                buttonRadius: 24 * Appearance.effectiveScale
                
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8 * Appearance.effectiveScale
                    MaterialSymbol {
                        text: "close"
                        iconSize: 20 * Appearance.effectiveScale
                        color: Appearance.m3colors.m3onSurface
                    }
                    StyledText {
                        text: I18nService.tr("Cancel")
                        color: Appearance.m3colors.m3onSurface
                        font.weight: Font.DemiBold
                    }
                }
                
                onClicked: {
                    GlobalStates.accentPickerOpen = false;
                }
            }

            RippleButton {
                Layout.preferredHeight: 48 * Appearance.effectiveScale
                Layout.preferredWidth: 200 * Appearance.effectiveScale
                colBackground: Appearance.colors.colPrimary
                colBackgroundHover: Qt.lighter(Appearance.colors.colPrimary, 1.15)
                buttonRadius: 24 * Appearance.effectiveScale
                
                RowLayout {
                    anchors.centerIn: parent
                    spacing: 8 * Appearance.effectiveScale
                    MaterialSymbol {
                        text: "colorize"
                        iconSize: 20 * Appearance.effectiveScale
                        color: Appearance.colors.colOnPrimary
                    }
                    StyledText {
                        text: I18nService.tr("Pick Color")
                        color: Appearance.colors.colOnPrimary
                        font.weight: Font.DemiBold
                    }
                }
                
                onClicked: {
                    root.isPicking = true;
                    Wallpapers.pickAccent(GlobalStates.accentPickerTarget);
                }
            }
        }
    }
}
