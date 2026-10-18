import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell

/**
 * BluetoothPanel - Android 16 style details panel for Bluetooth.
 */
Item {
    id: root
    
    // Fill the QuickSettings popup area
    anchors.fill: parent
    
    // Properties to communicate with the main QS content
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
            wifiNavRing.visible = false;
            return;
        }
        
        let maxListItems = Math.min(deviceList.count, 3);
        let hasSeeAll = deviceList.count > 3;
        let hasPair = BluetoothStatus.enabled;
        
        let targetItem = null;
        if (root.navIndex < maxListItems) {
            targetItem = deviceList.itemAtIndex(root.navIndex);
        } else if (hasSeeAll && root.navIndex === maxListItems) {
            targetItem = deviceList.footerItem ? deviceList.footerItem.children[0] : null;
        } else if (hasPair && ((hasSeeAll && root.navIndex === maxListItems + 1) || (!hasSeeAll && root.navIndex === maxListItems))) {
            targetItem = deviceList.footerItem ? deviceList.footerItem.children[1] : null;
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
            
            if (!wifiNavRing.visible) {
                wifiNavRing.enableAnimation = false;
                wifiNavRing.x = newX;
                wifiNavRing.y = newY;
                wifiNavRing.width = newW;
                wifiNavRing.height = newH;
                wifiNavRing.radius = newR;
                Qt.callLater(() => { wifiNavRing.enableAnimation = true; });
            } else {
                wifiNavRing.enableAnimation = true;
                wifiNavRing.x = newX;
                wifiNavRing.y = newY;
                wifiNavRing.width = newW;
                wifiNavRing.height = newH;
                wifiNavRing.radius = newR;
            }
            
            wifiNavRing.visible = root.activeFocus && root.navEngaged;
        }
    }
    
    onActiveFocusChanged: {
        if (root.activeFocus && root.navEngaged) {
            root.syncNavRing();
        } else {
            wifiNavRing.visible = false;
        }
    }

    focus: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) { root.dismiss(); event.accepted = true; return; }
        
        let maxListItems = Math.min(deviceList.count, 3);
        let hasSeeAll = deviceList.count > 3;
        let hasPair = BluetoothStatus.enabled;
        let totalItems = maxListItems + (hasSeeAll ? 1 : 0) + (hasPair ? 1 : 0) + 1; // +1 for Done
        
        root.navEngaged = true;
        if (event.key === Qt.Key_Z) {
            BluetoothStatus.toggle();
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            if (root.navIndex > 0) { root.navIndex--; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            if (root.navIndex < totalItems - 1) { root.navIndex++; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Space) {
            let targetItem = null;
            if (root.navIndex < maxListItems) {
                targetItem = deviceList.itemAtIndex(root.navIndex);
            } else if (hasSeeAll && root.navIndex === maxListItems) {
                targetItem = deviceList.footerItem ? deviceList.footerItem.children[0] : null;
            } else if (hasPair && ((hasSeeAll && root.navIndex === maxListItems + 1) || (!hasSeeAll && root.navIndex === maxListItems))) {
                targetItem = deviceList.footerItem ? deviceList.footerItem.children[1] : null;
            } else {
                targetItem = doneBtn;
            }
            if (targetItem && targetItem.clicked) targetItem.clicked();
            event.accepted = true;
        }
    }
    
    Connections {
        target: BluetoothStatus
        function onConnectedDevicesChanged() { if (root.navEngaged) Qt.callLater(() => root.syncNavRing()) }
        function onPairedButNotConnectedDevicesChanged() { if (root.navEngaged) Qt.callLater(() => root.syncNavRing()) }
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
    
    // --- Main Panel Background ---
    Rectangle {
        id: dialogBg
        anchors.centerIn: parent
        width: Math.min(parent.width - 24 * Appearance.effectiveScale, 380 * Appearance.effectiveScale)
        height: Math.min(parent.height - 48 * Appearance.effectiveScale, contentCol.implicitHeight + 48 * Appearance.effectiveScale)
        radius: 28 * Appearance.effectiveScale
        color: Appearance.m3colors.m3surfaceContainerHigh
        clip: true
        
        opacity: root.isActive ? 1 : 0
        scale: root.isActive ? 1 : 0.95
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
        
        Rectangle {
            id: wifiNavRing
            property bool enableAnimation: false
            color: "transparent"
            border.color: Appearance.m3colors.m3primary
            border.width: Math.max(1, 2 * Appearance.effectiveScale)
            opacity: 0.9
            z: 99
            visible: false
            Behavior on x { enabled: wifiNavRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: wifiNavRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: wifiNavRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on height { enabled: wifiNavRing.enableAnimation; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }
        
        MouseArea { anchors.fill: parent } // Block clicks
        
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
                    text: I18nService.tr("Bluetooth")
                    font.pixelSize: Appearance.font.pixelSize.huge
                    font.family: Appearance.font.family.title
                    font.weight: Font.Normal
                    color: Appearance.colors.colOnLayer1
                }
                
                StyledText {
                    id: subtitleText
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    Layout.topMargin: 4 * Appearance.effectiveScale
                    horizontalAlignment: Text.AlignHCenter
                    text: I18nService.tr("Tap to connect or disconnect a device")
                    font.pixelSize: Appearance.font.pixelSize.small
                    color: Appearance.colors.colSubtext
                }
            }
            
            // Bluetooth Toggle Row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24 * Appearance.effectiveScale
                Layout.leftMargin: 24 * Appearance.effectiveScale
                Layout.rightMargin: 24 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale
                
                StyledText {
                    Layout.fillWidth: true
                    text: I18nService.tr("Use Bluetooth")
                    font.pixelSize: Appearance.font.pixelSize.large
                    font.family: Appearance.font.family.title
                    color: Appearance.colors.colOnLayer1
                }
                
                AndroidToggle {
                    checked: BluetoothStatus.enabled
                    onToggled: BluetoothStatus.toggle()
                }
            }
            
            // List of devices
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 8 * Appearance.effectiveScale
                Layout.preferredHeight: Math.min(deviceList.implicitHeight, 300 * Appearance.effectiveScale)
                clip: true
                contentWidth: width
                contentHeight: deviceList.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                
                Column {
                    id: deviceList
                    width: parent.width
                    spacing: 4 * Appearance.effectiveScale
                    
                    property var _model: BluetoothStatus.enabled ? [...BluetoothStatus.connectedDevices, ...BluetoothStatus.pairedButNotConnectedDevices] : []
                    property int count: _model.length
                    property Item footerItem: footerCol
                    
                    onCountChanged: Qt.callLater(() => root.syncNavRing())
                    function itemAtIndex(idx) { return deviceRepeater.itemAt(idx); }
                    
                    Repeater {
                        id: deviceRepeater
                        model: deviceList._model
                        
                        delegate: RippleButton {
                            id: networkItem
                        required property var modelData
                        required property int index
                        
                        visible: index < 3
                        width: deviceList.width
                        implicitHeight: visible ? 64 * Appearance.effectiveScale : 0
                        buttonRadius: 28 * Appearance.effectiveScale
                        colBackground: modelData.connected ? Appearance.m3colors.m3primaryContainer : "transparent"
                        colBackgroundHover: modelData.connected ? Qt.darker(Appearance.m3colors.m3primaryContainer, 1.1) : Appearance.colors.colLayer0Hover
                        
                        onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                        
                        onClicked: {
                            if (modelData.connected) {
                                modelData.disconnect();
                            } else {
                                BluetoothStatus.pairAndTrust(modelData);
                            }
                        }

                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 24 * Appearance.effectiveScale
                            anchors.rightMargin: 24 * Appearance.effectiveScale
                            spacing: 16 * Appearance.effectiveScale

                            MaterialSymbol {
                                text: {
                                    const type = networkItem.modelData.deviceType;
                                    if (type === "phone") return "smartphone"
                                    if (type === "computer") return "computer"
                                    if (type === "audio-card") return "headset"
                                    return "bluetooth"
                                }
                                iconSize: 24 * Appearance.effectiveScale
                                color: networkItem.modelData.connected ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                StyledText {
                                    text: networkItem.modelData.name || networkItem.modelData.address
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    color: networkItem.modelData.connected ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                StyledText {
                                    readonly property var _d: networkItem.modelData
                                    text: {
                                        if (_d.connected) return I18nService.tr("Connected") + (_d.batteryAvailable ? " · " + Math.round(_d.battery * 100) + "%" : "")
                                        if (BluetoothStatus.pairingAddress === _d.address) return I18nService.tr("Connecting...")
                                        if (_d.pairing) return I18nService.tr("Pairing...")
                                        if (_d.paired || _d.trusted) return I18nService.tr("Saved")
                                        return I18nService.tr("Available")
                                    }
                                    font.pixelSize: Appearance.font.pixelSize.smaller
                                    color: networkItem.modelData.connected ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colSubtext
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                }
                            }
                        }
                        }
                    }
                    
                    Column {
                        id: footerCol
                        width: deviceList.width
                        spacing: (seeAllBtn.visible && pairBtn.visible) ? 4 * Appearance.effectiveScale : 0
                        
                        RippleButton {
                            id: seeAllBtn
                            visible: deviceList.count > 3
                            width: parent.width
                            implicitHeight: visible ? 56 * Appearance.effectiveScale : 0
                            buttonRadius: 28 * Appearance.effectiveScale
                            colBackground: "transparent"
                            colBackgroundHover: Appearance.colors.colLayer0Hover
                            
                            onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                            
                            onClicked: {
                                GlobalStates.quickSettingsOpen = false;
                                GlobalStates.settingsPageIndex = 1; // Bluetooth settings page
                                GlobalStates.activateSettings();
                                root.dismiss();
                            }
                            
                            contentItem: RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 24 * Appearance.effectiveScale
                                anchors.rightMargin: 24 * Appearance.effectiveScale
                                spacing: 16 * Appearance.effectiveScale
                                
                                MaterialSymbol {
                                    text: "arrow_forward_ios"
                                    iconSize: 20 * Appearance.effectiveScale
                                    color: Appearance.colors.colOnLayer1
                                }
                                
                                StyledText {
                                    Layout.fillWidth: true
                                    text: I18nService.tr("See all")
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    color: Appearance.colors.colOnLayer1
                                }
                            }
                        }
                        
                        RippleButton {
                            id: pairBtn
                            visible: BluetoothStatus.enabled
                            width: parent.width
                            implicitHeight: visible ? 56 * Appearance.effectiveScale : 0
                            buttonRadius: 28 * Appearance.effectiveScale
                            colBackground: "transparent"
                            colBackgroundHover: Appearance.colors.colLayer0Hover
                            
                            onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                            
                            onClicked: {
                                GlobalStates.quickSettingsOpen = false;
                                GlobalStates.settingsBluetoothPairMode = true;
                                GlobalStates.settingsPageIndex = 1;
                                GlobalStates.activateSettings();
                                root.dismiss();
                            }
                            
                            contentItem: RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 24 * Appearance.effectiveScale
                                anchors.rightMargin: 24 * Appearance.effectiveScale
                                spacing: 16 * Appearance.effectiveScale
                                
                                MaterialSymbol {
                                    text: "add"
                                    iconSize: 24 * Appearance.effectiveScale
                                    color: Appearance.colors.colOnLayer1
                                }
                                
                                StyledText {
                                    Layout.fillWidth: true
                                    text: I18nService.tr("Pair new device")
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    color: Appearance.colors.colOnLayer1
                                }
                            }
                        }
                    }
                }
            }
            
            Item { Layout.fillHeight: true } // Spacer
            
            // Bottom Action Row
            RowLayout {
                Layout.fillWidth: true
                
                Item { Layout.fillWidth: true } // Push Done to right
                
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
