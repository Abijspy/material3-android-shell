import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Quickshell
import "../core"
import "../core/functions" as Functions
import "../services"

Rectangle {
    id: root
    
    // Core properties
    property var model: []
    property int currentIndex: 0
    signal itemClicked(int index)
    signal rightActionClicked(int index)
    
    // Layout and modes
    property bool expandable: true
    property bool expanded: true
    property bool showMenuButton: true
    
    // Slots
    property Component topComponent: null
    property Component bottomComponent: null
    
    Layout.fillHeight: true
    Layout.preferredWidth: (expandable && expanded) ? (220 * Appearance.effectiveScale) : (80 * Appearance.effectiveScale)
    Behavior on Layout.preferredWidth { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
    Layout.fillWidth: false
    color: Appearance.colors.colLayer0
    radius: 20 * Appearance.effectiveScale
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 16 * Appearance.effectiveScale
        
        // Internal Menu Button (used if standalone rail)
        RippleButton {
            visible: root.showMenuButton
            Layout.alignment: root.expanded ? Qt.AlignLeft : Qt.AlignHCenter
            Layout.leftMargin: root.expanded ? (8 * Appearance.effectiveScale) : 0
            implicitWidth: 36 * Appearance.effectiveScale
            implicitHeight: 36 * Appearance.effectiveScale
            buttonRadius: 18 * Appearance.effectiveScale
            colBackground: "transparent"
            onClicked: {
                if (root.expandable) root.expanded = !root.expanded;
            }
            
            MaterialSymbol {
                anchors.centerIn: parent
                text: root.expanded ? "menu_open" : "menu"
                iconSize: 22 * Appearance.effectiveScale
                color: Appearance.colors.colOnLayer0
            }
        }
        
        // Top Component Slot
        Loader {
            Layout.fillWidth: true
            active: root.topComponent !== null
            visible: active
            sourceComponent: root.topComponent
        }
        
        // Scrollable Items Area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            Flickable {
                id: sidebarFlickable
                anchors.fill: parent
                contentHeight: navItemsColumn.implicitHeight
                clip: true
                ScrollBar.vertical: StyledScrollBar {}
                
                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: scrollMask
                }
                
                ColumnLayout {
                    id: navItemsColumn
                    width: parent.width
                    spacing: 8 * Appearance.effectiveScale
                    
                    Repeater {
                        model: root.model
                        delegate: MouseArea {
                            id: navBtn
                            visible: modelData.visible !== false
                            
                            readonly property bool isItemEnabled: modelData.enabled !== false
                            opacity: isItemEnabled ? 1.0 : 0.4
                            
                            Layout.fillWidth: true
                            implicitHeight: root.expanded ? 56 * Appearance.effectiveScale : 64 * Appearance.effectiveScale
                            hoverEnabled: true
                            cursorShape: isItemEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            
                            // Emit either stackIndex if available, or just index
                            onClicked: {
                                if (!isItemEnabled) return;
                                let targetIndex = (modelData.stackIndex !== undefined) ? modelData.stackIndex : index;
                                root.itemClicked(targetIndex);
                            }
                            
                            StyledToolTip {
                                text: modelData.tooltip || modelData.name
                                alternativeVisibleCondition: navBtn.containsMouse && (!root.expanded || (modelData.tooltip !== undefined && modelData.tooltip !== ""))
                            }
                            
                            readonly property bool isActive: root.currentIndex === ((modelData.stackIndex !== undefined) ? modelData.stackIndex : index)
                            
                            onIsActiveChanged: {
                                if (isActive) {
                                    Qt.callLater(() => {
                                        if (!sidebarFlickable.contentItem) return;
                                        let pos = navBtn.mapToItem(sidebarFlickable.contentItem, 0, 0);
                                        let itemY = pos.y;
                                        let itemHeight = navBtn.height;
                                        
                                        let safeTop = sidebarFlickable.contentY + 56 * Appearance.effectiveScale;
                                        let safeBottom = sidebarFlickable.contentY + sidebarFlickable.height - 56 * Appearance.effectiveScale;
                                        
                                        if (itemY < safeTop) {
                                            sidebarFlickable.contentY = Math.max(0, itemY - 68 * Appearance.effectiveScale);
                                        } else if (itemY + itemHeight > safeBottom) {
                                            sidebarFlickable.contentY = Math.min(
                                                Math.max(0, sidebarFlickable.contentHeight - sidebarFlickable.height), 
                                                itemY + itemHeight - sidebarFlickable.height + 68 * Appearance.effectiveScale
                                            );
                                        }
                                    });
                                }
                            }
                            
                            // Highlight Background
                            Rectangle {
                                id: itemBackground
                                anchors.left: parent.left
                                anchors.leftMargin: 12 * Appearance.effectiveScale
                                anchors.top: parent.top
                                
                                width: root.expanded ? (52 * Appearance.effectiveScale + itemText.implicitWidth + 24 * Appearance.effectiveScale) : 56 * Appearance.effectiveScale
                                height: root.expanded ? 56 * Appearance.effectiveScale : 32 * Appearance.effectiveScale
                                radius: 100 // Pill shape
                                
                                color: isActive ? Appearance.m3colors.m3secondaryContainer : ((navBtn.containsMouse && isItemEnabled) ? Appearance.colors.colLayer0Hover : "transparent")
                                
                                Behavior on color { ColorAnimation { duration: 150 } }
                                Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                                Behavior on height { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                            }
                            
                            Item {
                                id: iconContainer
                                width: 56 * Appearance.effectiveScale
                                height: root.expanded ? 56 * Appearance.effectiveScale : 32 * Appearance.effectiveScale
                                anchors.left: parent.left
                                anchors.leftMargin: 12 * Appearance.effectiveScale
                                anchors.top: parent.top
                                
                                Behavior on height { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                                
                                MaterialSymbol {
                                    anchors.centerIn: parent
                                    text: modelData.icon
                                    iconSize: 24 * Appearance.effectiveScale
                                    color: isActive ? Appearance.m3colors.m3onSecondaryContainer : Appearance.m3colors.m3onSurfaceVariant
                                    Behavior on color { ColorAnimation { duration: 150 } }
                                }
                            }
                            
                            StyledText {
                                id: itemText
                                text: modelData.name
                                width: root.expanded ? implicitWidth : (root.width - 12 * Appearance.effectiveScale)
                                fontSizeMode: root.expanded ? Text.FixedSize : Text.HorizontalFit
                                minimumPixelSize: 8 * Appearance.effectiveScale
                                font.pixelSize: root.expanded ? Math.round(14 * Appearance.effectiveScale) : Math.round(12 * Appearance.effectiveScale)
                                font.weight: Font.Normal
                                color: isActive ? Appearance.m3colors.m3onSurface : Appearance.m3colors.m3onSurfaceVariant
                                horizontalAlignment: root.expanded ? Text.AlignLeft : Text.AlignHCenter
                                
                                x: root.expanded ? (64 * Appearance.effectiveScale) : ((root.width - width) / 2)
                                y: root.expanded ? ((56 * Appearance.effectiveScale - implicitHeight) / 2) : (36 * Appearance.effectiveScale)
                                
                                Behavior on color { ColorAnimation { duration: 150 } }
                                Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                                Behavior on y { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                            }
                            
                            RippleButton {
                                visible: root.expanded && modelData.rightActionIcon !== undefined
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 12 * Appearance.effectiveScale
                                implicitWidth: 32 * Appearance.effectiveScale
                                implicitHeight: 32 * Appearance.effectiveScale
                                buttonRadius: 16 * Appearance.effectiveScale
                                colBackground: "transparent"
                                colBackgroundHover: Appearance.colors.colLayer1Hover
                                colRipple: Appearance.colors.colLayer2
                                
                                onClicked: {
                                    let targetIndex = (modelData.stackIndex !== undefined) ? modelData.stackIndex : index;
                                    root.rightActionClicked(targetIndex);
                                }
                                
                                MaterialSymbol {
                                    anchors.centerIn: parent
                                    text: modelData.rightActionIcon || ""
                                    iconSize: 20 * Appearance.effectiveScale
                                    color: Appearance.m3colors.m3onSurfaceVariant
                                }
                            }
                        }
                    }
                    
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 24 * Appearance.effectiveScale
                    }
                }
            }
            
            // Scroll Mask (LinearGradient)
            LinearGradient {
                id: scrollMask
                anchors.fill: sidebarFlickable
                visible: false
                start: Qt.point(0, 0)
                end: Qt.point(0, height)
                gradient: Gradient {
                    GradientStop {
                        position: 0.0
                        color: sidebarFlickable.atYBeginning ? "white" : "transparent"
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                    GradientStop {
                        position: Math.min(1.0, (56 * Appearance.effectiveScale) / Math.max(1, scrollMask.height))
                        color: "white"
                    }
                    GradientStop {
                        position: Math.max(0.0, 1.0 - ((56 * Appearance.effectiveScale) / Math.max(1, scrollMask.height)))
                        color: "white"
                    }
                    GradientStop {
                        position: 1.0
                        color: sidebarFlickable.atYEnd ? "white" : "transparent"
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                }
            }
        }
        
        // Bottom Component Slot
        Loader {
            Layout.fillWidth: true
            active: root.bottomComponent !== null
            visible: active
            sourceComponent: root.bottomComponent
        }
    }
}
