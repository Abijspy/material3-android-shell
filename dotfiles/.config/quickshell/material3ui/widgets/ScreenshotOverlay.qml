import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import "."
import "../core"
import "../core/functions" as Functions
import "../services"
import "../panels/RegionSelector/utils"

/**
 * Android 16 styled screenshot preview overlay.
 * Fixed: Clicks pass through transparent areas. Correct auto-hide timer.
 */
PanelWindow {
    id: root

    required property var targetScreen
    screen: targetScreen

    property string imagePath: ""
    property string displayPath: "" 
    property bool isDeleting: false

    anchors {
        left: true
        bottom: true
    }

    implicitWidth: 450 * Appearance.effectiveScale
    implicitHeight: 550 * Appearance.effectiveScale

    color: "transparent"
    visible: imagePath !== "" && !isDeleting

    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    exclusionMode: ExclusionMode.Ignore

    // Fix: Mask ensures clicks pass through transparent areas
    mask: Region {
        item: swipeWrapper
    }

    onImagePathChanged: {
        if (imagePath !== "") {
            displayPath = "file://" + imagePath + "?" + new Date().getTime();
            hideTimer.restart();
        }
    }

    Timer {
        id: hideTimer
        interval: 3000
        repeat: false
        onTriggered: root.imagePath = ""
    }

    // ── Content Container ──
    Item {
        id: mainContent
        anchors.fill: parent
        anchors.margins: 24 * Appearance.effectiveScale

        // Hover detection localized to content only
        MouseArea {
            id: globalHoverArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            onContainsMouseChanged: {
                if (containsMouse) hideTimer.stop();
                else if (root.visible) hideTimer.restart();
            }
        }

        // Swipe Wrapper that bounds the visual parts and handles dragging
        Item {
            id: swipeWrapper
            x: 0
            anchors.bottom: parent.bottom
            width: Math.max(actionPillIsland.width, thumbnailIsland.width)
            height: actionPillIsland.height + thumbnailIsland.height + thumbnailIsland.anchors.bottomMargin
            
            Behavior on x {
                enabled: !dragHandler.active && !destroyAnimation.running
                NumberAnimation { duration: 300; easing.type: Easing.OutQuint }
            }

            SequentialAnimation {
                id: destroyAnimation
                property bool left: true
                running: false
                NumberAnimation {
                    target: swipeWrapper
                    property: "x"
                    to: (root.width + 100 * Appearance.effectiveScale) * (destroyAnimation.left ? -1 : 1)
                    duration: 300
                    easing.type: Easing.InQuint
                }
                onFinished: {
                    root.imagePath = "";
                    swipeWrapper.x = 0;
                }
            }

            DragHandler {
                id: dragHandler
                target: swipeWrapper
                xAxis.enabled: true
                yAxis.enabled: false
                onActiveChanged: {
                    if (active) {
                        hideTimer.stop();
                    } else {
                        hideTimer.restart();
                        if (Math.abs(swipeWrapper.x) > 100 * Appearance.effectiveScale) {
                            destroyAnimation.left = swipeWrapper.x < 0;
                            destroyAnimation.running = true;
                        } else {
                            swipeWrapper.x = 0; // Bounce back
                        }
                    }
                }
            }

        // 1. Action Pill Island
        Rectangle {
            id: actionPillIsland
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            
            width: actionRow.implicitWidth + (16 * Appearance.effectiveScale)
            height: 40 * Appearance.effectiveScale + (16 * Appearance.effectiveScale)
            radius: height / 2
            
            color: Appearance.m3colors.m3surfaceContainerHigh

            StyledRectangularShadow {
                target: actionPillIsland
                z: -1
            }

            RowLayout {
                id: actionRow
                anchors.centerIn: parent
                spacing: 8 * Appearance.effectiveScale

                ActionCard {
                    btnIcon: "content_copy"
                    tooltip: I18nService.tr("Copy to clipboard")
                    visible: Config.ready && !Config.options.screenshot.autoCopy
                    onClicked: {
                        Quickshell.execDetached(["bash", "-c", `cat "${root.imagePath}" | wl-copy --type image/png`]);
                        SnackbarService.show(I18nService.tr("Image copied to clipboard"));
                        root.imagePath = "";
                    }
                }

                ActionCard {
                    btnIcon: "save"
                    tooltip: I18nService.tr("Save to Gallery")
                    visible: Config.ready && !Config.options.screenshot.autoSave
                    onClicked: {
                        const rawDir = Config.options.screenshot.savePath;
                        const finalDir = Functions.FileUtils.trimFileProtocol(rawDir);
                        Quickshell.execDetached(["bash", "-c", `mkdir -p "${finalDir}" && savePath="${finalDir}/Screenshot_$(date +%Y-%m-%d-%H-%M-%S).png" && cp "${root.imagePath}" "$savePath"`]);
                        root.imagePath = "";
                    }
                }

                ActionCard {
                    btnIcon: "edit" 
                    tooltip: I18nService.tr("Annotate / Edit")
                    onClicked: {
                        const command = ScreenshotAction.getCommand(0, 0, 0, 0, root.imagePath, 1); 
                        Quickshell.execDetached(command);
                        root.imagePath = "";
                    }
                }

                ActionCard {
                    btnIcon: "center_focus_strong" 
                    tooltip: I18nService.tr("Google Lens")
                    onClicked: {
                        const command = ScreenshotAction.getCommand(0, 0, 0, 0, root.imagePath, 2); 
                        Quickshell.execDetached(command);
                        root.imagePath = "";
                    }
                }

                ActionCard {
                    btnIcon: "delete"
                    tooltip: I18nService.tr("Delete")
                    isError: true
                    onClicked: {
                        root.isDeleting = true;
                        Quickshell.execDetached(["rm", root.imagePath]);
                        Quickshell.execDetached(["wl-copy", "--clear"]);
                        root.imagePath = "";
                        root.isDeleting = false;
                    }
                }
            }
        }

        // 2. Thumbnail Island
        Rectangle {
            id: thumbnailIsland
            anchors.bottom: actionPillIsland.top
            anchors.left: parent.left
            anchors.bottomMargin: 12 * Appearance.effectiveScale
            
            readonly property real islandPadding: 8 * Appearance.effectiveScale
            readonly property real screenAspect: (root.screen && root.screen.height > 0) ? (root.screen.width / root.screen.height) : (16/9)
            readonly property real maxImageDim: 300 * Appearance.effectiveScale
            readonly property real maxImgW: (screenAspect >= 1.0) ? maxImageDim : (maxImageDim * screenAspect)
            readonly property real maxImgH: (screenAspect >= 1.0) ? (maxImageDim / screenAspect) : maxImageDim
            readonly property real imgAspect: (previewImg.implicitWidth > 0) ? (previewImg.implicitWidth / previewImg.implicitHeight) : screenAspect
            readonly property real finalImgW: (imgAspect > (maxImgW / maxImgH)) ? maxImgW : (maxImgH * imgAspect)
            readonly property real finalImgH: finalImgW / imgAspect

            width: finalImgW + (islandPadding * 2)
            height: finalImgH + (islandPadding * 2)

            radius: 16 * Appearance.effectiveScale
            color: Appearance.m3colors.m3surfaceContainerHigh
            clip: true

            Image {
                id: previewImg
                width: parent.finalImgW
                height: parent.finalImgH
                anchors.centerIn: parent
                source: root.displayPath
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                
                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: previewImg.width
                        height: previewImg.height
                        radius: 12 * Appearance.effectiveScale
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Qt.openUrlExternally("file://" + root.imagePath)
                }
            }
        }

        StyledRectangularShadow {
            target: thumbnailIsland
            z: -1
        }
        } // End of swipeWrapper
    }

    // ── Action Card Component ──
    component ActionCard: RippleButton {
        id: actionBtn
        property string btnIcon: ""
        property string tooltip: ""
        property bool isError: false

        implicitWidth: 40 * Appearance.effectiveScale
        implicitHeight: 40 * Appearance.effectiveScale
        buttonRadius: height / 2
        
        colBackground: isError ? Appearance.m3colors.m3errorContainer : Appearance.colors.colPrimary
        colBackgroundHover: isError ? Functions.ColorUtils.applyAlpha(Appearance.m3colors.m3errorContainer, 0.7) : Appearance.colors.colPrimaryHover
        
        MaterialSymbol {
            anchors.centerIn: parent
            text: actionBtn.btnIcon
            iconSize: 20 * Appearance.effectiveScale
            fill: 1
            color: actionBtn.isError ? Appearance.m3colors.m3onErrorContainer : Appearance.colors.colOnPrimary
        }

        StyledToolTip {
            text: actionBtn.tooltip
        }
    }
}
