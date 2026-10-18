import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

/**
 * PowerProfilePanel - Android 16 style details panel for Power Profile.
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
    
    // Bound from parent (QuickSettingsContent.powerProfileMode)
    property string currentMode: "daily"
    // Called when user picks a card — parent updates the file + state
    signal setProfile(string profileId)
    signal dismiss()
    
    readonly property var profiles: [
        {
            id: "daily",
            name: I18nService.tr("Power Saving"),
            icon: "eco",
            description: I18nService.tr("Light usage. Saves battery.")
        },
        {
            id: "balanced",
            name: I18nService.tr("Balanced"),
            icon: "balance",
            description: I18nService.tr("Balanced for productivity.")
        },
        {
            id: "performance",
            name: I18nService.tr("Performance"),
            icon: "local_fire_department",
            description: I18nService.tr("Full power for heavy loads.")
        }
    ]
    
    Component.onCompleted: {
        root.navEngaged = root.inheritedNav;
        for (var i = 0; i < root.profiles.length; i++) {
            if (root.profiles[i].id === root.currentMode) { root.navIndex = i; break; }
        }
        root.forceActiveFocus();
        Qt.callLater(() => root.syncNavRing());
    }
    
    function syncNavRing() {
        if (!root.navEngaged) {
            navRing.visible = false;
            return;
        }
        
        let totalItems = profileRepeater.count + 1; // +1 for Done
        let targetItem = null;
        
        if (root.navIndex < profileRepeater.count) {
            targetItem = profileRepeater.itemAt(root.navIndex);
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
        }
    }
    
    onActiveFocusChanged: {
        if (root.activeFocus) Qt.callLater(() => root.syncNavRing());
        else navRing.visible = false;
    }
    
    focus: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) { root.dismiss(); event.accepted = true; return; }
        
        let totalItems = profileRepeater.count + 1; // +1 for Done
        
        root.navEngaged = true;
        if (event.key === Qt.Key_Up) {
            if (root.navIndex > 0) { root.navIndex--; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            if (root.navIndex < totalItems - 1) { root.navIndex++; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Space) {
            let targetItem = null;
            if (root.navIndex < profileRepeater.count) {
                targetItem = profileRepeater.itemAt(root.navIndex);
            } else {
                targetItem = doneBtn;
            }
            
            if (targetItem) {
                if (targetItem.clicked) targetItem.clicked();
                else if (targetItem.toggleAction) targetItem.toggleAction();
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
            onWheel: (wheel) => wheel.accepted = true
            hoverEnabled: true
            onClicked: root.dismiss()
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
                    text: I18nService.tr("Power Profile")
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
                    text: I18nService.tr("Select a performance mode")
                    font.pixelSize: Appearance.font.pixelSize.small
                    color: Appearance.colors.colSubtext
                }
            }
            
            // List of modes
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 16 * Appearance.effectiveScale
                Layout.preferredHeight: Math.min(profileList.implicitHeight, 300 * Appearance.effectiveScale)
                clip: true
                contentWidth: width
                contentHeight: profileList.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                
                Column {
                    id: profileList
                    width: parent.width
                    spacing: 4 * Appearance.effectiveScale
                    
                    Repeater {
                        id: profileRepeater
                        model: root.profiles
                        
                        delegate: RippleButton {
                            id: profileItem
                            required property var modelData
                            width: profileList.width
                            implicitHeight: 64 * Appearance.effectiveScale
                            buttonRadius: 28 * Appearance.effectiveScale
                            
                            readonly property bool isSelected: root.currentMode === modelData.id
                            colBackground: isSelected ? Appearance.colors.colPrimaryContainer : "transparent"
                            colBackgroundHover: isSelected ? Qt.darker(Appearance.colors.colPrimaryContainer, 1.1) : Appearance.colors.colLayer0Hover
                            
                            onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                            
                            function toggleAction() {
                                root.setProfile(modelData.id)
                            }
                            
                            onClicked: toggleAction()
                            
                            contentItem: RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 24 * Appearance.effectiveScale
                                anchors.rightMargin: 24 * Appearance.effectiveScale
                                spacing: 16 * Appearance.effectiveScale
                                
                                MaterialSymbol {
                                    text: modelData.icon
                                    iconSize: 24 * Appearance.effectiveScale
                                    fill: isSelected ? 1 : 0
                                    color: isSelected ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1
                                }
                                
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2 * Appearance.effectiveScale
                                    
                                    StyledText {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        font.pixelSize: Appearance.font.pixelSize.normal
                                        color: isSelected ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1
                                        elide: Text.ElideRight
                                    }
                                    
                                    StyledText {
                                        Layout.fillWidth: true
                                        text: modelData.description
                                        font.pixelSize: Appearance.font.pixelSize.smaller
                                        color: isSelected ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colSubtext
                                        opacity: isSelected ? 0.9 : 0.8
                                        wrapMode: Text.WordWrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Actions (Done)
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16 * Appearance.effectiveScale
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
