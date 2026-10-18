import QtQuick
import QtQuick.Layouts
import Quickshell
import "../core"
import "../core/functions" as Functions

Item {
    id: root

    property int currentTab: 0
    property var tabModel: []
    property bool showIcon: true
    property bool iconOnTop: true
    property real cornerRadius: Appearance.rounding.normal
    property var indicatorWidths: []
    property bool scrollNavigation: true

    // Fired when a tab is clicked
    signal tabClicked(int index)

    readonly property int tabCount: root.tabModel.length

    Layout.fillWidth: true
    height: (showIcon && iconOnTop ? 64 : 48) * Appearance.effectiveScale

    onCurrentTabChanged: {
        if (tabHighlight) {
            tabHighlight.idx1 = currentTab
            Qt.callLater(() => { tabHighlight.idx2 = currentTab })
        }
    }

    // Bottom border for the entire tab bar
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1 * Appearance.effectiveScale
        color: Appearance.m3colors.m3outlineVariant
        opacity: 0.5
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.tabModel
            
            delegate: RippleButton {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                buttonRadius: 0
                topLeftRadius: index === 0 ? root.cornerRadius : 0
                topRightRadius: index === root.tabCount - 1 ? root.cornerRadius : 0
                
                colBackground: "transparent"
                colBackgroundHover: Appearance.colors.colLayer0Hover
                colRipple: Appearance.colors.colLayer0Active
                onClicked: {
                    root.tabClicked(index);
                }
                
                readonly property bool isActive: root.currentTab === index

                // Container for icon and text
                Item {
                    anchors.fill: parent
                    
                    GridLayout {
                        anchors.centerIn: parent
                        columns: root.iconOnTop ? 1 : 2
                        rowSpacing: root.showIcon && root.iconOnTop ? 6 * Appearance.effectiveScale : 0
                        columnSpacing: root.showIcon && !root.iconOnTop ? 8 * Appearance.effectiveScale : 0
                        
                        MaterialSymbol {
                            visible: root.showIcon
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                            text: modelData.icon ? modelData.icon : ""
                            iconSize: (root.iconOnTop ? 24 : 18) * Appearance.effectiveScale
                            color: isActive ? Appearance.m3colors.m3primary : Appearance.m3colors.m3onSurfaceVariant
                            Behavior on color { ColorAnimation { duration: 200 } }
                        }
                        
                        StyledText {
                            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                            text: modelData.name
                            font.pixelSize: (root.showIcon && root.iconOnTop ? 12 : 14) * Appearance.effectiveScale
                            font.weight: Font.Medium
                            color: isActive ? Appearance.m3colors.m3primary : Appearance.m3colors.m3onSurfaceVariant
                            Behavior on color { ColorAnimation { duration: 200 } }
                            
                            onImplicitWidthChanged: {
                                if (implicitWidth > 0) {
                                    let arr = root.indicatorWidths.slice();
                                    let iconW = root.showIcon ? (root.iconOnTop ? 24 : 18) * Appearance.effectiveScale : 0;
                                    let contentW;
                                    if (root.showIcon && root.iconOnTop) {
                                        contentW = Math.max(iconW, implicitWidth);
                                    } else if (root.showIcon) {
                                        contentW = iconW + 8 * Appearance.effectiveScale + implicitWidth;
                                    } else {
                                        contentW = implicitWidth;
                                    }
                                    // M3: 2dp inset each side, minimum 24dp
                                    let inset = 2 * Appearance.effectiveScale;
                                    arr[index] = Math.max(24 * Appearance.effectiveScale, contentW - inset * 2);
                                    root.indicatorWidths = arr;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Scroll wheel / trackpad over the tab bar switches tabs
    WheelHandler {
        enabled: root.scrollNavigation
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const dy = event.angleDelta.y;
            const dx = event.angleDelta.x;
            let dir = 0;
            if (Math.abs(dx) > Math.abs(dy))
                dir = dx > 0 ? 1 : -1;
            else if (dy !== 0)
                dir = dy > 0 ? -1 : 1;

            if (dir === 0)
                return;

            const next = Math.max(0, Math.min(root.tabCount - 1, root.currentTab + dir));
            if (next !== root.currentTab) {
                root.tabClicked(next);
            }
        }
    }

    // Animated stretch-highlight pill
    Rectangle {
        id: tabHighlight
        anchors.bottom: parent.bottom
        height: 3 * Appearance.effectiveScale
        
        property int idx1: root.currentTab
        property int idx2: root.currentTab
        
        function getLeftForIndex(i) {
            if (root.tabCount === 0) return 0;
            let w = root.indicatorWidths[i] || 48;
            return (i + 0.5) * (parent.width / root.tabCount) - w / 2
        }
        function getRightForIndex(i) {
            if (root.tabCount === 0) return 0;
            let w = root.indicatorWidths[i] || 48;
            return (i + 0.5) * (parent.width / root.tabCount) + w / 2
        }
        
        property real animLeft1: getLeftForIndex(idx1)
        property real animRight1: getRightForIndex(idx1)
        property real animLeft2: getLeftForIndex(idx2)
        property real animRight2: getRightForIndex(idx2)
        
        x: Math.min(animLeft1, animLeft2)
        width: Math.max(animRight1, animRight2) - x
        
        topLeftRadius: height / 2
        topRightRadius: height / 2
        
        color: Appearance.m3colors.m3primary
        
        Behavior on animLeft1 { NumberAnimation { duration: 300; easing.type: Easing.OutQuart } }
        Behavior on animRight1 { NumberAnimation { duration: 300; easing.type: Easing.OutQuart } }
        Behavior on animLeft2 { NumberAnimation { duration: 500; easing.type: Easing.OutQuart } }
        Behavior on animRight2 { NumberAnimation { duration: 500; easing.type: Easing.OutQuart } }
    }
}
