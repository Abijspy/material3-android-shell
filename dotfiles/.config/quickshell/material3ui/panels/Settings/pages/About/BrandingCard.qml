import "../../../../core"
import "../../../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Widgets

Rectangle {
    id: cardRoot
    property string title
    property string name
    property string subText
    property color accentColor
    property string icon
    property string logoSource: ""
    property bool isSystemIcon: false

    implicitHeight: 180 * Appearance.effectiveScale
    radius: 20 * Appearance.effectiveScale
    color: Appearance.m3colors.m3surfaceContainerHigh
    
    layer.enabled: true
    layer.effect: OpacityMask {
        maskSource: Rectangle {
            width: cardRoot.width
            height: cardRoot.height
            radius: cardRoot.radius
        }
    }

    property string shapeName: ""

    // Decorative background (Material 3 Expressive Shape)
    Item {
        anchors.fill: parent
        clip: true

        MaterialShape {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: -cardRoot.width * 0.28
            anchors.topMargin: -cardRoot.width * 0.22
            width: cardRoot.width * 0.85
            height: width
            shapeString: cardRoot.shapeName !== "" ? cardRoot.shapeName : "SoftBurst"
            color: cardRoot.accentColor
            opacity: 0.12
            rotation: -12
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20 * Appearance.effectiveScale
        spacing: 4 * Appearance.effectiveScale

        StyledText {
            text: title
            font.pixelSize: Appearance.font.pixelSize.small
            color: Appearance.colors.colSubtext
            font.weight: Font.Medium
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * Appearance.effectiveScale

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4 * Appearance.effectiveScale

                StyledText {
                    text: name
                    font.pixelSize: Appearance.font.pixelSize.large
                    font.family: Appearance.font.family.title
                    font.weight: Font.DemiBold
                    color: Appearance.colors.colOnLayer1
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                RowLayout {
                    spacing: 6 * Appearance.effectiveScale
                    MaterialSymbol {
                        text: icon
                        iconSize: 16 * Appearance.effectiveScale
                        color: accentColor
                    }
                    StyledText {
                        text: subText
                        font.pixelSize: Appearance.font.pixelSize.small
                        color: Appearance.colors.colSubtext
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            // Distribution / Shell Logo
            CustomIcon {
                visible: logoSource !== ""
                Layout.preferredWidth: 64 * Appearance.effectiveScale
                Layout.preferredHeight: 64 * Appearance.effectiveScale
                source: logoSource
                colorize: true
                color: cardRoot.accentColor
            }
        }
    }
}
