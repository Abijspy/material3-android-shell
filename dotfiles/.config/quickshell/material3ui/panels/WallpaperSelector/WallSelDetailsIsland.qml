import "../../core"
import "../../services"
import "../../widgets"
import "../../core/functions" as Functions
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt.labs.folderlistmodel
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Io

                Rectangle {
                    id: detailsIsland
                    property Item mainSelector
                    Layout.fillHeight: true
                    Layout.preferredWidth: mainSelector.showDetails ? 320 * Appearance.effectiveScale : 0
                    visible: mainSelector.showDetails
                    color: Appearance.colors.colLayer1
                    radius: 28 * Appearance.effectiveScale
                    clip: true
                    opacity: 0.98

                    Behavior on Layout.preferredWidth {
                        NumberAnimation { duration: 250; easing.type: Easing.OutQuart }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16 * Appearance.effectiveScale
                        spacing: 16 * Appearance.effectiveScale
                        visible: mainSelector.selectedWallpaper !== null

                        StyledText {
                            text: mainSelector.selectedWallpaper ? mainSelector.selectedWallpaper.title : I18nService.tr("Wallpaper Details")
                            font.pixelSize: Appearance.font.pixelSize.normal
                            font.weight: Font.DemiBold
                            color: Appearance.colors.colOnLayer1
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        // Preview & Info
                        Rectangle {
                            id: previewPlate
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180 * Appearance.effectiveScale
                            radius: 16 * Appearance.effectiveScale
                            color: Appearance.colors.colLayer2
                            layer.enabled: true
                            layer.effect: OpacityMask {
                                maskSource: Rectangle { width: previewPlate.width; height: previewPlate.height; radius: 16 * Appearance.effectiveScale }
                            }

                            VideoThumbnail {
                                anchors.fill: parent
                                videoPath: mainSelector.inVideoMode && mainSelector.selectedWallpaper ? mainSelector.selectedWallpaper.folder : ""
                                visible: videoPath !== ""
                            }

                            AnimatedImage {
                                anchors.fill: parent
                                source: (!mainSelector.inVideoMode && mainSelector.selectedWallpaper) ? mainSelector.selectedWallpaper.preview : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                playing: true
                                cache: true
                                visible: source !== ""
                            }

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: "transparent" }
                                    GradientStop { position: 1.0; color: Qt.rgba(0,0,0, 0.5) }
                                }
                            }

                            StyledText {
                                anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 12 * Appearance.effectiveScale
                                text: mainSelector.selectedWallpaper ? mainSelector.selectedWallpaper.id : ""
                                color: "white"
                                font.pixelSize: Appearance.font.pixelSize.smallest
                                opacity: 0.8
                            }
                        }

                        ScrollView {
                            id: detailsScroll
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded

                            ColumnLayout {
                                width: detailsScroll.availableWidth
                                spacing: 12 * Appearance.effectiveScale

                                StyledText {
                                    text: I18nService.tr("Properties")
                                    font.pixelSize: Appearance.font.pixelSize.small
                                    font.weight: Font.Medium
                                    color: Appearance.colors.colSubtext
                                    visible: !mainSelector.inVideoMode && WallpaperEngineService.currentProperties.count > 0
                                }

                                // Video info block (mpvpaper)
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8 * Appearance.effectiveScale
                                    visible: mainSelector.inVideoMode

                                    StyledText {
                                        text: I18nService.tr("Video Wallpaper")
                                        font.pixelSize: Appearance.font.pixelSize.small
                                        font.weight: Font.Medium
                                        color: Appearance.colors.colSubtext
                                        Layout.fillWidth: true
                                    }

                                    StyledText {
                                        text: mainSelector.selectedWallpaper ? String(mainSelector.selectedWallpaper.title) : ""
                                        font.pixelSize: Appearance.font.pixelSize.smaller
                                        color: Appearance.colors.colOnLayer1
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }

                                    StyledText {
                                        text: I18nService.tr("A video file, played with mpv. Wallpaper transitions are not available for videos.")
                                        font.pixelSize: Appearance.font.pixelSize.smallest
                                        color: Appearance.colors.colSubtext
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                Repeater {
                                    model: WallpaperEngineService.currentProperties
                                    visible: !mainSelector.inVideoMode
                                    delegate: ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 8 * Appearance.effectiveScale

                                        RowLayout {
                                            Layout.fillWidth: true
                                            StyledText {
                                                text: propText || ""
                                                font.pixelSize: Appearance.font.pixelSize.smaller
                                                color: Appearance.colors.colOnLayer1
                                                Layout.fillWidth: true
                                            }
                                            
                                            // Boolean Checkbox
                                            AndroidToggle {
                                                visible: propType === "bool"
                                                checked: valBool
                                                onToggled: {
                                                    WallpaperEngineService.updateProperty(propKey, !checked);
                                                }
                                            }
                                        }

                                        // Slider for numbers
                                        StyledSlider {
                                            visible: propType === "slider"
                                            Layout.fillWidth: true
                                            from: propMin
                                            to: propMax
                                            value: valNum
                                            // Use onMoved to only trigger update when user actively changes it
                                            onMoved: {
                                                WallpaperEngineService.updateProperty(propKey, value);
                                            }
                                        }
                                        
                                        // Combo Box
                                        StyledComboBox {
                                            visible: propType === "combo"
                                            Layout.fillWidth: true
                                            searchable: false
                                            text: {
                                                if (!options_json || options_json === "" || options_json === "[]") return "";
                                                try {
                                                    let opts = JSON.parse(options_json);
                                                    let current = opts.find(o => String(o.value) === String(valNum) || String(o.value) === String(valStr));
                                                    return current ? current.label : "";
                                                } catch(e) { return ""; }
                                            }
                                            model: {
                                                if (!options_json || options_json === "" || options_json === "[]") return [];
                                                try {
                                                    let opts = JSON.parse(options_json);
                                                    return opts.map(o => o.label);
                                                } catch(e) { return []; }
                                            }
                                            onAccepted: (label) => {
                                                try {
                                                    let opts = JSON.parse(options_json);
                                                    let found = opts.find(o => o.label === label);
                                                    if (found) {
                                                        WallpaperEngineService.updateProperty(propKey, found.value);
                                                    }
                                                } catch(e) {}
                                            }
                                        }

                                        Item { Layout.preferredHeight: 4 * Appearance.effectiveScale }
                                    }
                                }

                                // Placeholder if no properties
                                StyledText {
                                    text: I18nService.tr("No properties available for this wallpaper.")
                                    font.pixelSize: Appearance.font.pixelSize.smaller
                                    color: Appearance.colors.colSubtext
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    visible: !mainSelector.inVideoMode && WallpaperEngineService.currentProperties.count === 0 && !WallpaperEngineService.loading
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12 * Appearance.effectiveScale

                            RippleButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: 44 * Appearance.effectiveScale
                                buttonText: I18nService.tr("Apply")
                                enabled: !GameMode.active
                                opacity: enabled ? 1 : 0.5
                                colBackground: Appearance.colors.colPrimary
                                colText: Appearance.colors.colOnPrimary
                                onClicked: {
                                    if (mainSelector.selectedWallpaper) {
                                        if (mainSelector.inVideoMode) {
                                            MpvpaperService.apply(mainSelector.selectedWallpaper.folder);
                                        } else {
                                            WallpaperEngineService.apply(mainSelector.selectedWallpaper.folder, mainSelector.selectedWallpaper.preview);
                                        }
                                        mainSelector.close();
                                    }
                                }
                            }

                            RippleButton {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitHeight: 44 * Appearance.effectiveScale
                                buttonText: I18nService.tr("Reset")
                                colBackground: Appearance.colors.colLayer2
                                colText: Appearance.colors.colOnLayer2
                                visible: !mainSelector.inVideoMode && WallpaperEngineService.currentProperties.count > 0
                                onClicked: {
                                    if (mainSelector.selectedWallpaper) {
                                        WallpaperEngineService.resetProperties(mainSelector.selectedWallpaper.folder);
                                    }
                                }
                                StyledToolTip { text: I18nService.tr("Reset properties to default") }
                            }
                        }
                    }

                    // No selection placeholder
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 12 * Appearance.effectiveScale
                        visible: mainSelector.selectedWallpaper === null && mainSelector.showDetails
                        
                        MaterialSymbol {
                            text: "wallpaper"
                            iconSize: 48 * Appearance.effectiveScale
                            color: Appearance.colors.colSubtext
                            Layout.alignment: Qt.AlignHCenter
                        }
                        
                        StyledText {
                            text: I18nService.tr("Select a wallpaper to see details")
                            color: Appearance.colors.colSubtext
                            font.pixelSize: Appearance.font.pixelSize.small
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }
                }
