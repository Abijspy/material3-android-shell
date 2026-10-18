pragma ComponentBehavior: Bound
import "../core"
import "../core/functions" as Functions
import "."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * Material 3 styled stepper component: [-] [value] [+]
 *
 * - Editable value field; typing a number clamps it to [from, to] on commit.
 * - The value area width grows with its content (e.g. "1", "20px", "1000ms")
 *   so long values are never cut off.
 * - The -/+ buttons show tooltips; the - tooltip includes the min value
 *   and the + tooltip includes the max value.
 */
RowLayout {
    id: root
    spacing: 0

    // ── API ──
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: 0
    property real displayFactor: 1
    property string prefix: ""
    property string suffix: ""
    property bool editable: true
    property string decreaseTooltip: ""
    property string increaseTooltip: ""

    readonly property bool editing: valueField.activeFocus

    // ── Styling ──
    property real baseHeight: 35 * Appearance.effectiveScale
    property real radius: Appearance.rounding.small
    property real innerButtonRadius: Appearance.rounding.unsharpen
    property real valuePadding: 8 * Appearance.effectiveScale

    function formatValue(v) {
        return root.prefix + (v * root.displayFactor).toFixed(root.decimals) + root.suffix
    }

    readonly property string displayText: root.formatValue(root.value)
    readonly property string minText: root.formatValue(root.from)
    readonly property string maxText: root.formatValue(root.to)

    function clampValue(v) {
        return Math.min(root.to, Math.max(root.from, v))
    }

    function applyValue(v) {
        root.value = root.clampValue(v)
    }

    function increment() {
        root.applyValue(root.value + root.stepSize)
    }

    function decrement() {
        root.applyValue(root.value - root.stepSize)
    }

    function commitText() {
        if (!root.editable) return
        var raw = valueField.text.replace(root.prefix, "").replace(root.suffix, "").trim()
        var parsed = parseFloat(raw)
        if (isNaN(parsed)) {
            valueField.text = root.displayText
            return
        }
        root.applyValue(parsed / root.displayFactor)
        valueField.text = root.displayText
    }

    component StepperButton: Item {
        id: btn
        required property bool increase
        required property string tooltip

        property bool realHovered: false
        property bool pressed: false

        implicitWidth: root.baseHeight
        implicitHeight: root.baseHeight

        Rectangle {
            anchors.fill: parent
            topLeftRadius: btn.increase ? root.innerButtonRadius : root.radius
            bottomLeftRadius: btn.increase ? root.innerButtonRadius : root.radius
            topRightRadius: btn.increase ? root.radius : root.innerButtonRadius
            bottomRightRadius: btn.increase ? root.radius : root.innerButtonRadius
            color: btn.pressed ? Appearance.colors.colLayer2Active
                 : btn.realHovered ? Appearance.colors.colLayer2Hover
                 : Appearance.colors.colLayer2

            Behavior on color {
                animation: Appearance.animation.elementMoveFast.colorAnimation.createObject(this)
            }
        }

        MaterialSymbol {
            anchors.centerIn: parent
            text: btn.increase ? "add" : "remove"
            iconSize: 20 * Appearance.effectiveScale
            color: Appearance.colors.colOnLayer2
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onEntered: btn.realHovered = true
            onExited: {
                btn.realHovered = false
                btn.pressed = false
                repeatTimer.stop()
            }
            onPressed: {
                btn.pressed = true
                if (btn.increase) root.increment()
                else root.decrement()
                repeatTimer.restart()
            }
            onReleased: {
                btn.pressed = false
                repeatTimer.stop()
            }
            onCanceled: {
                btn.pressed = false
                repeatTimer.stop()
            }
        }

        Timer {
            id: repeatTimer
            interval: 120
            repeat: true
            onTriggered: {
                if (btn.increase) root.increment()
                else root.decrement()
            }
        }

        StyledToolTip {
            text: btn.tooltip
        }
    }

    component Divider: Item {
        implicitWidth: 2 * Appearance.effectiveScale
        Layout.fillHeight: true
    }

    StepperButton {
        increase: false
        tooltip: root.decreaseTooltip !== "" ? root.decreaseTooltip
            : "Decrease (min: " + root.minText + ")"
    }

    Divider {}

    Item {
        id: valueArea

        implicitWidth: Math.max(measureText.implicitWidth, 2 * root.valuePadding) + (2 * root.valuePadding)
        implicitHeight: root.baseHeight

        StyledTextInput {
            id: valueField
            anchors.fill: parent
            text: root.displayText
            readOnly: !root.editable
            selectByMouse: root.editable
            backgroundColor: Appearance.colors.colLayer2
            inputRadius: 0
            showActiveBorder: false
            borderInactiveWidth: 0
            leftMargin: root.valuePadding
            rightMargin: root.valuePadding
            horizontalAlignment: TextInput.AlignHCenter
            color: Appearance.colors.colOnLayer2
            font.family: Appearance.font.family.numbers
            font.variableAxes: Appearance.font.variableAxes.numbers
            font.pixelSize: Appearance.font.pixelSize.small

            onActiveFocusChanged: {
                if (activeFocus) {
                    text = (root.value * root.displayFactor).toFixed(root.decimals)
                    valueField.input.selectAll()
                } else {
                    root.commitText()
                }
            }

            onAccepted: {
                root.commitText()
                valueField.input.focus = false
            }
        }

        Text {
            id: measureText
            visible: false
            text: valueField.activeFocus ? valueField.text : root.displayText
            font: valueField.font
        }
    }

    Divider {}

    StepperButton {
        increase: true
        tooltip: root.increaseTooltip !== "" ? root.increaseTooltip
            : "Increase (max: " + root.maxText + ")"
    }

    Connections {
        target: root
        function onValueChanged() {
            if (!valueField.activeFocus) valueField.text = root.displayText
        }
        function onPrefixChanged() {
            if (!valueField.activeFocus) valueField.text = root.displayText
        }
        function onSuffixChanged() {
            if (!valueField.activeFocus) valueField.text = root.displayText
        }
        function onDecimalsChanged() {
            if (!valueField.activeFocus) valueField.text = root.displayText
        }
        function onDisplayFactorChanged() {
            if (!valueField.activeFocus) valueField.text = root.displayText
        }
    }
}