import "."
import "../core"
import "../core/functions" as Functions
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * Material Design Floating Action Button (FAB).
 *
 * Three modes, decided by which properties are set:
 *  - Icon only:  set `icon` (and optionally `tooltipText`) - a squircle button.
 *  - Extended:   also set `label` - a pill with an icon and text.
 *  - Speed dial: set `actions` (list of { icon, label, callback }) - tapping the
 *                FAB pops the action pills up above it.
 *
 * Sizes: set `variant` to "normal" (56 card / 24 icon / 16 radius),
 * "medium" (80 / 28 / 20) or "large" (96 / 36 / 28). `size`, `iconSize` and
 * `radius` stay individually overridable.
 *
 * Anchor the component to fill the surface it floats on:
 *     FloatingActionButton { anchors.fill: parent }
 *
 * All colors are overridable, including the "open" variant used while the
 * speed dial is expanded, so the same component can be restyled freely.
 */
Item {
    id: root

    // ── Content ──
    property string icon: "add"
    property string iconOpen: "close"
    property string label: ""
    property string tooltipText: icon
    property string tooltipOpenText: tooltipText
    property var actions: [] // [{ icon, label, callback }]
    // ── Behavior ──
    property bool autoClose: true
    readonly property bool hasActions: actionRepeater.count > 0
    // ── Sizing ──
    property string variant: "normal"
    // "normal" | "medium" | "large"
    property real size: root._variantValue(56, 80, 96) * Appearance.effectiveScale
    property real iconSize: root._variantValue(24, 28, 36) * Appearance.effectiveScale
    property real radius: root._variantValue(16, 20, 28) * Appearance.effectiveScale
    property real margin: 16 * Appearance.effectiveScale
    property real actionSpacing: 4
    property real actionGap: 8
    readonly property real pillRadius: size / 2
    readonly property bool extended: label !== ""
    // ── Colors (idle) ──
    property color colBackground: Appearance.m3colors.m3primaryContainer
    property color colBackgroundHover: Functions.ColorUtils.mix(colBackground, colOnColor, 0.92)
    property color colOnColor: Appearance.m3colors.m3onPrimaryContainer
    property color colRipple: Functions.ColorUtils.applyAlpha(colOnColor, 0.15)
    // ── Colors (speed dial open) ──
    property color colBackgroundOpen: Appearance.colors.colPrimary
    property color colBackgroundOpenHover: Functions.ColorUtils.mix(colBackgroundOpen, colOnColorOpen, 0.92)
    property color colOnColorOpen: Appearance.colors.colOnPrimary
    property color colRippleOpen: Functions.ColorUtils.applyAlpha(colOnColorOpen, 0.15)
    // ── State & signals ──
    property bool _open: false
    readonly property bool open: _open

    signal clicked()
    signal actionTriggered(int index)

    function openMenu() {
        _open = true;
    }

    function closeMenu() {
        _open = false;
    }

    function toggleMenu() {
        _open = !_open;
    }

    function _variantValue(normal, medium, large) {
        if (root.variant === "medium")
            return medium;

        if (root.variant === "large")
            return large;

        return normal;
    }

    // ── Scrim: closes the speed dial on outside click ──
    MouseArea {
        anchors.fill: parent
        visible: root.open
        z: 90
        onClicked: root._open = false
    }

    // ── Speed dial pills, stacked above the FAB ──
    Item {
        id: speedDial

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: root.margin
        anchors.bottomMargin: root.margin
        width: root.size
        height: root.size
        z: 100

        Repeater {
            id: actionRepeater

            model: root.actions

            delegate: RippleButton {
                required property int index
                required property var modelData
                readonly property real shownY: -(root.actionGap + (index + 1) * root.size + index * root.actionSpacing)

                anchors.right: parent.right
                y: root.open ? shownY : 0
                opacity: root.open ? 1 : 0
                visible: root.open
                implicitHeight: root.size
                implicitWidth: Math.max(root.size, pillRow.implicitWidth + 48 * Appearance.effectiveScale)
                buttonRadius: root.pillRadius
                colBackground: root.colBackground
                colBackgroundHover: root.colBackgroundHover
                colRipple: root.colRipple
                onClicked: {
                    if (root.autoClose)
                        root._open = false;

                    if (modelData.callback)
                        modelData.callback();

                    root.actionTriggered(index);
                }

                RowLayout {
                    id: pillRow

                    anchors.centerIn: parent
                    spacing: 12 * Appearance.effectiveScale

                    MaterialSymbol {
                        text: modelData.icon ?? ""
                        iconSize: root.iconSize
                        color: root.colOnColor
                    }

                    StyledText {
                        text: modelData.label ?? ""
                        visible: modelData.label !== undefined && modelData.label !== ""
                        font.pixelSize: Appearance.font.pixelSize.normal
                        font.weight: Font.Medium
                        color: root.colOnColor
                    }

                }

                Behavior on y {
                    NumberAnimation {
                        duration: 200 + index * 50
                        easing.type: Easing.OutBack
                        easing.overshoot: 1.1
                    }

                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: 160 + index * 40
                    }

                }

            }

        }

    }

    // ── Main FAB ──
    StyledRectangularShadow {
        target: fabButton
        radius: fabButton.buttonRadius
        z: 101
    }

    RippleButton {
        id: fabButton

        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: root.margin
        anchors.bottomMargin: root.margin
        implicitWidth: root.extended ? Math.max(root.size, fabRow.implicitWidth + 32 * Appearance.effectiveScale) : root.size
        implicitHeight: root.size
        buttonRadius: root.open ? root.pillRadius : root.radius
        colBackground: root.open ? root.colBackgroundOpen : root.colBackground
        colBackgroundHover: root.open ? root.colBackgroundOpenHover : root.colBackgroundHover
        colRipple: root.open ? root.colRippleOpen : root.colRipple
        z: 102
        onClicked: {
            if (root.hasActions)
                root._open = !root._open;

            root.clicked();
        }

        RowLayout {
            id: fabRow

            anchors.centerIn: parent
            spacing: 8 * Appearance.effectiveScale

            MaterialSymbol {
                text: root.open ? root.iconOpen : root.icon
                iconSize: root.iconSize
                color: root.open ? root.colOnColorOpen : root.colOnColor

                Behavior on color {
                    ColorAnimation {
                        duration: 200
                    }

                }

                Behavior on text {
                }

            }

            StyledText {
                text: root.label
                visible: root.extended
                font.pixelSize: Appearance.font.pixelSize.normal
                font.weight: Font.Medium
                color: root.open ? root.colOnColorOpen : root.colOnColor

                Behavior on color {
                    ColorAnimation {
                        duration: 200
                    }

                }

            }

        }

        StyledToolTip {
            text: root.open ? root.tooltipOpenText : root.tooltipText
            extraVisibleCondition: root.tooltipText !== ""
        }

        Behavior on colBackground {
            ColorAnimation {
                duration: 200
            }

        }

    }

}
