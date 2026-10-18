pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../core"
import "../widgets"
import "../services"
import "../panels/Dashboard/calendar_layout.js" as CalendarLayout

Item {
    id: root

    property int firstDayOfWeek: Config.ready ? (Config.options.time.firstDayOfWeek ?? 1) : 1
    property string currentDateStr: ""
    readonly property string dateStyle: Config.ready && Config.options.time ? (Config.options.time.dateStyle ?? "DMY") : "DMY"

    signal dateSelected(string dateStr)
    signal cancelled()

    property int selectMode: 0
    property bool yearMode: false
    property int monthShift: 0

    property date viewingDate: CalendarLayout.getDateInXMonthsTime(monthShift)
    property var calendarLayout: CalendarLayout.getCalendarLayout(viewingDate, monthShift === 0, firstDayOfWeek)
    property string pendingDateStr: ""
    property string inputText: ""
    property bool inputValid: true

    readonly property real cellSize: 40 * Appearance.effectiveScale
    readonly property int yearRangeStart: new Date().getFullYear() - 49
    readonly property real __monthGridTotal: root.cellSize * 0.7 + Appearance.sizes.calendarSpacing + 6 * root.cellSize + 5 * Appearance.sizes.calendarSpacing
    implicitWidth: Math.max(300 * Appearance.effectiveScale,
        7 * root.cellSize + 6 * Appearance.sizes.calendarSpacing + 48 * Appearance.effectiveScale)
    implicitHeight: contentCol.implicitHeight

    Component.onCompleted: {
        root.pendingDateStr = root.__toCanonicalDateStr(root.currentDateStr)
        root.inputText = root.__formatDisplayByStyle(root.pendingDateStr)
        root.__setViewingMonth()
    }

    onCurrentDateStrChanged: {
        root.pendingDateStr = root.__toCanonicalDateStr(root.currentDateStr)
        root.inputText = root.__formatDisplayByStyle(root.pendingDateStr)
        root.__setViewingMonth()
    }

    function __formatDate(year, month, day) {
        return year + "-" + String(month).padStart(2, '0') + "-" + String(day).padStart(2, '0')
    }

    function __parseAnyDate(str) {
        if (!str) return null
        let parts = str.trim().split(/[-/]/).map(Number)
        if (parts.length < 3 || parts.some(isNaN)) return null

        let y, m, d
        if (parts[0] > 1000) {
            y = parts[0]; m = parts[1]; d = parts[2]
        } else if (parts[2] > 1000) {
            if (root.dateStyle === "MDY") {
                m = parts[0]; d = parts[1]; y = parts[2]
            } else {
                d = parts[0]; m = parts[1]; y = parts[2]
            }
        } else {
            return null
        }
        if (y < 1000 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return null
        return { year: y, month: m, day: d }
    }

    function __toCanonicalDateStr(str) {
        let p = root.__parseAnyDate(str)
        if (!p) return ""
        return p.year + "-" + String(p.month).padStart(2, '0') + "-" + String(p.day).padStart(2, '0')
    }

    function __formatDisplayByStyle(dateStr) {
        let p = root.__parseAnyDate(dateStr)
        if (!p) return ""
        const ys = String(p.year).padStart(4, '0')
        const ms = String(p.month).padStart(2, '0')
        const ds = String(p.day).padStart(2, '0')

        if (root.dateStyle === "YMD") return ys + "/" + ms + "/" + ds
        if (root.dateStyle === "MDY") return ms + "/" + ds + "/" + ys
        return ds + "/" + ms + "/" + ys
    }

    function __getMonthYearHeader(dateObj) {
        if (!dateObj) return ""
        const monthNames = [
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        ]
        const m = dateObj.getMonth()
        const y = dateObj.getFullYear()
        return I18nService.tr(monthNames[m]) + " " + y
    }

    function __formatHeaderDate(dateStr) {
        let p = root.__parseAnyDate(dateStr)
        if (!p) return I18nService.tr("Select Date")
        const dayNames = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"]
        const monthNames = [
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        ]
        const d = new Date(p.year, p.month - 1, p.day)
        const dayShort = I18nService.tr(dayNames[d.getDay()])
        const monthName = I18nService.tr(monthNames[p.month - 1])
        return dayShort + ", " + monthName + " " + p.day
    }

    function __daysInMonth(year, month) {
        return new Date(year, month, 0).getDate()
    }

    function __isValidInput(text) {
        if (!text) return false
        const digits = text.replace(/\D/g, '')
        if (digits.length < 8) return false
        const p = root.__parseAnyDate(text)
        if (!p) return false
        if (p.day > root.__daysInMonth(p.year, p.month)) return false
        return true
    }

    function __errorText() {
        const fmt = root.dateStyle === "YMD" ? "YYYY/MM/DD" : (root.dateStyle === "MDY" ? "MM/DD/YYYY" : "DD/MM/YYYY")
        const digits = root.inputText.replace(/\D/g, '')
        if (digits.length < 8) return I18nService.tr("Complete the date: ") + fmt
        return I18nService.tr("Invalid date")
    }

    function __formatTypedDateInput(rawText, isDeleting) {
        if (!rawText) return ""
        let digits = rawText.replace(/\D/g, '').substring(0, 8)
        if (digits.length === 0) return ""

        let y = "", m = "", d = ""
        if (root.dateStyle === "YMD") {
            y = digits.substring(0, 4)
            m = digits.substring(4, 6)
            d = digits.substring(6, 8)
        } else if (root.dateStyle === "MDY") {
            m = digits.substring(0, 2)
            d = digits.substring(2, 4)
            y = digits.substring(4, 8)
        } else {
            d = digits.substring(0, 2)
            m = digits.substring(2, 4)
            y = digits.substring(4, 8)
        }

        if (m.length === 2) {
            let mv = parseInt(m, 10)
            if (mv > 12) m = "12"
        }

        if (d.length === 2) {
            let dv = parseInt(d, 10)
            if (dv > 31) d = "31"
            if (m.length === 2) {
                let mv = parseInt(m, 10)
                let yv = y.length === 4 ? parseInt(y, 10) : new Date().getFullYear()
                if (mv >= 1 && mv <= 12) {
                    const max = root.__daysInMonth(yv, mv)
                    if (dv > max) d = String(max).padStart(2, '0')
                }
            }
        }

        if (root.dateStyle === "YMD") {
            if (digits.length <= 4) return y
            if (digits.length <= 6) {
                let res = y + "/" + m
                if (isDeleting && rawText.endsWith("/")) res = res.substring(0, res.length - 1)
                return res
            }
            let res = y + "/" + m + "/" + d
            if (isDeleting && rawText.endsWith("/")) res = res.substring(0, res.length - 1)
            return res
        } else {
            const first = root.dateStyle === "MDY" ? m : d
            const second = root.dateStyle === "MDY" ? d : m
            if (digits.length <= 2) return first
            if (digits.length <= 4) {
                let res = first + "/" + second
                if (isDeleting && rawText.endsWith("/")) res = res.substring(0, res.length - 1)
                return res
            }
            let res = first + "/" + second + "/" + y
            if (isDeleting && rawText.endsWith("/")) res = res.substring(0, res.length - 1)
            return res
        }
    }

    function __setViewingMonth() {
        let p = root.__parseAnyDate(root.pendingDateStr)
        if (p) {
            const target = new Date(p.year, p.month - 1, 1)
            const today = new Date()
            root.monthShift = (target.getFullYear() - today.getFullYear()) * 12
                + (target.getMonth() - today.getMonth())
        } else {
            root.monthShift = 0
        }
    }

    MouseArea {
        anchors.fill: parent
    }

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
        spacing: 8 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Select Date")
            Layout.leftMargin: 32 * Appearance.effectiveScale
            Layout.topMargin: 24 * Appearance.effectiveScale
            font.pixelSize: Appearance.font.pixelSize.small
            font.weight: Font.DemiBold
            color: Appearance.colors.colSubtext
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 32 * Appearance.effectiveScale
            Layout.rightMargin: 24 * Appearance.effectiveScale
            Layout.topMargin: 16 * Appearance.effectiveScale
            spacing: 0

            StyledText {
                Layout.fillWidth: true
                text: root.selectMode === 0 ? root.__formatHeaderDate(root.pendingDateStr) : I18nService.tr("Enter date")
                font.pixelSize: Math.round(32 * Appearance.effectiveScale)
                font.family: Appearance.font.family.numbers
                font.weight: Font.Normal
                color: Appearance.colors.colOnLayer1
            }

            RippleButton {
                implicitWidth: 36 * Appearance.effectiveScale
                implicitHeight: 36 * Appearance.effectiveScale
                buttonRadius: Appearance.rounding.full
                colBackground: "transparent"
                colBackgroundHover: Appearance.colors.colLayer2Hover
                onClicked: {
                    root.selectMode = root.selectMode === 0 ? 1 : 0
                    if (root.selectMode === 1) {
                        root.inputText = root.__formatDisplayByStyle(root.pendingDateStr)
                        dateInput.forceActiveFocus()
                    }
                }
                contentItem: MaterialSymbol {
                    text: root.selectMode === 0 ? "edit" : "calendar_today"
                    iconSize: 20 * Appearance.effectiveScale
                    horizontalAlignment: Text.AlignHCenter
                    color: Appearance.m3colors.m3onSurfaceVariant
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Appearance.colors.colOutlineVariant
        }

        // ── MODE 0: CALENDAR VIEW ──
        ColumnLayout {
            visible: root.selectMode === 0
            Layout.fillWidth: true
            spacing: 8 * Appearance.effectiveScale

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24 * Appearance.effectiveScale
                Layout.rightMargin: 24 * Appearance.effectiveScale
                spacing: 4 * Appearance.effectiveScale

                RippleButton {
                    implicitHeight: 32 * Appearance.effectiveScale
                    buttonRadius: Appearance.rounding.full
                    colBackground: "transparent"
                    colBackgroundHover: Appearance.colors.colLayer2Hover
                    onClicked: root.yearMode = !root.yearMode
                    contentItem: RowLayout {
                        spacing: 2 * Appearance.effectiveScale
                        StyledText {
                            text: root.__getMonthYearHeader(root.viewingDate)
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.DemiBold
                            color: Appearance.colors.colOnLayer1
                        }
                        MaterialSymbol {
                            text: "arrow_drop_down"
                            iconSize: 18 * Appearance.effectiveScale
                            color: Appearance.colors.colOnLayer1
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                RippleButton {
                    implicitWidth: 32 * Appearance.effectiveScale; implicitHeight: 32 * Appearance.effectiveScale
                    buttonRadius: Appearance.rounding.full
                    colBackground: "transparent"; colBackgroundHover: Appearance.colors.colLayer2Hover
                    enabled: !root.yearMode
                    onClicked: root.monthShift--
                    contentItem: MaterialSymbol {
                        text: "chevron_left"; iconSize: Appearance.font.pixelSize.normal
                        horizontalAlignment: Text.AlignHCenter; color: Appearance.colors.colOnLayer1
                    }
                }
                RippleButton {
                    implicitWidth: 32 * Appearance.effectiveScale; implicitHeight: 32 * Appearance.effectiveScale
                    buttonRadius: Appearance.rounding.full
                    colBackground: "transparent"; colBackgroundHover: Appearance.colors.colLayer2Hover
                    enabled: !root.yearMode
                    onClicked: root.monthShift++
                    contentItem: MaterialSymbol {
                        text: "chevron_right"; iconSize: Appearance.font.pixelSize.normal
                        horizontalAlignment: Text.AlignHCenter; color: Appearance.colors.colOnLayer1
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24 * Appearance.effectiveScale
                Layout.rightMargin: 24 * Appearance.effectiveScale
                spacing: Appearance.sizes.calendarSpacing

                RowLayout {
                    visible: !root.yearMode
                    spacing: Appearance.sizes.calendarSpacing
                    Repeater {
                        model: {
                            const baseDays = [I18nService.tr("Mo"), I18nService.tr("Tu"), I18nService.tr("We"), I18nService.tr("Th"), I18nService.tr("Fr"), I18nService.tr("Sa"), I18nService.tr("Su")]
                            const offset = (root.firstDayOfWeek + 6) % 7
                            let r = []
                            for (let i = 0; i < 7; i++) r.push(baseDays[(i + offset) % 7])
                            return r
                        }
                        delegate: Item {
                            required property string modelData
                            implicitWidth: root.cellSize; implicitHeight: root.cellSize * 0.7
                            StyledText {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: Appearance.font.pixelSize.smaller
                                color: Appearance.colors.colSubtext
                                font.weight: Font.Medium
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: !root.yearMode
                    Layout.fillWidth: true
                    spacing: Appearance.sizes.calendarSpacing

                    Repeater {
                        model: 6
                        delegate: RowLayout {
                            required property int index
                            Layout.fillWidth: true
                            spacing: Appearance.sizes.calendarSpacing

                            Repeater {
                                model: 7
                                delegate: RippleButton {
                                    required property int index
                                    padding: 0
                                    implicitWidth: root.cellSize; implicitHeight: root.cellSize
                                    buttonRadius: implicitHeight / 2

                                    readonly property int row: parent.index
                                    readonly property var cell: root.calendarLayout[row][index]
                                    readonly property string dateStr: cell.today === -1 ? "" :
                                        root.__formatDate(root.viewingDate.getFullYear(), root.viewingDate.getMonth() + 1, cell.day)
                                    readonly property bool isCurrent: cell.today === 1
                                    readonly property bool isPending: dateStr.length > 0 && dateStr === root.pendingDateStr
                                    readonly property bool isSelected: dateStr.length > 0 && dateStr === root.__toCanonicalDateStr(root.currentDateStr)

                                    colBackground: isPending ? Appearance.colors.colPrimary : "transparent"
                                    colBackgroundHover: isPending ? Appearance.colors.colPrimary : Appearance.colors.colLayer2Hover

                                    onClicked: {
                                        if (cell.today === -1) return
                                        root.pendingDateStr = dateStr
                                        root.inputText = root.__formatDisplayByStyle(dateStr)
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: height / 2
                                        border.width: 2 * Appearance.effectiveScale
                                        border.color: Appearance.colors.colPrimary
                                        color: "transparent"
                                        visible: isSelected && !isPending
                                    }

                                    contentItem: StyledText {
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                        text: cell.day.toString()
                                        font.pixelSize: Appearance.font.pixelSize.small
                                        font.family: Appearance.font.family.numbers
                                        font.weight: Font.Normal
                                        color: isPending ? Appearance.m3colors.m3onPrimary
                                            : cell.today === -1 ? Appearance.colors.colOutlineVariant
                                            : Appearance.colors.colOnLayer1
                                    }
                                }
                            }
                        }
                    }
                }

                Item {
                    visible: root.yearMode
                    Layout.fillWidth: true
                    implicitHeight: root.__monthGridTotal + 48 * Appearance.effectiveScale

                    Flickable {
                        id: yearFlickable
                        anchors.fill: parent
                        contentHeight: yearFlow.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        onVisibleChanged: if (visible) {
                            const cy = new Date().getFullYear()
                            const row = Math.floor((cy - root.yearRangeStart) / 4)
                            const rowY = row * (root.cellSize + yearFlow.spacing)
                            contentY = Math.max(0, rowY - (height - root.cellSize) / 2)
                        }

                        Flow {
                            id: yearFlow
                            width: parent.width
                            spacing: 4 * Appearance.effectiveScale

                            Repeater {
                                model: 100

                                delegate: RippleButton {
                                    required property int index
                                    implicitWidth: (yearFlow.width - 3 * yearFlow.spacing) / 4
                                    implicitHeight: root.cellSize
                                    buttonRadius: implicitHeight / 2

                                    readonly property int year: root.yearRangeStart + index
                                    readonly property bool isCurrent: year === new Date().getFullYear()

                                    colBackground: isCurrent ? Appearance.colors.colPrimary : "transparent"
                                    colBackgroundHover: isCurrent ? Appearance.colors.colPrimary : Appearance.colors.colLayer2Hover

                                    onClicked: {
                                        const target = new Date(year, root.viewingDate.getMonth(), 1)
                                        const today = new Date()
                                        root.monthShift = (target.getFullYear() - today.getFullYear()) * 12
                                            + (target.getMonth() - today.getMonth())
                                        root.yearMode = false
                                    }

                                    contentItem: StyledText {
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                        text: year.toString()
                                        font.pixelSize: Appearance.font.pixelSize.small
                                        font.family: Appearance.font.family.numbers
                                        font.weight: Font.Normal
                                        color: isCurrent ? Appearance.m3colors.m3onPrimary : Appearance.colors.colOnLayer1
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── MODE 1: TEXT INPUT VIEW (Polkit StyledTextInput) ──
        ColumnLayout {
            visible: root.selectMode === 1
            Layout.fillWidth: true
            Layout.leftMargin: 24 * Appearance.effectiveScale
            Layout.rightMargin: 24 * Appearance.effectiveScale
            Layout.topMargin: 16 * Appearance.effectiveScale
            Layout.bottomMargin: 20 * Appearance.effectiveScale
            spacing: 8 * Appearance.effectiveScale

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 56 * Appearance.effectiveScale
                radius: 8 * Appearance.effectiveScale
                color: "transparent"
                border.width: 2 * Appearance.effectiveScale
                border.color: !root.inputValid ? Appearance.colors.colError
                    : (dateInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline)

                // Floating "Date" Label
                Rectangle {
                    x: 12 * Appearance.effectiveScale
                    y: -8 * Appearance.effectiveScale
                    implicitWidth: dateLabel.implicitWidth + 8 * Appearance.effectiveScale
                    implicitHeight: dateLabel.implicitHeight
                    color: Appearance.m3colors.m3surfaceContainerHigh

                    StyledText {
                        id: dateLabel
                        anchors.centerIn: parent
                        text: I18nService.tr("Date")
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        font.weight: Font.Medium
                        color: !root.inputValid ? Appearance.colors.colError
                            : (dateInput.input.activeFocus ? Appearance.m3colors.m3primary : Appearance.m3colors.m3outline)
                    }
                }

                StyledTextInput {
                    id: dateInput
                    anchors.fill: parent
                    anchors.leftMargin: 16 * Appearance.effectiveScale
                    anchors.rightMargin: 16 * Appearance.effectiveScale
                    inputRadius: 0
                    backgroundColor: "transparent"
                    borderInactiveWidth: 0
                    showActiveBorder: false
                    leftMargin: 0
                    rightMargin: 0
                    placeholder: root.dateStyle === "YMD" ? "YYYY/MM/DD" : (root.dateStyle === "MDY" ? "MM/DD/YYYY" : "DD/MM/YYYY")
                    font.pixelSize: Appearance.font.pixelSize.normal
                    text: root.inputText
                    color: Appearance.m3colors.m3onSurface

                    property int _lastLen: 0

                    onTextChanged: {
                        if (root.selectMode === 1 && input.activeFocus) {
                            let isDeleting = text.length < _lastLen
                            _lastLen = text.length
                            let formatted = root.__formatTypedDateInput(text, isDeleting)
                            if (formatted !== text) {
                                text = formatted
                            }
                            let p = root.__parseAnyDate(text)
                            if (p) {
                                root.pendingDateStr = root.__formatDate(p.year, p.month, p.day)
                                root.__setViewingMonth()
                            }
                        }
                        root.inputValid = root.__isValidInput(text)
                    }
                }
            }

            StyledText {
                visible: !root.inputValid
                Layout.fillWidth: true
                text: root.__errorText()
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.colors.colError
                horizontalAlignment: Text.AlignLeft
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: 24 * Appearance.effectiveScale
            Layout.bottomMargin: 24 * Appearance.effectiveScale
            spacing: 8 * Appearance.effectiveScale

            Item { Layout.fillWidth: true }

            RippleButton {
                implicitHeight: 36 * Appearance.effectiveScale
                buttonRadius: Appearance.rounding.full
                colBackground: "transparent"; colBackgroundHover: Appearance.colors.colLayer2Hover
                onClicked: root.cancelled()
                contentItem: StyledText {
                    text: I18nService.tr("Cancel"); font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.Medium; color: Appearance.colors.colPrimary
                }
            }

            RippleButton {
                implicitHeight: 36 * Appearance.effectiveScale
                buttonRadius: Appearance.rounding.full
                colBackground: "transparent"; colBackgroundHover: Appearance.colors.colLayer2Hover
                enabled: root.selectMode !== 1 || root.inputValid
                opacity: root.selectMode === 1 && !root.inputValid ? 0.4 : 1
                onClicked: {
                    if (root.pendingDateStr) {
                        let displayStr = root.__formatDisplayByStyle(root.pendingDateStr)
                        root.currentDateStr = displayStr
                        root.dateSelected(displayStr)
                    }
                }
                contentItem: StyledText {
                    text: I18nService.tr("OK"); font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.Medium; color: Appearance.colors.colPrimary
                }
            }
        }
    }
}
