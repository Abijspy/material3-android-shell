pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import "../core"
import "../widgets"
import "../services"

Item {
    id: root

    property string currentTimeStr: ""
    property bool is24Hour: false

    signal timeSelected(string timeStr)
    signal cancelled()

    // 0 = Dial, 1 = Keyboard/Input
    property int selectMode: 0
    // 0 = Hour, 1 = Minute
    property int activeField: 0

    property int selectedHour: 7
    property int selectedMinute: 0
    property string amPm: "AM"

    // Draft text used while typing in input mode (only normalized on focus loss)
    property string hourDraft: ""
    property string minuteDraft: ""

    readonly property real dialSize: 256 * Appearance.effectiveScale
    readonly property real dialCenter: 128 * Appearance.effectiveScale
    readonly property real knobRadius: 24 * Appearance.effectiveScale
    readonly property real dialRadius: dialCenter - knobRadius
    readonly property real innerRingRadius: root.dialRadius * 0.62

    // 24h hour dial: inner ring (12-23) aligns with outer ring (0-11), so 12 lines up with 0.
    // The ring (inner vs outer) is chosen by distance from center, the hour by angle.
    function isInnerHour(h) {
        return h >= 12
    }

    // Knob travel radius (inner ring when a 13-24 hour is selected in 24h dial mode)
    readonly property real knobCenterRadius: root.activeField === 0 && root.is24Hour && root.isInnerHour(root.selectedHour)
        ? root.innerRingRadius : root.dialRadius

    // Boundary between the inner (13-24) and outer (1-12) rings
    readonly property real ringThreshold: (root.dialRadius + root.innerRingRadius) / 2

    // Target Knob Angle (0..360); same angle for both rings (h and h+12 share a position)
    readonly property real knobAngleDeg: root.activeField === 0 ?
        ((root.selectedHour % 12) * 30) :
        (root.selectedMinute * 6)

    // Shortest-path continuous rotation angle
    property real handRotation: 0

    onKnobAngleDegChanged: root.updateShortestRotation()
    onActiveFieldChanged: root.updateShortestRotation()

    function updateShortestRotation() {
        let current = root.handRotation
        let currentNorm = (current % 360 + 360) % 360
        let target = root.knobAngleDeg
        let diff = (target - currentNorm + 540) % 360 - 180
        root.handRotation = current + diff
    }

    // Calculated Knob Center Position
    readonly property real knobAngleRad: (handRotation - 90) * Math.PI / 180
    readonly property real currentKnobX: dialCenter + root.knobCenterRadius * Math.cos(knobAngleRad)
    readonly property real currentKnobY: dialCenter + root.knobCenterRadius * Math.sin(knobAngleRad)

    implicitWidth: Math.max(300 * Appearance.effectiveScale, 364 * Appearance.effectiveScale)
    implicitHeight: contentCol.implicitHeight

    Component.onCompleted: root.parseInitialTime()

    onCurrentTimeStrChanged: root.parseInitialTime()

    function parseInitialTime() {
        if (!root.currentTimeStr || root.currentTimeStr.trim() === "") {
            const d = new Date()
            let h = d.getHours()
            let m = d.getMinutes()
            if (!root.is24Hour) {
                root.amPm = h >= 12 ? "PM" : "AM"
                h = h % 12
                if (h === 0) h = 12
            }
            root.selectedHour = h
            root.selectedMinute = m
            root.handRotation = root.knobAngleDeg
            return
        }

        let str = root.currentTimeStr.trim()
        let isPm = false
        if (str.toUpperCase().endsWith("PM")) {
            isPm = true
            str = str.substring(0, str.length - 2).trim()
        } else if (str.toUpperCase().endsWith("AM")) {
            isPm = false
            str = str.substring(0, str.length - 2).trim()
        }

        // Support both ':' and '.' as time separator (e.g. "17.05" or "17:05")
        const cleanStr = str.replace('.', ':')
        const parts = cleanStr.split(":")
        if (parts.length >= 2) {
            let h = parseInt(parts[0], 10) || 0
            let m = parseInt(parts[1], 10) || 0
            if (!root.is24Hour) {
                if (isPm) {
                    if (h < 12) h += 0
                } else {
                    if (h >= 12) {
                        isPm = true
                        if (h > 12) h -= 12
                    } else if (h === 0) {
                        h = 12
                    }
                }
                root.amPm = isPm ? "PM" : "AM"
            }
            root.selectedHour = root.is24Hour ? root.normalizeHour(h) : h
            root.selectedMinute = Math.min(59, Math.max(0, m))
            root.handRotation = root.knobAngleDeg
        }
    }

    function getFormattedTime() {
        let h = root.selectedHour
        let m = String(root.selectedMinute).padStart(2, '0')
        if (!root.is24Hour) {
            if (root.amPm === "PM" && h < 12) h += 12
            else if (root.amPm === "AM" && h === 12) h = 0
        }
        let hStr = String(h).padStart(2, '0')
        return hStr + ":" + m
    }

    // Wrap a 2-digit hour input instead of clamping it (24h: 24->00, 12h: 13->01)
    function normalizeHour(val) {
        if (root.is24Hour) return ((val % 24) + 24) % 24
        return ((val - 1) % 12 + 12) % 12 + 1
    }

    function commitHourInput() {
        let val = parseInt(root.hourDraft, 10)
        if (isNaN(val)) val = root.is24Hour ? 0 : 12
        root.selectedHour = root.normalizeHour(val)
        root.hourDraft = String(root.selectedHour).padStart(2, '0')
    }

    function commitMinuteInput() {
        let val = parseInt(root.minuteDraft, 10)
        if (isNaN(val)) val = 0
        val = Math.min(59, Math.max(0, val))
        root.selectedMinute = val
        root.minuteDraft = String(val).padStart(2, '0')
    }

    function confirm() {
        if (root.selectMode === 1) {
            root.commitHourInput()
            root.commitMinuteInput()
        }
        root.timeSelected(root.getFormattedTime())
    }

    MouseArea {
        anchors.fill: parent
    }

    // Role 7: Surface container high (Dialog Card Background)
    Rectangle {
        id: bgRect
        anchors.fill: parent
        radius: Appearance.rounding.card
        color: Appearance.m3colors.m3surfaceContainerHigh
    }

    StyledRectangularShadow {
        target: bgRect
        z: -1
    }

    ColumnLayout {
        id: contentCol
        anchors.fill: parent
        spacing: 0

        // Role 1: On surface variant (Title text "Select time" / "Enter time")
        StyledText {
            text: root.selectMode === 0 ? I18nService.tr("Select time") : I18nService.tr("Enter time")
            Layout.leftMargin: 24 * Appearance.effectiveScale
            Layout.topMargin: 24 * Appearance.effectiveScale
            Layout.bottomMargin: 24 * Appearance.effectiveScale
            font.pixelSize: Appearance.font.pixelSize.small
            font.weight: Font.Medium
            color: Appearance.m3colors.m3onSurfaceVariant
        }

        // Time Display Area (Hours : Minutes + AM/PM)
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: root.selectMode === 0 ? 28 * Appearance.effectiveScale : 20 * Appearance.effectiveScale
            spacing: 12 * Appearance.effectiveScale

            // Hour Box
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 6 * Appearance.effectiveScale

                // Role 17: Primary container (Active) / Role 3: Surface container highest (Inactive)
                Rectangle {
                    implicitWidth: 96 * Appearance.effectiveScale
                    implicitHeight: 80 * Appearance.effectiveScale
                    radius: 12 * Appearance.effectiveScale
                    color: root.activeField === 0 ? Appearance.m3colors.m3primaryContainer : Appearance.m3colors.m3surfaceContainerHighest
                    border.width: (root.selectMode === 1 && root.activeField === 0) ? 2 * Appearance.effectiveScale : 0
                    border.color: Appearance.m3colors.m3primary

                    // Role 16: On primary container (Active text) / Role 4: On surface (Inactive text)
                    StyledTextInput {
                        id: hourInput
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        text: root.selectMode === 0 ? String(root.selectedHour).padStart(2, '0') : root.hourDraft
                        font.pixelSize: Math.round(48 * Appearance.effectiveScale)
                        font.family: Appearance.font.family.numbers
                        font.weight: Font.Normal
                        color: root.activeField === 0 ? Appearance.m3colors.m3onPrimaryContainer : Appearance.m3colors.m3onSurface
                        readOnly: root.selectMode === 0
                        inputMask: "99"
                        backgroundColor: "transparent"
                        inputRadius: 0
                        borderInactiveWidth: 0
                        showActiveBorder: false
                        leftMargin: 0
                        rightMargin: 0

                        onTextChanged: {
                            if (root.selectMode === 1 && input.activeFocus) {
                                root.hourDraft = text
                                let val = parseInt(text, 10)
                                if (!isNaN(val)) {
                                    root.selectedHour = root.normalizeHour(val)
                                }
                            }
                        }

                        Connections {
                            target: hourInput.input
                            function onActiveFocusChanged() {
                                if (hourInput.input.activeFocus) {
                                    root.activeField = 0
                                    hourInput.input.selectAll()
                                } else {
                                    root.commitHourInput()
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.selectMode === 0
                        onClicked: root.activeField = 0
                    }
                }

                // Role 1: On surface variant ("Hour" label)
                StyledText {
                    visible: root.selectMode === 1
                    text: I18nService.tr("Hour")
                    Layout.alignment: Qt.AlignLeft
                    Layout.leftMargin: 4 * Appearance.effectiveScale
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.m3colors.m3onSurfaceVariant
                }
            }

            // Role 2: On surface (Separator ":")
            StyledText {
                text: ":"
                font.family: Appearance.font.family.numbers
                font.pixelSize: Math.round(48 * Appearance.effectiveScale)
                font.weight: Font.Normal
                color: Appearance.m3colors.m3onSurface
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 12 * Appearance.effectiveScale
            }

            // Minute Box
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 6 * Appearance.effectiveScale

                // Role 17: Primary container (Active) / Role 3: Surface container highest (Inactive)
                Rectangle {
                    implicitWidth: 96 * Appearance.effectiveScale
                    implicitHeight: 80 * Appearance.effectiveScale
                    radius: 12 * Appearance.effectiveScale
                    color: root.activeField === 1 ? Appearance.m3colors.m3primaryContainer : Appearance.m3colors.m3surfaceContainerHighest
                    border.width: (root.selectMode === 1 && root.activeField === 1) ? 2 * Appearance.effectiveScale : 0
                    border.color: Appearance.m3colors.m3primary

                    // Role 16: On primary container (Active text) / Role 4: On surface (Inactive text)
                    StyledTextInput {
                        id: minuteInput
                        anchors.fill: parent
                        horizontalAlignment: Text.AlignHCenter
                        text: root.selectMode === 0 ? String(root.selectedMinute).padStart(2, '0') : root.minuteDraft
                        font.pixelSize: Math.round(48 * Appearance.effectiveScale)
                        font.family: Appearance.font.family.numbers
                        font.weight: Font.Normal
                        color: root.activeField === 1 ? Appearance.m3colors.m3onPrimaryContainer : Appearance.m3colors.m3onSurface
                        readOnly: root.selectMode === 0
                        inputMask: "99"
                        backgroundColor: "transparent"
                        inputRadius: 0
                        borderInactiveWidth: 0
                        showActiveBorder: false
                        leftMargin: 0
                        rightMargin: 0

                        onTextChanged: {
                            if (root.selectMode === 1 && input.activeFocus) {
                                root.minuteDraft = text
                                let val = parseInt(text, 10)
                                if (!isNaN(val)) {
                                    root.selectedMinute = Math.min(59, Math.max(0, val))
                                }
                            }
                        }

                        Connections {
                            target: minuteInput.input
                            function onActiveFocusChanged() {
                                if (minuteInput.input.activeFocus) {
                                    root.activeField = 1
                                    minuteInput.input.selectAll()
                                } else {
                                    root.commitMinuteInput()
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: root.selectMode === 0
                        onClicked: root.activeField = 1
                    }
                }

                // Role 1: On surface variant ("Minute" label)
                StyledText {
                    visible: root.selectMode === 1
                    text: I18nService.tr("Minute")
                    Layout.alignment: Qt.AlignLeft
                    Layout.leftMargin: 4 * Appearance.effectiveScale
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.m3colors.m3onSurfaceVariant
                }
            }

            // AM/PM Vertical Toggle Buttons (Narrow 52px width, 4px gap, smooth 38px rounded pill)
            ColumnLayout {
                visible: !root.is24Hour
                Layout.alignment: Qt.AlignTop
                implicitWidth: 52 * Appearance.effectiveScale
                Layout.preferredWidth: 52 * Appearance.effectiveScale
                implicitHeight: 80 * Appearance.effectiveScale
                Layout.preferredHeight: 80 * Appearance.effectiveScale
                spacing: 4 * Appearance.effectiveScale

                SegmentedButton {
                    implicitWidth: 52 * Appearance.effectiveScale
                    Layout.preferredWidth: 52 * Appearance.effectiveScale
                    implicitHeight: 38 * Appearance.effectiveScale
                    Layout.preferredHeight: 38 * Appearance.effectiveScale
                    Layout.fillWidth: false
                    orientation: Qt.Vertical
                    pillOnActive: true
                    fullRadius: 19 * Appearance.effectiveScale
                    leftPadding: 0
                    rightPadding: 0
                    buttonText: "AM"
                    checked: root.amPm === "AM"
                    colActive: Appearance.m3colors.m3tertiaryContainer
                    colInactive: Appearance.m3colors.m3surfaceContainerHighest
                    colActiveText: Appearance.m3colors.m3onTertiaryContainer
                    colInactiveText: Appearance.m3colors.m3onSurfaceVariant
                    onClicked: root.amPm = "AM"
                }

                SegmentedButton {
                    implicitWidth: 52 * Appearance.effectiveScale
                    Layout.preferredWidth: 52 * Appearance.effectiveScale
                    implicitHeight: 38 * Appearance.effectiveScale
                    Layout.preferredHeight: 38 * Appearance.effectiveScale
                    Layout.fillWidth: false
                    orientation: Qt.Vertical
                    pillOnActive: true
                    fullRadius: 19 * Appearance.effectiveScale
                    leftPadding: 0
                    rightPadding: 0
                    buttonText: "PM"
                    checked: root.amPm === "PM"
                    colActive: Appearance.m3colors.m3tertiaryContainer
                    colInactive: Appearance.m3colors.m3surfaceContainerHighest
                    colActiveText: Appearance.m3colors.m3onTertiaryContainer
                    colInactiveText: Appearance.m3colors.m3onSurfaceVariant
                    onClicked: root.amPm = "PM"
                }
            }
        }

        // Clock Dial View (Mode 0)
        Item {
            visible: root.selectMode === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 24 * Appearance.effectiveScale
            implicitWidth: root.dialSize
            implicitHeight: root.dialSize

            // Role 15: Surface container highest (Dial Face Background Circle)
            Rectangle {
                id: dialBackground
                anchors.fill: parent
                radius: width / 2
                color: Appearance.m3colors.m3surfaceContainerHighest

                MouseArea {
                    id: dialMouseArea
                    anchors.fill: parent
                    preventStealing: true

                    function handleMouse(mx, my, isRelease) {
                        const cx = width / 2
                        const cy = height / 2
                        const dx = mx - cx
                        const dy = my - cy

                        let rad = Math.atan2(dy, dx)
                        let deg = rad * 180 / Math.PI
                        if (deg < 0) deg += 360
                        let topDeg = (deg + 90) % 360

                        if (root.activeField === 0) {
                            let h = Math.round(topDeg / 30) % 12
                            if (root.is24Hour) {
                                let dist = Math.sqrt(dx * dx + dy * dy)
                                if (dist < root.ringThreshold) {
                                    root.selectedHour = h + 12
                                } else {
                                    root.selectedHour = h
                                }
                            } else {
                                root.selectedHour = h === 0 ? 12 : h
                            }
                            if (isRelease) {
                                root.activeField = 1
                            }
                        } else {
                            let m = Math.round(topDeg / 6) % 60
                            root.selectedMinute = m
                        }
                    }

                    onPressed: mouse => handleMouse(mouse.x, mouse.y, false)
                    onPositionChanged: mouse => handleMouse(mouse.x, mouse.y, false)
                    onReleased: mouse => handleMouse(mouse.x, mouse.y, true)
                }

                // Role 10: Primary (Center Pin)
                Rectangle {
                    width: 6 * Appearance.effectiveScale
                    height: 6 * Appearance.effectiveScale
                    radius: width / 2
                    color: Appearance.m3colors.m3primary
                    anchors.centerIn: parent
                    z: 5
                }

                // ── LAYER 1: Base Dial Numbers (Role 14: On surface) ──
                Repeater {
                    model: (root.activeField === 0 && root.is24Hour) ? 24 : 12

                    delegate: Item {
                        required property int index
                        readonly property bool isInnerIndex: index >= 12
                        readonly property int val: {
                            if (root.activeField === 1) return index * 5
                            if (root.is24Hour) return index
                            return index === 0 ? 12 : index
                        }
                        readonly property string displayStr: root.activeField === 0 ? val.toString() : String(val).padStart(2, '0')
                        readonly property real ringRadius: isInnerIndex ? root.innerRingRadius : root.dialRadius
                        readonly property real angleRad: ((index % 12) * 30 - 90) * Math.PI / 180
                        readonly property real numX: root.dialCenter + ringRadius * Math.cos(angleRad)
                        readonly property real numY: root.dialCenter + ringRadius * Math.sin(angleRad)

                        x: numX - width / 2
                        y: numY - height / 2
                        width: root.knobRadius * 2
                        height: root.knobRadius * 2
                        z: 2

                        StyledText {
                            anchors.centerIn: parent
                            text: parent.displayStr
                            font.pixelSize: Appearance.font.pixelSize.normal
                            font.family: Appearance.font.family.numbers
                            font.weight: Font.Normal
                            color: Appearance.m3colors.m3onSurface
                        }
                    }
                }

                // ── LAYER 2: Hand Indicator & Primary Knob Disc ──
                Item {
                    id: handIndicator
                    anchors.centerIn: parent
                    width: 0
                    height: 0
                    z: 3

                    rotation: root.handRotation

                    Behavior on rotation {
                        enabled: !dialMouseArea.pressed
                        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                    }

                    // Role 10: Primary (Hand Line)
                    Canvas {
                        id: handLineCanvas
                        anchors.centerIn: parent
                        width: root.dialSize
                        height: root.dialSize
                        antialiasing: true

                        onPaint: {
                            const ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.beginPath();
                            ctx.moveTo(root.dialCenter, root.dialCenter);
                            ctx.lineTo(root.dialCenter, root.dialCenter - root.knobCenterRadius);
                            ctx.strokeStyle = Appearance.m3colors.m3primary;
                            ctx.lineWidth = 2 * Appearance.effectiveScale;
                            ctx.lineCap = "round";
                            ctx.stroke();
                        }

                        Component.onCompleted: handLineCanvas.requestPaint()
                        onWidthChanged: handLineCanvas.requestPaint()
                        onHeightChanged: handLineCanvas.requestPaint()

                        Connections {
                            target: root
                            function onKnobCenterRadiusChanged() { handLineCanvas.requestPaint() }
                        }

                        Connections {
                            target: Appearance.m3colors
                            function onM3primaryChanged() { handLineCanvas.requestPaint() }
                        }
                    }

                    // Role 11: Primary (Knob Disc)
                    Rectangle {
                        x: -root.knobRadius
                        y: -root.knobCenterRadius - root.knobRadius
                        width: root.knobRadius * 2
                        height: root.knobRadius * 2
                        radius: root.knobRadius
                        color: Appearance.m3colors.m3primary
                        antialiasing: true
                    }
                }

                // ── LAYER 3: Circular Clipped Contrast Overlay (On primary text inside knob) ──
                Item {
                    id: knobClippedContainer
                    x: root.currentKnobX - root.knobRadius
                    y: root.currentKnobY - root.knobRadius
                    width: root.knobRadius * 2
                    height: root.knobRadius * 2
                    z: 4

                    Item {
                        id: contrastContent
                        anchors.fill: parent
                        visible: false

                        Repeater {
                            model: (root.activeField === 0 && root.is24Hour) ? 24 : 12

                            delegate: Item {
                                required property int index
                                readonly property bool isInnerIndex: index >= 12
                                readonly property int val: {
                                    if (root.activeField === 1) return index * 5
                                    if (root.is24Hour) return index
                                    return index === 0 ? 12 : index
                                }
                                readonly property string displayStr: root.activeField === 0 ? val.toString() : String(val).padStart(2, '0')
                                readonly property real ringRadius: isInnerIndex ? root.innerRingRadius : root.dialRadius
                                readonly property real angleRad: ((index % 12) * 30 - 90) * Math.PI / 180
                                readonly property real numX: root.dialCenter + ringRadius * Math.cos(angleRad)
                                readonly property real numY: root.dialCenter + ringRadius * Math.sin(angleRad)

                                x: (numX - root.currentKnobX + root.knobRadius) - width / 2
                                y: (numY - root.currentKnobY + root.knobRadius) - height / 2
                                width: root.knobRadius * 2
                                height: root.knobRadius * 2

                                StyledText {
                                    anchors.centerIn: parent
                                    text: parent.displayStr
                                    font.pixelSize: Appearance.font.pixelSize.normal
                                    font.family: Appearance.font.family.numbers
                                    font.weight: Font.Normal
                                    color: Appearance.m3colors.m3onPrimary
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: contrastMask
                        anchors.fill: parent
                        radius: width / 2
                        color: "black"
                        visible: false
                    }

                    OpacityMask {
                        anchors.fill: parent
                        source: contrastContent
                        maskSource: contrastMask
                    }
                }
            }
        }

        // Bottom Action Bar (Toggle icon + Cancel / OK buttons)
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24 * Appearance.effectiveScale
            Layout.rightMargin: 24 * Appearance.effectiveScale
            Layout.bottomMargin: 24 * Appearance.effectiveScale
            Layout.topMargin: 12 * Appearance.effectiveScale
            spacing: 8 * Appearance.effectiveScale

            // Role 13: On surface variant (Switch Mode Button)
            RippleButton {
                implicitWidth: 40 * Appearance.effectiveScale
                implicitHeight: 40 * Appearance.effectiveScale
                buttonRadius: Appearance.rounding.full
                colBackground: "transparent"
                colBackgroundHover: Appearance.colors.colLayer2Hover
                onClicked: {
                    if (root.selectMode === 1) {
                        root.commitHourInput()
                        root.commitMinuteInput()
                        root.selectMode = 0
                    } else {
                        root.hourDraft = String(root.selectedHour).padStart(2, '0')
                        root.minuteDraft = String(root.selectedMinute).padStart(2, '0')
                        root.selectMode = 1
                        hourInput.forceActiveFocus()
                    }
                }
                contentItem: MaterialSymbol {
                    text: root.selectMode === 0 ? "keyboard" : "schedule"
                    iconSize: 22 * Appearance.effectiveScale
                    horizontalAlignment: Text.AlignHCenter
                    color: Appearance.m3colors.m3onSurfaceVariant
                }
            }

            Item { Layout.fillWidth: true }

            // Role 12: Primary (Cancel Button)
            RippleButton {
                implicitHeight: 40 * Appearance.effectiveScale
                buttonRadius: Appearance.rounding.full
                colBackground: "transparent"
                colBackgroundHover: Appearance.colors.colLayer2Hover
                onClicked: root.cancelled()
                contentItem: StyledText {
                    text: I18nService.tr("Cancel")
                    font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.Medium
                    color: Appearance.m3colors.m3primary
                }
            }

            // Role 12: Primary (OK Button)
            RippleButton {
                implicitHeight: 40 * Appearance.effectiveScale
                buttonRadius: Appearance.rounding.full
                colBackground: "transparent"
                colBackgroundHover: Appearance.colors.colLayer2Hover
                onClicked: root.confirm()
                contentItem: StyledText {
                    text: I18nService.tr("OK")
                    font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.Medium
                    color: Appearance.m3colors.m3primary
                }
            }
        }
    }
}
