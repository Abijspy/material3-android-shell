import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell

Item {
    id: root
    signal dismiss()
    
    anchors.fill: parent
    property bool isActive: true
    property int navIndex: 0
    property bool inheritedNav: false
    property bool navEngaged: false
    
    Component.onCompleted: {
        Network.rescanWifi();
        root.navEngaged = root.inheritedNav;
        root.forceActiveFocus();
        Qt.callLater(() => root.syncNavRing());
    }
    
    Component.onDestruction: {
        if (Network.wifiScanning) {
            Network.cancelRescanWifi();
        }
    }
    
    onPasswordDialogOpenChanged: {
        if (!passwordDialogOpen && root.activeFocus) {
            Qt.callLater(() => root.syncNavRing());
        }
    }
    
    Connections {
        target: Network
        function onWifiScanningChanged() {
            if (Network.wifiScanning) {
                wifiNavRing.visible = false;
            } else {
                Qt.callLater(() => {
                    let maxListItems = Math.min(wifiList.count, 3);
                    let hasSeeAll = wifiList.count > 3;
                    let totalItems = maxListItems + (hasSeeAll ? 1 : 0) + 1;
                    if (root.navIndex >= totalItems && totalItems > 0) {
                        root.navIndex = totalItems - 1;
                    }
                    if (root.activeFocus) {
                        root.syncNavRing();
                    }
                });
            }
        }
    }
    
    focus: true
    Keys.onPressed: (event) => {
        if (root.passwordDialogOpen) {
            // While the password dialog is open, ESC closes only the dialog,
            // never the panel behind it
            if (event.key === Qt.Key_Escape) { root.closePasswordDialog(); event.accepted = true; }
            return;
        }
        if (event.key === Qt.Key_Escape) { root.dismiss(); event.accepted = true; return; }
        
        let maxListItems = Math.min(wifiList.count, 3);
        let hasSeeAll = wifiList.count > 3;
        let totalItems = maxListItems + (hasSeeAll ? 1 : 0) + 1; // +1 for Done
        
        root.navEngaged = true;
        if (event.key === Qt.Key_Z) {
            Network.toggleWifi();
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
                targetItem = wifiList.itemAtIndex(root.navIndex);
            } else if (hasSeeAll && root.navIndex === maxListItems) {
                targetItem = wifiList.footerItem;
            } else {
                targetItem = doneBtn;
            }
            if (targetItem && targetItem.clicked) targetItem.clicked();
            event.accepted = true;
        }
    }
    
    function syncNavRing() {
        if (root.passwordDialogOpen || !root.navEngaged) {
            wifiNavRing.visible = false;
            return;
        }
        
        let targetItem = null;
        let maxListItems = Math.min(wifiList.count, 3);
        let hasSeeAll = wifiList.count > 3;
        
        if (root.navIndex < maxListItems) {
            targetItem = wifiList.itemAtIndex(root.navIndex);
        } else if (hasSeeAll && root.navIndex === maxListItems) {
            targetItem = wifiList.footerItem;
        } else {
            targetItem = doneBtn;
        }
        
        if (!targetItem) { wifiNavRing.visible = false; return; }
        
        let p = targetItem.mapToItem(dialogBg, 0, 0);
        let newX = p.x - 4 * Appearance.effectiveScale;
        let newY = p.y - 4 * Appearance.effectiveScale;
        let newW = targetItem.width + 8 * Appearance.effectiveScale;
        let newH = targetItem.height + 8 * Appearance.effectiveScale;
        let newR = targetItem.buttonRadius ? targetItem.buttonRadius + 4 * Appearance.effectiveScale : 12 * Appearance.effectiveScale;
        
        if (!wifiNavRing.visible) {
            // Jump directly without animating if becoming visible
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
    
    onActiveFocusChanged: {
        if (root.activeFocus) Qt.callLater(() => root.syncNavRing());
        else wifiNavRing.visible = false;
    }
    
    property bool passwordDialogOpen: false
    property var connectingNetwork: null
    
    function closePasswordDialog() {
        root.passwordDialogOpen = false;
        root.connectingNetwork = null;
        root.forceActiveFocus();
    }
    
    // Dimmed Scrim
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
            onWheel: (wheel) => wheel.accepted = true
        }
    }
    
    // Main Wifi Dialog
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
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        
        StyledRectangularShadow { target: dialogBg; z: -1 }
        
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
                    text: I18nService.tr("Internet")
                    font.pixelSize: Appearance.font.pixelSize.huge
                    font.family: Appearance.font.family.title
                    font.weight: Font.Normal
                    color: Appearance.colors.colOnLayer1
                }
                
                StyledText {
                    id: subtitleText
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    Layout.topMargin: 4 * Appearance.effectiveScale // Add space above subtitle
                    horizontalAlignment: Text.AlignHCenter
                    text: I18nService.tr("Tap a network to connect")
                    font.pixelSize: Appearance.font.pixelSize.small
                    color: Appearance.colors.colSubtext
                }
                
                // Loading Indicator Wrapper
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 12 * Appearance.effectiveScale
                    Layout.preferredWidth: 120 * Appearance.effectiveScale
                    Layout.preferredHeight: 4 * Appearance.effectiveScale
                    
                    StyledIndeterminateProgressBar {
                        anchors.fill: parent
                        running: Network.wifiScanning
                        barColor: Appearance.m3colors.m3primary
                    }
                }
            }
            
            // Wi-Fi Toggle Row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24 * Appearance.effectiveScale // Increased top margin
                Layout.leftMargin: 24 * Appearance.effectiveScale // Matched pill internal padding
                Layout.rightMargin: 24 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale
                
                StyledText {
                    Layout.fillWidth: true
                    text: I18nService.tr("Wi-Fi")
                    font.pixelSize: Appearance.font.pixelSize.large // Increased font size
                    font.family: Appearance.font.family.title
                    color: Appearance.colors.colOnLayer1
                }
                
                AndroidToggle {
                    checked: Network.wifiEnabled
                    onToggled: Network.toggleWifi()
                }
            }
            
            // List of networks
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 8 * Appearance.effectiveScale
                Layout.preferredHeight: Math.min(wifiList.implicitHeight, 300 * Appearance.effectiveScale)
                clip: true
                contentWidth: width
                contentHeight: wifiList.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                
                Column {
                    id: wifiList
                    width: parent.width
                    spacing: 4 * Appearance.effectiveScale
                    
                    property int count: wifiRepeater.count
                    property Item footerItem: seeAllBtn
                    
                    onCountChanged: Qt.callLater(() => root.syncNavRing())
                    function itemAtIndex(idx) { return wifiRepeater.itemAt(idx); }
                    
                    Repeater {
                        id: wifiRepeater
                        model: Network.friendlyWifiNetworks
                        
                        delegate: RippleButton {
                            id: networkItem
                        required property var modelData
                        required property int index
                        
                        visible: index < 3
                        width: wifiList.width
                        implicitHeight: visible ? 64 * Appearance.effectiveScale : 0 // Increased height
                        buttonRadius: 28 * Appearance.effectiveScale // Extra large rounding
                        colBackground: modelData.active ? Appearance.m3colors.m3primaryContainer : "transparent"
                        colBackgroundHover: modelData.active ? Qt.darker(Appearance.m3colors.m3primaryContainer, 1.1) : Appearance.colors.colLayer0Hover
                        
                        onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                        
                        onClicked: {
                            if (modelData.active) {
                                GlobalStates.quickSettingsOpen = false;
                                GlobalStates.settingsPageIndex = 0; // Network settings page
                                GlobalStates.activateSettings();
                                root.dismiss();
                            } else if (modelData.isSaved || !modelData.isSecure) {
                                Network.connectToWifiNetwork(modelData);
                            } else {
                                root.connectingNetwork = modelData;
                                root.passwordDialogOpen = true;
                                pwdInput.text = "";
                                pwdInput.forceActiveFocus();
                            }
                        }

                        contentItem: RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 24 * Appearance.effectiveScale
                            anchors.rightMargin: 24 * Appearance.effectiveScale
                            spacing: 16 * Appearance.effectiveScale

                            NetworkIcon {
                                strength: networkItem.modelData.strength
                                iconSize: 24 * Appearance.effectiveScale
                                color: networkItem.modelData.active ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                StyledText {
                                    text: networkItem.modelData.ssid
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    color: networkItem.modelData.active ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                StyledText {
                                    text: networkItem.modelData.active ? I18nService.tr("Connected") : (networkItem.modelData.isSaved ? I18nService.tr("Saved") : "")
                                    font.pixelSize: Appearance.font.pixelSize.smaller
                                    color: networkItem.modelData.active ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colSubtext
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                }
                            }

                            MaterialSymbol {
                                visible: networkItem.modelData.active || networkItem.modelData.isSecure
                                text: networkItem.modelData.active ? "settings" : "lock"
                                iconSize: networkItem.modelData.active ? 20 * Appearance.effectiveScale : 18 * Appearance.effectiveScale
                                color: networkItem.modelData.active ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colSubtext
                            }
                        }
                        }
                    }
                    
                    RippleButton {
                        id: seeAllBtn
                        width: wifiList.width
                        implicitHeight: wifiList.count > 3 ? 64 * Appearance.effectiveScale : 0 // Matched height
                        visible: wifiList.count > 3
                        buttonRadius: 28 * Appearance.effectiveScale
                        colBackground: "transparent"
                        colBackgroundHover: Appearance.colors.colLayer0Hover
                        
                        onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                        
                        onClicked: {
                            GlobalStates.settingsPageIndex = 0;
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
                                iconSize: 20 * Appearance.effectiveScale // Slightly smaller size for ios arrow to look proportional
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
                }
            }
            
            // Actions (Done)
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16 * Appearance.effectiveScale // Increased top margin
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
    
    // Password Dialog Scrim — covers the whole screen (like the nested alarm
    // time picker) so clicking anywhere outside closes only the password
    // dialog, never the wifi panel behind it
    Rectangle {
        anchors.fill: parent
        radius: Appearance.rounding.panel
        color: Functions.ColorUtils.applyAlpha(Appearance.colors.colLayer0, 0.4)
        opacity: root.passwordDialogOpen ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.closePasswordDialog()
            onWheel: (wheel) => wheel.accepted = true
        }
    }
    
    // Password Dialog
    Rectangle {
        id: pwdDialogBg
        anchors.centerIn: parent
        width: Math.min(parent.width - 24 * Appearance.effectiveScale, 340 * Appearance.effectiveScale)
        height: pwdContentCol.implicitHeight + 48 * Appearance.effectiveScale
        radius: 28 * Appearance.effectiveScale
        color: Appearance.m3colors.m3surfaceContainerHigh
        
        opacity: root.passwordDialogOpen ? 1 : 0
        scale: root.passwordDialogOpen ? 1 : 0.95
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on scale { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        
        StyledRectangularShadow { target: pwdDialogBg; z: -1 }
        
        MouseArea { anchors.fill: parent } // Block clicks
        
        ColumnLayout {
            id: pwdContentCol
            anchors.fill: parent
            anchors.margins: 24 * Appearance.effectiveScale
            spacing: 0
            
            StyledText {
                Layout.fillWidth: true
                text: root.connectingNetwork ? root.connectingNetwork.ssid : ""
                horizontalAlignment: Text.AlignLeft
                font.pixelSize: Appearance.font.pixelSize.huge || 24 * Appearance.effectiveScale
                font.family: Appearance.font.family.title
                font.weight: Font.Normal
                color: Appearance.colors.colOnLayer1
                wrapMode: Text.Wrap
            }
            
            // Password Input
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 24 * Appearance.effectiveScale
                Layout.preferredHeight: 52 * Appearance.effectiveScale
                radius: 8 * Appearance.effectiveScale
                color: "transparent"
                border.width: pwdInput.input.activeFocus ? Math.max(1, 2 * Appearance.effectiveScale) : Math.max(1, 1 * Appearance.effectiveScale)
                border.color: pwdInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline
                
                // Floating Label
                Rectangle {
                    x: 12 * Appearance.effectiveScale
                    y: -8 * Appearance.effectiveScale
                    width: passLabel.width + 8 * Appearance.effectiveScale
                    height: 16 * Appearance.effectiveScale
                    color: Appearance.m3colors.m3surfaceContainerHigh
                    
                    StyledText {
                        id: passLabel
                        anchors.centerIn: parent
                        text: I18nService.tr("Password")
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        font.weight: Font.Medium
                        color: pwdInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline
                    }
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16 * Appearance.effectiveScale
                    anchors.rightMargin: 8 * Appearance.effectiveScale
                    
                    StyledTextInput {
                        id: pwdInput
                        Layout.fillWidth: true
                        echoMode: showPwdBtn.revealed ? TextInput.Normal : TextInput.Password
                        placeholder: ""
                        backgroundColor: "transparent"
                        inputRadius: 0
                        borderInactiveWidth: 0
                        showActiveBorder: false
                        leftMargin: 0
                        rightMargin: 0
                        font.pixelSize: Appearance.font.pixelSize.normal
                        onAccepted: if (text.length > 0) connectBtn.click()
                        Keys.onEscapePressed: root.closePasswordDialog()
                    }
                    
                    RippleButton {
                        id: showPwdBtn
                        property bool revealed: false
                        implicitWidth: 32 * Appearance.effectiveScale
                        implicitHeight: 32 * Appearance.effectiveScale
                        buttonRadius: 16 * Appearance.effectiveScale
                        colBackground: "transparent"
                        onClicked: revealed = !revealed
                        contentItem: MaterialSymbol {
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: showPwdBtn.revealed ? "visibility_off" : "visibility"
                            iconSize: 20 * Appearance.effectiveScale
                            color: Appearance.colors.colSubtext
                        }
                    }
                }
            }
            
            // Actions
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24 * Appearance.effectiveScale
                spacing: 8 * Appearance.effectiveScale
                
                Item { Layout.fillWidth: true }
                
                RippleButton {
                    implicitHeight: 40 * Appearance.effectiveScale
                    buttonRadius: 20 * Appearance.effectiveScale
                    buttonText: I18nService.tr("Cancel")
                    colBackground: "transparent"
                    colBackgroundHover: Appearance.colors.colLayer2Hover
                    colText: Appearance.m3colors.m3primary
                    onClicked: root.closePasswordDialog()
                }
                
                RippleButton {
                    id: connectBtn
                    implicitHeight: 40 * Appearance.effectiveScale
                    buttonRadius: 20 * Appearance.effectiveScale
                    buttonText: I18nService.tr("Connect")
                    colBackground: "transparent"
                    colBackgroundHover: Appearance.colors.colLayer2Hover
                    colText: Appearance.m3colors.m3primary
                    enabled: pwdInput.text.length > 0
                    onClicked: {
                        if (root.connectingNetwork) {
                            Network.connectWithPassword(root.connectingNetwork.ssid, pwdInput.text);
                        }
                        root.passwordDialogOpen = false;
                        root.forceActiveFocus();
                        root.dismiss();
                    }
                }
            }
        }
    }
}
