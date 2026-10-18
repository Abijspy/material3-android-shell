import "../core"
import "../services"
import QtQuick
import QtQuick.Layouts
import Quickshell
import Quickshell.Io
import Quickshell.Wayland

Item {
    id: root
    width: mainLayout.width
    height: mainLayout.height

    // ── Properties ──
    property bool interactive: true

    // Configuration shortcuts
    readonly property var cfg: Config.options.appearance.atAGlance
    readonly property string fontFamily: cfg.fontFamily !== "" ? cfg.fontFamily : Appearance.font.family.main
    readonly property int fontSize: cfg.fontSize
    
    // Time & Date bindings
    readonly property int currentHour: DateTime.hours
    readonly property string dateString: {
        var dummy = DateTime.currentDate;
        var lang = I18nService.languageCode || "en_US";
        try {
            return DateTime.now.toLocaleDateString(Qt.locale(lang), "dddd, MMMM d");
        } catch (e) {
            return Qt.formatDate(DateTime.now, "dddd, MMMM d");
        }
    }
    
    // State
    property var quotesData: ({})
    property string currentGreeting: "Good day"
    property string _displayedQuote: ""
    property string _pendingQuote: ""
    property real _quoteContentOpacity: 1

    // Determine time period
    property string timePeriod: {
        if (currentHour >= 5 && currentHour < 12) return "morning";
        if (currentHour >= 12 && currentHour < 17) return "afternoon";
        if (currentHour >= 17 && currentHour < 21) return "evening";
        if (currentHour >= 21) return "night";
        return "midnight"; // 00:00 to 04:59
    }

    // ── Schedule Integration ──
    property var scheduleEvents: ScheduleService.events
    property int _minuteTrigger: DateTime.minutes
    property var todayEvents: []
    property var nextEvent: null
    property int _scheduleVersion: 0
    property string _displayedScheduleTitle: ""
    property string _displayedScheduleDesc: ""
    property string _pendingScheduleTitle: ""
    property string _pendingScheduleDesc: ""
    property real _scheduleContentOpacity: 1

    // ── Bluetooth Integration ──
    property var btDevices: BluetoothStatus.connectedDevices
    property string _displayedBluetoothText: ""
    property string _pendingBluetoothText: ""
    property real _bluetoothContentOpacity: 1

    onScheduleEventsChanged: updateScheduleInfo()
    on_MinuteTriggerChanged: { updateScheduleInfo(); updateBluetoothInfo(); }
    onBtDevicesChanged: updateBluetoothInfo()
    property var _reminderTracker: ReminderService.reminders
    on_ReminderTrackerChanged: updateScheduleInfo()

    Timer { id: scheduleFadeTimer; interval: 200; onTriggered: { _displayedScheduleTitle = _pendingScheduleTitle; _displayedScheduleDesc = _pendingScheduleDesc; _scheduleContentOpacity = 1; } }
    Timer { id: quoteFadeTimer; interval: 200; onTriggered: { _displayedQuote = _pendingQuote; _quoteContentOpacity = 1; } }
    Timer { id: bluetoothFadeTimer; interval: 200; onTriggered: { _displayedBluetoothText = _pendingBluetoothText; _bluetoothContentOpacity = 1; } }

    function updateBluetoothInfo() {
        const devices = BluetoothStatus.connectedDevices.filter(d => d.batteryAvailable);
        let text = "";
        if (devices.length === 1) {
            text = devices[0].name + " \u00b7 " + Math.round(devices[0].battery * 100) + "%";
        } else if (devices.length > 1) {
            let parts = [];
            for (let i = 0; i < devices.length; i++)
                parts.push(Math.round(devices[i].battery * 100) + "%");
            text = devices.length + " devices \u00b7 " + parts.join(", ");
        }

        const wasVisible = _displayedBluetoothText !== "";
        const isVisible = text !== "";

        if (wasVisible && isVisible && text !== _displayedBluetoothText) {
            _pendingBluetoothText = text;
            _bluetoothContentOpacity = 0;
            bluetoothFadeTimer.restart();
        } else {
            _displayedBluetoothText = text;
            _bluetoothContentOpacity = isVisible ? 1 : 0;
        }
    }

    function updateScheduleInfo() {
        // DateTime.now is boundary-aligned to SystemClock; new Date() can lag a
        // minute right after the trigger (SystemClock fires ±50ms early).
        const now = DateTime.now;
        const nowDateStr = Qt.formatDate(now, "yyyy-MM-dd");

        // Recurrence matching delegated to ScheduleService.eventOccursOn — single source of truth.
        let events = ScheduleService.events.filter(ev => ScheduleService.eventOccursOn(ev, nowDateStr))
            .sort((a, b) => a.time.localeCompare(b.time));

        todayEvents = events;

        const UPCOMING_WINDOW = 120;
        const nowMs = now.getHours() * 60 + now.getMinutes();
        let next = null;
        for (const ev of events) {
            // For multi-day once events, only show "Up next" on the first day
            if (ev.recurrence === "once" && ev.endDate && nowDateStr !== ev.date) continue;

            const [h, m] = ev.time.split(":").map(Number);
            const startMs = h * 60 + m;
            const isUpcomingInWindow = nowMs < startMs && (startMs - nowMs) <= UPCOMING_WINDOW;
            if (isUpcomingInWindow) {
                next = ev;
                break;
            }
        }

        // If no upcoming schedule event, check upcoming reminders (within 2 hours, not yet fired)
        let nextReminder = null;
        if (!next) {
            for (const r of ReminderService.reminders) {
                if (r.fired) continue;
                const [rh, rm] = r.time.split(":").map(Number);
                const rMs = rh * 60 + rm;
                if (nowMs < rMs && (rMs - nowMs) <= UPCOMING_WINDOW) {
                    nextReminder = r;
                    break;
                }
            }
        }

        const wasVisible = nextEvent !== null;
        const newNext = next ? {
            title: next.title,
            time: next.time,
            endTime: next.endTime,
            description: next.description,
            date: next.date,
            endDate: next.endDate
        } : (nextReminder ? {
            _isReminder: true,
            title: nextReminder.text,
            time: nextReminder.time,
            description: nextReminder.linkedTitle || ""
        } : null);

        let label = "";
        let desc = "";
        if (newNext) {
            if (newNext._isReminder) {
                label = I18nService.tr("Reminder: ") + newNext.title + " \u00b7 " + newNext.time;
                desc = newNext.description || "";
            } else {
                let dayInfo = "";
                if (newNext.endDate && newNext.date) {
                    const start = new Date(newNext.date + "T00:00:00");
                    const end = new Date(newNext.endDate + "T00:00:00");
                    const dayDiff = Math.round((end - start) / 86400000) + 1;
                    if (dayDiff > 1) {
                        const todayDiff = Math.round((new Date(nowDateStr + "T00:00:00") - start) / 86400000) + 1;
                        dayInfo = " \u00b7 Day " + todayDiff + "/" + dayDiff;
                    }
                }
                let timeStr = newNext.time;
                if (newNext.endTime) timeStr += "\u2013" + newNext.endTime;
                label = "Up next: " + newNext.title + dayInfo + " \u00b7 " + timeStr;
                desc = newNext.description || "";
            }
        }

        const textChanged = wasVisible && newNext && (label !== _displayedScheduleTitle || desc !== _displayedScheduleDesc);

        if (textChanged) {
            _pendingScheduleTitle = label;
            _pendingScheduleDesc = desc;
            _scheduleContentOpacity = 0;
            nextEvent = newNext;
            _scheduleVersion++;
            scheduleFadeTimer.restart();
        } else if (wasVisible && newNext && !textChanged) {
            _displayedScheduleTitle = label;
            _displayedScheduleDesc = desc;
            nextEvent = newNext;
            _scheduleVersion++;
        } else {
            _displayedScheduleTitle = label;
            _displayedScheduleDesc = desc;
            _scheduleContentOpacity = newNext ? 1 : 0;
            nextEvent = newNext;
            _scheduleVersion++;
        }
    }

    Component.onCompleted: { updateScheduleInfo(); updateBluetoothInfo(); }

    // Colors
    function getColorForStyle(style) {
        switch (style) {
            case "primary": return Appearance.colors.colPrimary;
            case "secondary": return Appearance.colors.colSecondary;
            case "tertiary": return Appearance.colors.colTertiary;
            case "error": return Appearance.colors.colError;
            case "onSurface": return Appearance.m3colors.m3onSurface;
            case "surface": return Appearance.m3colors.m3surface;
            case "onLayer0": return Appearance.colors.colOnLayer0;
            case "onLayer1": return Appearance.colors.colOnLayer1;
            case "surfaceContainerHigh": return Appearance.m3colors.m3surfaceContainerHigh;
            default: return Appearance.colors.colPrimary;
        }
    }

    property color greetingColor: getColorForStyle(cfg.greetingColorStyle)
    property color dateColor: getColorForStyle(cfg.dateColorStyle)
    property color quoteColor: getColorForStyle(cfg.quoteColorStyle)

    // Wall-clock aligned 10-minute tick via property binding (no drift, no double-fire)
    readonly property int _tenMinuteBlock: Math.floor(DateTime.minutes / 10)
    on_TenMinuteBlockChanged: {
        updateGreetingOnly();
        updateQuoteOnly();
    }

    readonly property string _quotesLangFile: {
        let code = I18nService.languageCode || "en_US";
        return "translations/quotes_" + code + ".json";
    }

    // Load Quotes JSON via Process dynamically matching active language
    Process {
        id: quotesLoader
        command: ["bash", "-c", `
            file="${Quickshell.shellPath(root._quotesLangFile)}"
            if [ -f "$file" ]; then
                cat "$file"
            else
                cat "${Quickshell.shellPath("translations/quotes_en_US.json")}" 2>/dev/null || cat "${Quickshell.shellPath("data/quotes.json")}"
            fi
        `]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    root.quotesData = JSON.parse(this.text);
                    root.updateText();
                } catch(e) {
                    console.error("Failed to parse quotes:", e);
                }
            }
        }
    }

    Connections {
        target: I18nService
        function onLanguageCodeChanged() {
            quotesLoader.running = false;
            quotesLoader.running = true;
        }
    }

    onTimePeriodChanged: updateText()

    function updateQuoteOnly() {
        if (!quotesData) return;
        let quotesList = quotesData[timePeriod] || [];
        let newQuote = "";
        if (quotesList.length > 0) {
            newQuote = quotesList[Math.floor(Math.random() * quotesList.length)];
        } else if (quotesData["general"] && quotesData["general"].length > 0) {
            newQuote = quotesData["general"][Math.floor(Math.random() * quotesData["general"].length)];
        }
        if (!newQuote) return;
        if (nextEvent === null && _displayedQuote !== "") {
            _pendingQuote = newQuote;
            _quoteContentOpacity = 0;
            quoteFadeTimer.restart();
        } else {
            _displayedQuote = newQuote;
            _quoteContentOpacity = 1;
        }
    }

    // Pick a random greeting for the current period from quotes.json ("greetings").
    // Falls back to a sensible default if the data isn't loaded yet.
    function updateGreetingOnly() {
        let greetings = quotesData && quotesData["greetings"] ? quotesData["greetings"][timePeriod] : null;
        if (greetings && greetings.length > 0) {
            currentGreeting = greetings[Math.floor(Math.random() * greetings.length)];
        } else {
            // Fallbacks while JSON is still loading
            if (timePeriod === "morning") currentGreeting = "Good morning";
            else if (timePeriod === "afternoon") currentGreeting = "Good afternoon";
            else if (timePeriod === "evening") currentGreeting = "Good evening";
            else if (timePeriod === "night") currentGreeting = "Good night";
            else currentGreeting = "Solemn midnight";
        }
    }

    function updateText() {
        if (!quotesData) return;

        // Greeting (random variation per period)
        updateGreetingOnly();

        // Quotes
        updateQuoteOnly();
    }

    // ── Layout ──
    ColumnLayout {
        id: mainLayout
        width: cfg.customWidth > 0 ? cfg.customWidth : implicitWidth
        spacing: 8 * Appearance.effectiveScale

        StyledText {
            visible: cfg.showGreeting
            text: currentGreeting + "."
            font.pixelSize: Math.round(fontSize * 1.2 * Appearance.effectiveScale)
            font.family: fontFamily
            font.weight: Font.DemiBold
            color: greetingColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: cfg.customWidth > 0
            Layout.alignment: cfg.alignment === "center" ? Qt.AlignHCenter : (cfg.alignment === "right" ? Qt.AlignRight : Qt.AlignLeft)
            horizontalAlignment: cfg.alignment === "center" ? Text.AlignHCenter : (cfg.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
        }

        StyledText {
            visible: cfg.showDate
            text: I18nService.tr("It's %1").replace("%1", dateString)
            font.pixelSize: Math.round(fontSize * Appearance.effectiveScale)
            font.family: fontFamily
            color: dateColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: cfg.customWidth > 0
            Layout.alignment: cfg.alignment === "center" ? Qt.AlignHCenter : (cfg.alignment === "right" ? Qt.AlignRight : Qt.AlignLeft)
            horizontalAlignment: cfg.alignment === "center" ? Text.AlignHCenter : (cfg.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
        }

        // ── Content stack: schedule, bluetooth, quote — di posisi yang sama ──
        Item {
            Layout.fillWidth: cfg.customWidth > 0
            implicitHeight: Math.max(scheduleContent._currentHeight, btContent._currentHeight, quoteContent._currentHeight)

            // Schedule (priority 1)
            ColumnLayout {
                id: scheduleContent
                anchors { top: parent.top; left: parent.left; right: parent.right }
                spacing: 2 * Appearance.effectiveScale
                opacity: cfg.showQuote && nextEvent !== null ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 200 } }

                readonly property real _currentHeight: visible ? implicitHeight : 0

                StyledText {
                    text: _displayedScheduleTitle
                    opacity: _scheduleContentOpacity
                    font.pixelSize: Math.round((fontSize * 0.8) * Appearance.effectiveScale)
                    font.family: fontFamily
                    color: quoteColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.maximumWidth: cfg.customWidth > 0 ? cfg.customWidth : 400 * Appearance.effectiveScale
                    horizontalAlignment: cfg.alignment === "center" ? Text.AlignHCenter : (cfg.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }

                StyledText {
                    text: _displayedScheduleDesc
                    opacity: _scheduleContentOpacity * 0.7
                    font.pixelSize: Math.round((fontSize * 0.65) * Appearance.effectiveScale)
                    font.family: fontFamily
                    font.italic: true
                    color: dateColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.maximumWidth: cfg.customWidth > 0 ? cfg.customWidth : 400 * Appearance.effectiveScale
                    horizontalAlignment: cfg.alignment === "center" ? Text.AlignHCenter : (cfg.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }
            }

            // Bluetooth (priority 2 — when no schedule)
            RowLayout {
                id: btContent
                anchors { top: parent.top; left: parent.left; right: parent.right }
                spacing: 8 * Appearance.effectiveScale
                opacity: cfg.showQuote && nextEvent === null && _displayedBluetoothText !== "" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 200 } }

                readonly property real _currentHeight: visible ? implicitHeight : 0

                StyledText {
                    text: _displayedBluetoothText
                    opacity: _bluetoothContentOpacity
                    font.pixelSize: Math.round((fontSize * 0.8) * Appearance.effectiveScale)
                    font.family: fontFamily
                    color: quoteColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.maximumWidth: cfg.customWidth > 0 ? cfg.customWidth : 400 * Appearance.effectiveScale
                    horizontalAlignment: cfg.alignment === "center" ? Text.AlignHCenter : (cfg.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }
            }

            // Quote (idle, priority 3 — when no schedule and no bt)
            RowLayout {
                id: quoteContent
                anchors { top: parent.top; left: parent.left; right: parent.right }
                spacing: 8 * Appearance.effectiveScale
                opacity: cfg.showQuote && nextEvent === null && _displayedBluetoothText === "" && _displayedQuote !== "" ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 200 } }

                readonly property real _currentHeight: visible ? implicitHeight : 0

                StyledText {
                    text: _displayedQuote
                    opacity: _quoteContentOpacity
                    font.pixelSize: Math.round((fontSize * 0.8) * Appearance.effectiveScale)
                    font.family: fontFamily
                    color: quoteColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Layout.maximumWidth: cfg.customWidth > 0 ? cfg.customWidth : 400 * Appearance.effectiveScale
                    horizontalAlignment: cfg.alignment === "center" ? Text.AlignHCenter : (cfg.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                }
            }
        }
    }

    HoverHandler {
        id: widgetHoverHandler
    }

    // ── Resize Handle ──
    Rectangle {
        id: resizeHandle
        visible: root.interactive && !cfg.locked && (widgetHoverHandler.hovered || resizeArea.containsMouse)
        width: 24 * Appearance.effectiveScale
        height: 24 * Appearance.effectiveScale
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: -8 * Appearance.effectiveScale
        color: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer
        radius: 8 * Appearance.effectiveScale

        MaterialSymbol {
            anchors.centerIn: parent
            text: "swap_horiz"
            iconSize: 15 * Appearance.effectiveScale
            color: Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer
        }

        MouseArea {
            id: resizeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            
            property real startGlobalX
            property real startWidth
            
            onPressed: (mouse) => {
                let globalPos = mapToItem(null, mouse.x, mouse.y)
                startGlobalX = globalPos.x
                startWidth = root.width
            }
            
            onPositionChanged: (mouse) => {
                if (pressed) {
                    let globalPos = mapToItem(null, mouse.x, mouse.y)
                    let deltaX = globalPos.x - startGlobalX
                    let newWidth = Math.max(100 * Appearance.effectiveScale, startWidth + deltaX)
                    Config.options.appearance.atAGlance.customWidth = newWidth
                }
            }
        }
    }
}
