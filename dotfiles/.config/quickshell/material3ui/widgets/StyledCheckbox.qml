import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import "../core"

Item {
    id: root

    property bool checked: false
    property string text: ""
    property real iconSize: 18 * Appearance.effectiveScale
    
    property alias font: label.font
    property alias textColor: label.color
    
    // Mimic CheckBox signal
    signal toggled()
    signal clicked()

    implicitWidth: layout.implicitWidth
    implicitHeight: Math.max(layout.implicitHeight, 32 * Appearance.effectiveScale)

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            root.checked = !root.checked
            root.toggled()
            root.clicked()
        }
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        spacing: 12 * Appearance.effectiveScale

        Item {
            width: root.iconSize + (16 * Appearance.effectiveScale)
            height: root.iconSize + (16 * Appearance.effectiveScale)

            // Hover indicator circle
            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                radius: width / 2
                color: root.checked ? Appearance.colors.colPrimary : Appearance.colors.colOnLayer0
                opacity: mouseArea.containsMouse ? 0.08 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            // Checkbox visual box
            Rectangle {
                id: box
                anchors.centerIn: parent
                width: root.iconSize
                height: root.iconSize
                radius: 2 * Appearance.effectiveScale
                
                border.width: root.checked ? 0 : 2 * Appearance.effectiveScale
                border.color: root.checked ? Appearance.colors.colPrimary : Appearance.colors.colSubtext
                color: root.checked ? Appearance.colors.colPrimary : "transparent"
                
                Behavior on color { ColorAnimation { duration: 150 } }
                Behavior on border.color { ColorAnimation { duration: 150 } }

                Shape {
                    id: checkShape
                    anchors.fill: parent
                    layer.enabled: true
                    layer.samples: 4
                    
                    state: root.checked ? "checked" : "unchecked"
                    
                    states: [
                        State {
                            name: "checked"
                            PropertyChanges { target: checkShape; scale: 1.0; opacity: 1.0 }
                        },
                        State {
                            name: "unchecked"
                            PropertyChanges { target: checkShape; scale: 0.0; opacity: 0.0 }
                        }
                    ]
                    
                    transitions: [
                        Transition {
                            from: "unchecked"
                            to: "checked"
                            SequentialAnimation {
                                PauseAnimation { duration: 80 }
                                ParallelAnimation {
                                    NumberAnimation { target: checkShape; property: "scale"; duration: 300; easing.type: Easing.OutBack; easing.overshoot: 2.0 }
                                    NumberAnimation { target: checkShape; property: "opacity"; duration: 150 }
                                }
                            }
                        },
                        Transition {
                            from: "checked"
                            to: "unchecked"
                            ParallelAnimation {
                                NumberAnimation { target: checkShape; property: "scale"; duration: 150; easing.type: Easing.InBack }
                                NumberAnimation { target: checkShape; property: "opacity"; duration: 100 }
                            }
                        }
                    ]
                    
                    // Fixed checkmark coordinates based on M3 24x24 standard
                    readonly property real penW: 1.8 * Appearance.effectiveScale

                    ShapePath {
                        strokeColor: Appearance.colors.colOnPrimary
                        strokeWidth: checkShape.penW
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        
                        startX: root.iconSize * (5.5 / 24)
                        startY: root.iconSize * (12.5 / 24)
                        
                        PathLine { 
                            x: root.iconSize * (10 / 24)
                            y: root.iconSize * (17 / 24)
                        }
                        
                        PathLine { 
                            x: root.iconSize * (19 / 24)
                            y: root.iconSize * (8 / 24)
                        }
                    }
                }
            }
        }

        StyledText {
            id: label
            Layout.fillWidth: true
            text: root.text
            visible: root.text !== ""
            font.pixelSize: Appearance.font.pixelSize.normal
            color: Appearance.colors.colOnLayer0
        }
    }
}
