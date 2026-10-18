import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell
import Quickshell.Widgets

/**
 * Functional Audio device selection panel.
 * Shared between Audio Output and Audio Input — configured via `isSink`.
 * Uses real Pipewire data from the Audio service.
 */
Item {
    id: root
    
    anchors.fill: parent
    
    property string panelTitle: "Audio Output"
    property string panelIcon: "volume_up"
    property bool isSink: true
    
    property bool isActive: true
    property bool navEngaged: false
    property bool inheritedNav: false
    property int navIndex: 0
    
    signal dismiss()
    
    Component.onCompleted: {
        root.navEngaged = root.inheritedNav;
        root.forceActiveFocus();
        Qt.callLater(() => root.syncNavRing());
    }
    
    function syncNavRing() {
        if (!root.navEngaged) {
            navRing.visible = false;
            return;
        }
        
        let targetItem = null;
        let deviceCount = deviceRepeater.count;
        let streamCount = streamRepeater.count;
        
        if (root.navIndex === 0) {
            targetItem = toggleRow;
        } else if (root.navIndex > 0 && root.navIndex <= deviceCount) {
            targetItem = deviceRepeater.itemAt(root.navIndex - 1);
        } else if (root.navIndex > deviceCount && root.navIndex <= deviceCount + streamCount) {
            targetItem = streamRepeater.itemAt(root.navIndex - deviceCount - 1);
        } else {
            targetItem = doneBtn;
        }
        
        if (targetItem) {
            let p = targetItem.mapToItem(dialogBg, 0, 0);
            let newX = p.x - 4 * Appearance.effectiveScale;
            let newY = p.y - 4 * Appearance.effectiveScale;
            let newW = targetItem.width + 8 * Appearance.effectiveScale;
            let newH = targetItem.height + 8 * Appearance.effectiveScale;
            let newR = targetItem.buttonRadius ? targetItem.buttonRadius + 4 * Appearance.effectiveScale : 12 * Appearance.effectiveScale;
            
            if (!navRing.visible) {
                navRing.enableAnimation = false;
                navRing.x = newX;
                navRing.y = newY;
                navRing.width = newW;
                navRing.height = newH;
                navRing.radius = newR;
                Qt.callLater(() => { navRing.enableAnimation = true; });
            } else {
                navRing.enableAnimation = true;
                navRing.x = newX;
                navRing.y = newY;
                navRing.width = newW;
                navRing.height = newH;
                navRing.radius = newR;
            }
            
            navRing.visible = root.activeFocus && root.navEngaged;
            
            // Scroll into view if it's a list item
            if (root.navIndex > 0 && root.navIndex <= deviceCount + streamCount) {
                const cpos = targetItem.mapToItem(audioFlick.contentItem, 0, 0);
                if (cpos.y < audioFlick.contentY + 4 * Appearance.effectiveScale) {
                    audioFlick.contentY = Math.max(0, cpos.y - 4 * Appearance.effectiveScale);
                } else if (cpos.y + targetItem.height > audioFlick.contentY + audioFlick.height - 4 * Appearance.effectiveScale) {
                    audioFlick.contentY = cpos.y + targetItem.height - audioFlick.height + 4 * Appearance.effectiveScale;
                }
            }
        }
    }
    
    onActiveFocusChanged: {
        if (root.activeFocus) Qt.callLater(() => root.syncNavRing());
        else navRing.visible = false;
    }
    
    focus: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) { root.dismiss(); event.accepted = true; return; }
        
        let deviceCount = deviceRepeater.count;
        let streamCount = streamRepeater.count;
        let totalItems = 1 + deviceCount + streamCount + 1;
        
        root.navEngaged = true;
        if (event.key === Qt.Key_Z) {
            if (root.isSink) Audio.toggleMute();
            else Audio.toggleMicMute();
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            if (root.navIndex > 0) { root.navIndex--; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            if (root.navIndex < totalItems - 1) { root.navIndex++; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            if (root.navIndex > deviceCount && root.navIndex <= deviceCount + streamCount) {
                const it = streamRepeater.itemAt(root.navIndex - deviceCount - 1);
                if (it && it.modelData) {
                    Audio.setNodeVolume(it.modelData, Math.max(0.0, it.modelData.audio.volume - 0.05));
                }
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Right) {
            if (root.navIndex > deviceCount && root.navIndex <= deviceCount + streamCount) {
                const it = streamRepeater.itemAt(root.navIndex - deviceCount - 1);
                if (it && it.modelData) {
                    Audio.setNodeVolume(it.modelData, Math.min(1.0, it.modelData.audio.volume + 0.05));
                }
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Space) {
            if (root.navIndex === 0) {
                if (root.isSink) Audio.toggleMute();
                else Audio.toggleMicMute();
            } else if (root.navIndex > 0 && root.navIndex <= deviceCount) {
                const it = deviceRepeater.itemAt(root.navIndex - 1);
                if (it && it.clicked) it.clicked();
            } else if (root.navIndex > deviceCount && root.navIndex <= deviceCount + streamCount) {
                // Return/Space does nothing on a volume slider, maybe toggle its own mute if supported?
                // For now, no action.
            } else {
                doneBtn.clicked();
            }
            event.accepted = true;
        }
    }
    
    // --- Scrim (Click outside to close) ---
    Rectangle {
        anchors.fill: parent
        color: Functions.ColorUtils.applyAlpha(Appearance.colors.colLayer0, 0.6)
        radius: Appearance.rounding.panel
        opacity: root.isActive ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onClicked: root.dismiss()
            onWheel: (event) => { event.accepted = true; } // Block scroll
        }
    }
    
    // --- Dialog Container ---
    Rectangle {
        id: dialogBg
        width: Math.min(parent.width - 24 * Appearance.effectiveScale, 380 * Appearance.effectiveScale)
        height: Math.min(parent.height - 48 * Appearance.effectiveScale, contentCol.implicitHeight + 48 * Appearance.effectiveScale)
        anchors.centerIn: parent
        radius: 28 * Appearance.effectiveScale
        color: Appearance.m3colors.m3surfaceContainerHigh
        clip: true
        
        opacity: root.isActive ? 1 : 0
        scale: root.isActive ? 1 : 0.95
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        
        StyledRectangularShadow { target: dialogBg; z: -1 }
        
        // Trap clicks inside dialog
        MouseArea {
            anchors.fill: parent
            onWheel: (wheel) => wheel.accepted = true
            hoverEnabled: true
        }
        
        // --- Keyboard Focus Ring ---
        Rectangle {
            id: navRing
            property bool enableAnimation: false
            visible: false
            z: 999
            color: "transparent"
            border.width: Math.max(1, 2 * Appearance.effectiveScale)
            border.color: Appearance.m3colors.m3primary
            opacity: 0.9
            radius: 12 * Appearance.effectiveScale
            
            Behavior on x { enabled: navRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: navRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: navRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on height { enabled: navRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on radius { enabled: navRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }
        
        ColumnLayout {
            id: contentCol
            anchors.fill: parent
            anchors.margins: 24 * Appearance.effectiveScale
            spacing: 16 * Appearance.effectiveScale
            
            // Header: Title
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                
                StyledText {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: I18nService.tr(root.panelTitle)
                    font.pixelSize: Appearance.font.pixelSize.huge
                    font.family: Appearance.font.family.title
                    font.weight: Font.Normal
                    color: Appearance.colors.colOnLayer1
                }
            }
            
            // Toggle Row
            RowLayout {
                id: toggleRow
                Layout.fillWidth: true
                Layout.topMargin: 16 * Appearance.effectiveScale
                Layout.leftMargin: 24 * Appearance.effectiveScale
                Layout.rightMargin: 24 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale
                
                StyledText {
                    Layout.fillWidth: true
                    text: root.isSink ? I18nService.tr("Audio Output") : I18nService.tr("Microphone")
                    font.pixelSize: Appearance.font.pixelSize.large
                    font.family: Appearance.font.family.title
                    color: Appearance.colors.colOnLayer1
                }
                
                AndroidToggle {
                    checked: root.isSink ? !Audio.muted : !Audio.microphoneMuted
                    onToggled: {
                        if (root.isSink) Audio.toggleMute();
                        else Audio.toggleMicMute();
                    }
                }
            }
            
            // Separator
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8 * Appearance.effectiveScale
                Layout.bottomMargin: 8 * Appearance.effectiveScale
                height: 1
                color: Appearance.m3colors.m3outlineVariant
            }
            
            // Scrollable Content
            Flickable {
                id: audioFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: audioContentCol.implicitHeight
                clip: true
                contentWidth: width
                contentHeight: audioContentCol.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                
                ColumnLayout {
                    id: audioContentCol
                    width: audioFlick.width
                    spacing: 20 * Appearance.effectiveScale
                    
                    // Section: Devices
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8 * Appearance.effectiveScale
                        
                        StyledText {
                            text: I18nService.tr("Devices")
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.Medium
                            color: Appearance.m3colors.m3outline
                            Layout.leftMargin: 4 * Appearance.effectiveScale
                        }
                        
                        Column {
                            Layout.fillWidth: true
                            spacing: 2 * Appearance.effectiveScale
                            Repeater {
                                id: deviceRepeater
                                model: root.isSink ? Audio.outputDevices : Audio.inputDevices
                                delegate: RippleButton {
                                    id: audioDeviceItem
                                    required property var modelData
                                    width: audioFlick.width
                                    implicitHeight: 52 * Appearance.effectiveScale
                                    buttonRadius: Appearance.rounding.small
                                    
                                    readonly property bool isActive: root.isSink 
                                        ? (Audio.sink === modelData)
                                        : (Audio.source === modelData)
                                        
                                    colBackground: audioDeviceItem.isActive ? Functions.ColorUtils.transparentize(Appearance.colors.colPrimary, 0.85) : "transparent"
                                    colBackgroundHover: audioDeviceItem.isActive ? Functions.ColorUtils.transparentize(Appearance.colors.colPrimary, 0.75) : Appearance.colors.colLayer2
                                    
                                    onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                                    
                                    onClicked: {
                                        if (root.isSink) Audio.setDefaultSink(audioDeviceItem.modelData);
                                        else Audio.setDefaultSource(audioDeviceItem.modelData);
                                    }
                                    
                                    contentItem: RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12 * Appearance.effectiveScale
                                        anchors.rightMargin: 12 * Appearance.effectiveScale
                                        spacing: 12 * Appearance.effectiveScale
                                        
                                        MaterialSymbol {
                                            text: {
                                                if (!root.isSink) return "mic"
                                                const desc = audioDeviceItem.modelData.description.toLowerCase();
                                                if (desc.includes("headset") || desc.includes("headphone")) return "headphones"
                                                if (desc.includes("hdmi") || desc.includes("tv")) return "tv"
                                                return "speaker"
                                            }
                                            iconSize: 20 * Appearance.effectiveScale
                                            fill: audioDeviceItem.isActive ? 1 : 0
                                            color: audioDeviceItem.isActive ? Appearance.colors.colPrimary : Appearance.m3colors.m3onSurfaceVariant
                                        }
                                        
                                        StyledText {
                                            Layout.fillWidth: true
                                            text: Audio.friendlyDeviceName(audioDeviceItem.modelData)
                                            font.pixelSize: Appearance.font.pixelSize.small
                                            color: Appearance.m3colors.m3onSurface
                                            elide: Text.ElideRight
                                        }
                                        
                                        MaterialSymbol {
                                            visible: audioDeviceItem.isActive
                                            text: "check_circle"
                                            iconSize: 18 * Appearance.effectiveScale
                                            fill: 1
                                            color: Appearance.colors.colPrimary
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    // Section: Applications
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12 * Appearance.effectiveScale
                        visible: (root.isSink ? Audio.streamNodes.length : Audio.micStreamNodes.length) > 0
                        
                        StyledText {
                            text: I18nService.tr("Applications")
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.Medium
                            color: Appearance.m3colors.m3outline
                            Layout.leftMargin: 4 * Appearance.effectiveScale
                        }
                        
                        Column {
                            Layout.fillWidth: true
                            spacing: 16 * Appearance.effectiveScale
                            Repeater {
                                id: streamRepeater
                                model: root.isSink ? Audio.streamNodes : Audio.micStreamNodes
                                delegate: ColumnLayout {
                                    id: streamItem
                                    required property var modelData
                                    width: audioFlick.width
                                    spacing: 4 * Appearance.effectiveScale
                                    
                                    onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                                    
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 12 * Appearance.effectiveScale
                                        Layout.rightMargin: 12 * Appearance.effectiveScale
                                        spacing: 8 * Appearance.effectiveScale
                                        
                                        Item {
                                            width: 20 * Appearance.effectiveScale
                                            height: 20 * Appearance.effectiveScale
                                            
                                            IconImage {
                                                id: appIcon
                                                anchors.fill: parent
                                                source: Quickshell.iconPath(Audio.appNodeIconName(streamItem.modelData), "image-missing")
                                                visible: status === Image.Ready
                                            }
                                            
                                            MaterialSymbol {
                                                anchors.centerIn: parent
                                                text: "settings_input_component"
                                                iconSize: 18 * Appearance.effectiveScale
                                                color: Appearance.m3colors.m3onSurfaceVariant
                                                visible: appIcon.status !== Image.Ready
                                            }
                                        }
                                        
                                        StyledText {
                                            text: Audio.appNodeDisplayName(streamItem.modelData)
                                            font.pixelSize: Appearance.font.pixelSize.small
                                            font.weight: Font.Medium
                                            color: Appearance.m3colors.m3onSurface
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }
                                        StyledText {
                                            text: Math.round(streamItem.modelData.audio.volume * 100) + "%"
                                            font.pixelSize: Appearance.font.pixelSize.smaller
                                            color: Appearance.colors.colSubtext
                                        }
                                    }
                                    
                                    StyledSlider {
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 12 * Appearance.effectiveScale
                                        Layout.rightMargin: 12 * Appearance.effectiveScale
                                        configuration: StyledSlider.Configuration.S
                                        value: streamItem.modelData.audio.volume
                                        stopIndicatorValues: []
                                        onMoved: Audio.setNodeVolume(streamItem.modelData, value)
                                    }
                                }
                            }
                        }
                    }
                    
                    // Bottom spacer for better scrolling
                    Item { Layout.preferredHeight: 12 * Appearance.effectiveScale }
                }
            }
            
            // Actions (Done)
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8 * Appearance.effectiveScale
                spacing: 8 * Appearance.effectiveScale
                
                Item { Layout.fillWidth: true }
                
                RippleButton {
                    id: doneBtn
                    implicitHeight: 40 * Appearance.effectiveScale
                    leftPadding: 24 * Appearance.effectiveScale
                    rightPadding: 24 * Appearance.effectiveScale
                    buttonRadius: 20 * Appearance.effectiveScale
                    buttonText: I18nService.tr("Done")
                    colBackground: Appearance.colors.colPrimary
                    colBackgroundHover: Qt.darker(Appearance.colors.colPrimary, 1.1)
                    colText: Appearance.colors.colOnPrimary
                    font.weight: Font.DemiBold
                    font.pixelSize: Appearance.font.pixelSize.small
                    
                    onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                    
                    onClicked: root.dismiss()
                }
            }
        }
    }
}
