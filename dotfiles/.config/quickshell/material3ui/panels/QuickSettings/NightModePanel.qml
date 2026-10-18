import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

/**
 * NightModePanel - Android 16 style details panel for Night Mode.
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
    
    function adjustTemp(delta) {
        let current = Config.options.nightMode?.colorTemperature ?? 4000;
        let currentSnapped = Math.round(current / 100) * 100;
        const target = Math.max(tempSlider.from, Math.min(tempSlider.to, currentSnapped + delta));
        Config.options.nightMode.colorTemperature = target;
    }
    
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
        if (root.navIndex === 0) {
            targetItem = tempSlider;
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
        
        root.navEngaged = true;
        
        if (event.key === Qt.Key_Z) {
            Hyprsunset.toggle();
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            if (root.navIndex > 0) { root.navIndex--; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            if (root.navIndex < 1) { root.navIndex++; root.syncNavRing(); }
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            if (root.navIndex === 0) { root.adjustTemp(-100); event.accepted = true; }
        } else if (event.key === Qt.Key_Right) {
            if (root.navIndex === 0) { root.adjustTemp(100); event.accepted = true; }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Space) {
            if (root.navIndex === 1) {
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
                    text: I18nService.tr("Night Mode")
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
                    text: I18nService.tr("Adjust screen color temperature")
                    font.pixelSize: Appearance.font.pixelSize.small
                    color: Appearance.colors.colSubtext
                }
            }
            
            // Toggle Row
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24 * Appearance.effectiveScale
                Layout.leftMargin: 24 * Appearance.effectiveScale
                Layout.rightMargin: 24 * Appearance.effectiveScale
                spacing: 12 * Appearance.effectiveScale
                
                StyledText {
                    Layout.fillWidth: true
                    text: I18nService.tr("Use Night Mode")
                    font.pixelSize: Appearance.font.pixelSize.large
                    font.family: Appearance.font.family.title
                    color: Appearance.colors.colOnLayer1
                }
                
                AndroidToggle {
                    checked: Hyprsunset.active
                    onToggled: Hyprsunset.toggle()
                }
            }
            
            // Color temperature slider
            Column {
                Layout.fillWidth: true
                Layout.topMargin: 16 * Appearance.effectiveScale
                Layout.leftMargin: 12 * Appearance.effectiveScale
                Layout.rightMargin: 12 * Appearance.effectiveScale
                spacing: 6 * Appearance.effectiveScale
                
                RowLayout {
                    width: parent.width
                    StyledText {
                        Layout.fillWidth: true
                        text: I18nService.tr("Color Temperature")
                        font.pixelSize: Appearance.font.pixelSize.small
                        font.weight: Font.Medium
                        color: Appearance.m3colors.m3onSurface
                    }
                    StyledText {
                        text: `${Math.round(tempSlider.value)}K`
                        font.pixelSize: Appearance.font.pixelSize.small
                        color: Appearance.colors.colPrimary
                    }
                }
                
                // Temperature label row (warm ← → cool)
                RowLayout {
                    width: parent.width
                    StyledText {
                        text: I18nService.tr("Warm")
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.m3colors.m3onSurfaceVariant
                    }
                    Item { Layout.fillWidth: true }
                    StyledText {
                        text: I18nService.tr("Cool")
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.m3colors.m3onSurfaceVariant
                    }
                }
                
                StyledSlider {
                    id: tempSlider
                    width: parent.width
                    from: 1200  // warmest
                    to: 6500    // coolest
                    stepSize: 100
                    value: Config.options.nightMode?.colorTemperature ?? 4000
                    configuration: StyledSlider.Configuration.S
                    
                    onYChanged: if (root.navEngaged) Qt.callLater(() => root.syncNavRing())
                    
                    onMoved: {
                        Config.options.nightMode.colorTemperature = value;
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
