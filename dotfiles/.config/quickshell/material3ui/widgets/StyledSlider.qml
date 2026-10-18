pragma ComponentBehavior: Bound
import "../core"
import "."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell.Widgets

/**
 * Material 3 slider. See https://m3.material.io/components/sliders/overview
 * It doesn't exactly match the spec because it does not make sense to have stuff on a computer that fucking huge.
 * Should be at 3/4 scale...
 */

Slider {
    id: root

    property real defaultValue: -1
    property list<real> stopIndicatorValues: defaultValue >= 0 ? [defaultValue] : []
    property list<real> dividerValues: []
    property real dividerMargins: 2 * Appearance.effectiveScale
    enum Configuration {
        Wavy = 4,
        X0 = 3,
        XS = 16,
        S = 24,
        M = 40,
        L = 56,
        XL = 96
    }

    property var configuration: StyledSlider.Configuration.S

    property real handleDefaultWidth: 3 * Appearance.effectiveScale
    property real handlePressedWidth: 1.5 * Appearance.effectiveScale
    property color highlightColor: Appearance.m3colors.m3primary // Adapted
    property color trackColor: Appearance.m3colors.m3secondaryContainer // Adapted
    property color handleColor: Appearance.m3colors.m3primary // Adapted
    property color dotColor: Appearance.m3colors.m3onSecondaryContainer // Adapted
    property color dotColorHighlighted: Appearance.m3colors.m3onPrimary // Adapted
    property real unsharpenRadius: 2 * Appearance.effectiveScale
    
    property real trackWidth: configuration * Appearance.effectiveScale
    property real trackRadius: trackWidth >= StyledSlider.Configuration.XL * Appearance.effectiveScale ? 28 * Appearance.effectiveScale
        : trackWidth >= StyledSlider.Configuration.L * Appearance.effectiveScale ? 16 * Appearance.effectiveScale
        : trackWidth >= StyledSlider.Configuration.M * Appearance.effectiveScale ? 12 * Appearance.effectiveScale
        : 8 * Appearance.effectiveScale
    property real handleHeight: (configuration === StyledSlider.Configuration.Wavy) ? 24 * Appearance.effectiveScale : (configuration === StyledSlider.Configuration.X0) ? 14 * Appearance.effectiveScale : Math.max(44 * Appearance.effectiveScale, trackWidth + (12 * Appearance.effectiveScale))
    property real handleWidth: handleDefaultWidth
    property real handleMargins: 4 * Appearance.effectiveScale
    property real trackDotSize: 3 * Appearance.effectiveScale
    property real insetIconSize: configuration >= StyledSlider.Configuration.XL ? 32 * Appearance.effectiveScale : 24 * Appearance.effectiveScale
    property bool showTrailingDot: true
    property bool usePercentTooltip: true
    property string tooltipContent: usePercentTooltip ? `${Math.round(((value - from) / (to - from)) * 100)}%` : `${Math.round(value)}`
    property bool wavy: configuration === StyledSlider.Configuration.Wavy // If true, the progress bar will have a wavy fill effect
    property bool animateWave: true
    // Effective visibility used to gate the wavy Canvas + FrameAnimation.
    // Defaults to `visible`, but hosts that toggle a parent panel via opacity
    // (so this slider's `visible` stays true while collapsed) should bind this
    // to the panel's real shown-state. When false, the WavyLine Canvas is fully
    // destroyed instead of repainting 60fps off-screen.
    property bool wavyVisible: visible
    property bool animateValue: true
    property real waveAmplitudeMultiplier: wavy ? 0.5 : 0
    property real waveFrequency: 6
    property real waveFps: 60

    leftPadding: handleMargins
    rightPadding: handleMargins
    implicitWidth: 100 * Appearance.effectiveScale
    property real effectiveDraggingWidth: width - leftPadding - rightPadding

    Layout.fillWidth: true
    from: 0
    to: 1

    Behavior on value {
        enabled: root.animateValue
        SmoothedAnimation {
            velocity: Appearance.animation.elementMoveFast.velocity
        }
    }

    Behavior on handleMargins {
        animation: Appearance.animation.elementMoveFast.numberAnimation.createObject(this)
    }

    component TrackDot: Rectangle {
        required property real value
        property real normalizedValue: (value - root.from) / (root.to - root.from)
        anchors.verticalCenter: parent.verticalCenter
        x: root.handleMargins + (normalizedValue * root.effectiveDraggingWidth) - (root.trackDotSize / 2)
        width: root.trackDotSize
        height: root.trackDotSize
        radius: Appearance.rounding.full
        color: normalizedValue > root.visualPosition ? root.dotColor : root.dotColorHighlighted

        Behavior on color {
            animation: Appearance.animation.elementMoveFast.colorAnimation.createObject(this)
        }
    }

    MouseArea {
        anchors.fill: parent
        onPressed: (mouse) => mouse.accepted = false
        cursorShape: root.pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor 
    }

    background: Item {
        id: background
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.width
        implicitHeight: trackWidth

        property var normalized: root.dividerValues.map(v => (v - root.from) / (root.to - root.from))
        property var filtered: normalized.filter(v => Math.abs(v - root.visualPosition) * root.effectiveDraggingWidth > root.handleMargins + root.handleWidth / 2 - root.dividerMargins)
        property var leftValues: [0, ...filtered.filter(v => v < root.visualPosition), root.visualPosition]
        property var rightValues: [root.visualPosition, ...filtered.filter(v => v > root.visualPosition), 1]
        property var leftWidths: leftValues.map((v, i, a) => a[i + 1] - v).slice(0, -1)
        property var rightWidths: rightValues.map((v, i, a) => a[i + 1] - v).slice(0, -1)

        // Fill left
        Repeater {
            model: background.leftWidths.length

            Loader {
                required property real index
                anchors.verticalCenter: background.verticalCenter
                property real leftMargin: index > 0 ? root.dividerMargins : 0
                property real rightMargin: index < background.leftWidths.length - 1 ? root.dividerMargins : root.handleMargins
                x: background.leftValues[index] * root.effectiveDraggingWidth + leftMargin + (index > 0 ? root.leftPadding : 0)
                width: background.leftWidths[index] * root.effectiveDraggingWidth - leftMargin - rightMargin - (index === background.leftWidths.length - 1 ? root.handleWidth / 2 : 0) + (index === 0 ? root.leftPadding : 0)
                height: root.trackWidth
                active: !root.wavy
                sourceComponent: Rectangle {
                    color: root.highlightColor
                    topLeftRadius: index === 0 ? root.trackRadius : root.unsharpenRadius
                    bottomLeftRadius: index === 0 ? root.trackRadius : root.unsharpenRadius
                    topRightRadius: root.unsharpenRadius
                    bottomRightRadius: root.unsharpenRadius
                }
            }
        }

        Repeater {
            model: background.leftWidths.length

            Loader {
                required property int index
                anchors.verticalCenter: background.verticalCenter
                property real leftMargin: index > 0 ? root.dividerMargins : 0
                property real rightMargin: index < background.leftWidths.length - 1 ? root.dividerMargins : root.handleMargins
                x: background.leftValues[index] * root.effectiveDraggingWidth + leftMargin + (index > 0 ? root.leftPadding : 0)
                width: background.leftWidths[index] * root.effectiveDraggingWidth - leftMargin - rightMargin - (index === background.leftWidths.length - 1 ? root.handleWidth / 2 : 0) + (index === 0 ? root.leftPadding : 0)
                height: root.height
                active: root.wavy && root.wavyVisible
                sourceComponent: WavyLine {
                    id: wavyFill
                    frequency: root.waveFrequency
                    fullLength: root.width
                    color: root.highlightColor
                    amplitudeMultiplier: root.wavy ? 0.5 : 0
                    width: parent.width
                    height: root.trackWidth
                    Connections {
                        target: root
                        function onValueChanged() { wavyFill.requestPaint(); }
                        function onHighlightColorChanged() { wavyFill.requestPaint(); }
                    }
                    FrameAnimation {
                        running: root.animateWave
                        onTriggered: {
                            wavyFill.requestPaint()
                        }
                    }
                }
            }
        }

        // Fill right
        Repeater {
            model: background.rightWidths.length

            Rectangle {
                required property int index
                anchors.verticalCenter: background.verticalCenter
                property real leftMargin: index > 0 ? root.dividerMargins : root.handleMargins
                property real rightMargin: index < background.rightWidths.length - 1 ? root.dividerMargins : 0
                x: background.rightValues[index] * root.effectiveDraggingWidth + leftMargin + (index === 0 ? root.handleWidth / 2 : 0) + root.leftPadding
                width: background.rightWidths[index] * root.effectiveDraggingWidth - leftMargin - rightMargin - (index === 0 ? root.handleWidth / 2 : 0) + (index === background.rightWidths.length - 1 ? root.rightPadding : 0)
                height: trackWidth
                color: root.trackColor
                topRightRadius: index === background.rightWidths.length - 1 ? root.trackRadius : root.unsharpenRadius
                bottomRightRadius: index === background.rightWidths.length - 1 ? root.trackRadius : root.unsharpenRadius
                topLeftRadius: root.unsharpenRadius
                bottomLeftRadius: root.unsharpenRadius
            }
        }

        // Stop indicators
        Repeater {
            model: root.stopIndicatorValues
            TrackDot {
                required property real modelData
                value: modelData
                anchors.verticalCenter: parent?.verticalCenter
            }
        }

        // M3 Trailing Dot
        Rectangle {
            anchors.verticalCenter: background.verticalCenter
            x: root.leftPadding + root.effectiveDraggingWidth - (width / 2)
            width: (root.showTrailingDot && root.trackWidth >= StyledSlider.Configuration.S * Appearance.effectiveScale) ? root.trackDotSize : 0
            height: width
            radius: width / 2
            color: root.visualPosition > 0.98 ? root.dotColorHighlighted : root.dotColor
            visible: width > 0
            
            Behavior on color {
                animation: Appearance.animation.elementMoveFast.colorAnimation.createObject(this)
            }
        }
    }

    handle: Rectangle {
        id: handle

        implicitWidth: root.handleWidth
        implicitHeight: root.handleHeight
        x: root.leftPadding + (root.visualPosition * root.effectiveDraggingWidth) - (root.handleWidth / 2)
        anchors.verticalCenter: root.verticalCenter
        radius: Appearance.rounding.full
        color: root.handleColor

        Item {
            id: valueIndicator
            implicitWidth: tooltipText.implicitWidth + (32 * Appearance.effectiveScale) // x padding 16 * 2
            implicitHeight: tooltipText.implicitHeight + (24 * Appearance.effectiveScale) // y padding 12 * 2
            
            // Constrain X so the tooltip never overflows the slider's left/right boundaries
            x: Math.max(-handle.x, Math.min(root.width - handle.x - width, (parent.width - width) / 2))
            
            anchors.bottom: parent.top
            anchors.bottomMargin: 4 * Appearance.effectiveScale
            
            scale: root.pressed ? 1.0 : 0.0
            opacity: root.pressed ? 1.0 : 0.0
            transformOrigin: Item.Bottom

            Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack } }
            Behavior on opacity { NumberAnimation { duration: 150 } }

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Appearance.colors.colTooltip
            }
            
            StyledText {
                id: tooltipText
                anchors.centerIn: parent
                text: root.tooltipContent
                font {
                    family: Appearance.font.family.numbers
                    variableAxes: Appearance.font.variableAxes.numbers
                    pixelSize: Appearance.font.pixelSize.normal
                }
                color: Appearance.colors.colOnTooltip
            }
        }
    }
}
