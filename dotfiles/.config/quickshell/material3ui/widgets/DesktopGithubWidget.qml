import "../core"
import "../core/functions" as Functions
import "../services"
import "."
import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

Item {
    id: root

    property var cfg: Config.ready ? Config.options.appearance.githubWidget : null
    property string sizeMode: cfg ? cfg.sizeMode : "2x2"
    property bool interactive: true

    HoverHandler {
        id: widgetHoverHandler
    }

    // ── Grid dimensions ──
    readonly property real baseWidth: 132 * Appearance.effectiveScale
    readonly property real baseHeight: 108 * Appearance.effectiveScale
    readonly property real gap: 12 * Appearance.effectiveScale
    readonly property real width2x1: (baseWidth * 2) + gap
    readonly property real width2x2: (baseWidth * 2) + gap
    readonly property real height2x1: baseHeight
    readonly property real height2x2: (baseHeight * 2) + gap

    implicitWidth: sizeMode === "2x1" ? width2x1 : width2x2
    implicitHeight: sizeMode === "2x1" ? height2x1 : height2x2

    Behavior on implicitWidth {
        NumberAnimation { duration: 250; easing.bezierCurve: [0.2, 0, 0, 1] }
    }
    Behavior on implicitHeight {
        NumberAnimation { duration: 250; easing.bezierCurve: [0.2, 0, 0, 1] }
    }

    function getModeForSize(targetWidth, targetHeight) {
        let midH = (height2x1 + height2x2) / 2
        if (targetHeight >= midH) return "2x2"
        return "2x1"
    }

    // ── Palette ──
    readonly property color bg: Appearance.colors.colSecondaryContainer
    readonly property color fg: Appearance.colors.colOnSecondaryContainer
    readonly property color accent: Appearance.colors.colTertiary
    readonly property color onAccent: Appearance.colors.colOnTertiary
    readonly property color muted: Functions.ColorUtils.applyAlpha(Appearance.colors.colOnSecondaryContainer, 0.55)

    readonly property string displayName: GitHubService.user.login
        ? ("@" + GitHubService.user.login)
        : (GitHubService.loading ? "…" : "Not connected")

    readonly property bool showStreak: GitHubService.streak > 0
    readonly property string streakValue: GitHubService.streak > 99 ? "99+" : ("" + GitHubService.streak)
    readonly property string streakLabel: root.streakValue + " Streak"

    readonly property real gemScale: 0.65
    readonly property real heatRefScale: 0.70
    readonly property real gemInsetW: 0.80
    readonly property real gemInsetH: 0.50

    property bool showingSettings: false

    // Flip card scale and animation
    transform: Scale {
        id: flipScale
        origin.x: root.width / 2
        origin.y: root.height / 2
        xScale: 1
    }

    SequentialAnimation {
        id: flipAnim
        NumberAnimation {
            target: flipScale; property: "xScale"
            to: 0; duration: 150; easing.type: Easing.InQuad
        }
        ScriptAction {
            script: root.showingSettings = !root.showingSettings
        }
        NumberAnimation {
            target: flipScale; property: "xScale"
            to: 1; duration: 150; easing.type: Easing.OutQuad
        }
    }

    function toggleFlip() { flipAnim.start() }
    function setSize(mode) { if (cfg) cfg.sizeMode = mode }

    // ── Data ──
    function levelOf(v) {
        if (v <= 0) return 0
        if (v <= 2) return 1
        if (v <= 4) return 2
        if (v <= 7) return 3
        return 4
    }

    // Current month as a Sun-first 42-cell calendar, -1 = day outside the month.
    property var monthData: root.buildMonthData()

    function buildMonthData() {
        var now = new Date()
        var y = now.getFullYear()
        var m = now.getMonth()
        var firstDow = new Date(y, m, 1).getDay()
        var daysInMonth = new Date(y, m + 1, 0).getDate()

        var offset = -1
        var gs = GitHubService.gridStartDate
        if (gs) {
            var p = gs.split("-")
            var gStart = new Date(+p[0], +p[1] - 1, +p[2])
            offset = Math.round((new Date(y, m, 1) - gStart) / 86400000)
        }

        var gd = GitHubService.contributionDays
        var cells = []
        for (var i = 0; i < 42; i++) cells.push(-1)

        var sum = 0
        for (var d = 0; d < daysInMonth; d++) {
            var gi = offset >= 0 ? offset + d : -1
            var v = 0
            if (gi >= 0 && gi < gd.length) v = gd[gi]
            cells[firstDow + d] = v
            sum += v
        }

        return {
            cells: cells,
            sum: sum,
            rows: Math.ceil((firstDow + daysInMonth) / 7)
        }
    }

    readonly property string monthYearLabel: {
        var now = new Date()
        var style = Config.ready && Config.options.time ? Config.options.time.dateStyle : "DMY"
        return style === "YMD"
            ? Qt.formatDate(now, "yyyy MMMM")
            : Qt.formatDate(now, "MMMM yyyy")
    }

    function heatColor(level, onSolid, ghost) {
        var base, ramp
        if (onSolid) {
            base = Functions.ColorUtils.applyAlpha(root.onAccent, 0.08)
            ramp = Functions.ColorUtils.applyAlpha(root.onAccent, 0.32 + level * 0.18)
        } else {
            base = Functions.ColorUtils.applyAlpha(root.fg, 0.10)
            ramp = Functions.ColorUtils.applyAlpha(root.accent, 0.30 + level * 0.18)
        }
        if (ghost || level <= 0) return base
        return ramp
    }

    function fmt(n) { return n.toLocaleString(Qt.locale(), 'f', 0) }

    function heatCellSize() {
        let gap = 3 * Appearance.effectiveScale
        let rows = Math.max(1, root.monthData.rows)
        let gemW = root.width * root.heatRefScale * root.gemInsetW
        let gemH = root.height * root.heatRefScale * root.gemInsetH
        let availW = gemW - (6 * gap)
        let availH = gemH - ((rows - 1) * gap)
        let cw = availW / 7 / Appearance.effectiveScale
        let ch = availH / rows / Appearance.effectiveScale
        return Math.max(3, Math.min(cw, ch))
    }

    // ── Components ──
    component MonthHeatmap: Grid {
        id: mGrid
        property var cellsModel: []
        property real cell: 10
        property bool onSolid: false
        columns: 7
        columnSpacing: 3 * Appearance.effectiveScale
        rowSpacing: 3 * Appearance.effectiveScale

        Repeater {
            model: cellsModel
            delegate: Rectangle {
                width: mGrid.cell * Appearance.effectiveScale
                height: mGrid.cell * Appearance.effectiveScale
                radius: Math.max(1.5, 2.5 * Appearance.effectiveScale)
                color: root.heatColor(root.levelOf(modelData < 0 ? 0 : modelData), mGrid.onSolid, modelData < 0)
            }
        }
    }

    // ── Main card ──
    Rectangle {
        id: card
        anchors.fill: parent
        radius: 28 * Appearance.effectiveScale
        color: root.bg

        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: card.width
                height: card.height
                radius: card.radius
            }
        }

        // ── PAGE 1: View Mode ──
        Item {
            anchors.fill: parent
            visible: !root.showingSettings

            Loader {
                anchors.fill: parent
                sourceComponent: {
                    if (root.sizeMode === "2x1") return mode2x1Layout
                    return mode2x2Layout
                }
            }
        }

        // ── [2x1: username + stars hero | mini month heatmap panel] ──
        Component {
            id: mode2x1Layout
            Item {
                anchors.fill: parent

                RowLayout {
                    anchors.fill: parent
                    anchors.topMargin: 14 * Appearance.effectiveScale
                    anchors.leftMargin: 14 * Appearance.effectiveScale
                    anchors.bottomMargin: 14 * Appearance.effectiveScale
                    anchors.rightMargin: 0
                    spacing: 12 * Appearance.effectiveScale

                    // Left: identity + stars
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 2 * Appearance.effectiveScale

                        StyledText {
                            Layout.fillWidth: true
                            text: root.displayName
                            font.pixelSize: Appearance.font.pixelSize.normal
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.Bold
                            color: root.fg
                            elide: Text.ElideRight
                        }

                        Item { Layout.fillHeight: true }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 5 * Appearance.effectiveScale

                            MaterialSymbol {
                                iconSize: 16 * Appearance.effectiveScale
                                text: root.showStreak ? "local_fire_department" : "star"
                                color: root.accent
                                Layout.alignment: Qt.AlignBaseline
                            }
                            StyledText {
                                text: GitHubService.loading ? "…" : (root.showStreak ? (root.streakValue + " Streak") : root.fmt(GitHubService.totalStars))
                                font.pixelSize: root.showStreak ? Appearance.font.pixelSize.normal : 26 * Appearance.effectiveScale
                                font.weight: Font.Bold
                                color: root.accent
                                Layout.alignment: Qt.AlignBaseline
                            }
                        }
                    }

                    MaterialShape {
                        Layout.preferredWidth: 104 * Appearance.effectiveScale
                        Layout.fillHeight: true
                        shapeString: "Gem"
                        color: root.accent

                        MonthHeatmap {
                            anchors.centerIn: parent
                            cellsModel: root.monthData.cells
                            cell: 4
                            onSolid: true
                        }
                    }
                }
            }
        }

        // ── [2x2: username + stars header, month heatmap panel as hero] ──
        Component {
            id: mode2x2Layout
            Item {
                anchors.fill: parent

MaterialShape {
                    id: bgShape
                    width: parent.width * root.gemScale
                    height: parent.height * root.gemScale
                    anchors.centerIn: parent
                    shapeString: "Gem"
                    color: root.accent
                    z: 0
                }

                MonthHeatmap {
                    anchors.centerIn: bgShape
                    z: 2
                    cellsModel: root.monthData.cells
                    cell: root.heatCellSize()
                    onSolid: true
                }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16 * Appearance.effectiveScale
                        spacing: 12 * Appearance.effectiveScale
                        z: 1

                    // Header
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10 * Appearance.effectiveScale

                        StyledText {
                            Layout.fillWidth: true
                            text: root.displayName
                            font.pixelSize: Appearance.font.pixelSize.large
                            font.family: Appearance.font.family.title
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.Bold
                            color: root.fg
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            spacing: 5 * Appearance.effectiveScale

                            MaterialSymbol {
                                iconSize: 16 * Appearance.effectiveScale
                                text: root.showStreak ? "local_fire_department" : "star"
                                color: root.accent
                                Layout.alignment: Qt.AlignBaseline
                            }
                            StyledText {
                                text: GitHubService.loading ? "…" : (root.showStreak ? root.streakLabel : root.fmt(GitHubService.totalStars))
                                font.pixelSize: Appearance.font.pixelSize.normal
                                font.weight: Font.Bold
                                color: root.accent
                                Layout.alignment: Qt.AlignBaseline
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        StyledText {
                            text: (GitHubService.loading ? "…" : root.fmt(root.monthData.sum)) + " commit in"
                            font.pixelSize: Appearance.font.pixelSize.smallest
                            color: root.muted
                        }

                        Item { Layout.fillWidth: true }

                        StyledText {
                            text: root.monthYearLabel
                            font.pixelSize: Appearance.font.pixelSize.smallest
                            color: root.muted
                        }
                    }
                }
            }
        }

        // ── PAGE 2: Flip Settings Mode ──
        Flickable {
            anchors.fill: parent
            visible: root.showingSettings
            contentHeight: settingsCol.implicitHeight + (20 * Appearance.effectiveScale)
            clip: true
            interactive: true

            ColumnLayout {
                id: settingsCol
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    leftMargin: 12 * Appearance.effectiveScale
                    rightMargin: 12 * Appearance.effectiveScale
                    topMargin: 10 * Appearance.effectiveScale
                }
                spacing: 8 * Appearance.effectiveScale

                StyledText {
                    Layout.fillWidth: true
                    text: "Config"
                    font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.Bold
                    color: Appearance.colors.colPrimary
                }

                StyledText {
                    Layout.fillWidth: true
                    text: "Username"
                    font.pixelSize: Appearance.font.pixelSize.smallest
                    color: Appearance.m3colors.m3onSurface
                }
                StyledTextInput {
                    id: usernameInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30 * Appearance.effectiveScale
                    font.pixelSize: Appearance.font.pixelSize.smallest
                    text: Config.ready && Config.options.github ? Config.options.github.githubUsername : ""
                    placeholder: "octocat"
                    color: Appearance.m3colors.m3onSurface
                    inputRadius: 8
                    leftMargin: 10
                    rightMargin: 10
                    onAccepted: root.saveCredentials()
                    onEditingFinished: root.saveCredentials()
                }

                StyledText {
                    Layout.fillWidth: true
                    text: "Personal Access Token"
                    font.pixelSize: Appearance.font.pixelSize.smallest
                    color: Appearance.m3colors.m3onSurface
                }
                StyledTextInput {
                    id: tokenInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30 * Appearance.effectiveScale
                    font.pixelSize: Appearance.font.pixelSize.smallest
                    text: Config.ready && Config.options.github ? Config.options.github.githubToken : ""
                    placeholder: "ghp_xxxxxxxxxxxx"
                    color: Appearance.m3colors.m3onSurface
                    inputRadius: 8
                    leftMargin: 10
                    rightMargin: 10
                    echoMode: TextInput.Password
                    onAccepted: root.saveCredentials()
                    onEditingFinished: root.saveCredentials()
                }
            }
        }
    // ── Error status shown on the card face ──
        StyledText {
            anchors {
                bottom: parent.bottom
                horizontalCenter: parent.horizontalCenter
                bottomMargin: 4 * Appearance.effectiveScale
            }
            visible: GitHubService.errorMessage !== "" && !GitHubService.loading
            text: GitHubService.errorMessage
            font.pixelSize: Appearance.font.pixelSize.smallest
            color: "#E57373"
            opacity: 0.9
        }
    }

    // Settings button (appears on hover, hidden when locked)
    Item {
        width: 24 * Appearance.effectiveScale
        height: 24 * Appearance.effectiveScale
        z: 100
        visible: cfg ? !cfg.locked : true
        opacity: widgetHoverHandler.hovered ? 0.9 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        anchors {
            top: parent.top
            right: parent.right
            topMargin: 8 * Appearance.effectiveScale
            rightMargin: 8 * Appearance.effectiveScale
        }

        Rectangle {
            anchors.fill: parent
            radius: 12 * Appearance.effectiveScale
            color: Appearance.colors.colTertiary

            MaterialSymbol {
                anchors.centerIn: parent
                text: "settings"
                iconSize: 14 * Appearance.effectiveScale
                color: Appearance.colors.colOnTertiary
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleFlip()
            }
        }
    }

    // Resize Handle (supports dragging between 2x1 and 2x2)
    Rectangle {
        id: resizeHandle
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: -8 * Appearance.effectiveScale
        width: 24 * Appearance.effectiveScale
        height: 24 * Appearance.effectiveScale
        radius: 8 * Appearance.effectiveScale
        color: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer
        z: 100

        opacity: root.interactive && (cfg && !cfg.locked) && (widgetHoverHandler.hovered || resizeArea.containsMouse || resizeArea.pressed) ? 0.9 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        MaterialSymbol {
            anchors.centerIn: parent
            text: "open_in_full"
            iconSize: 15 * Appearance.effectiveScale
            color: Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer
        }

        MouseArea {
            id: resizeArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            preventStealing: true

            property real startWidth: 0
            property real startHeight: 0
            property real startGlobalX: 0
            property real startGlobalY: 0

            onPressed: (mouse) => {
                startWidth = root.width;
                startHeight = root.height;
                let p = mapToItem(null, mouse.x, mouse.y);
                startGlobalX = p.x;
                startGlobalY = p.y;
            }

            onPositionChanged: (mouse) => {
                if (!pressed) return;
                let p = mapToItem(null, mouse.x, mouse.y);
                let deltaX = p.x - startGlobalX;
                let deltaY = p.y - startGlobalY;
                let targetWidth = startWidth + deltaX;
                let targetHeight = startHeight + deltaY;

                let targetMode = root.getModeForSize(targetWidth, targetHeight);
                if (targetMode !== root.sizeMode) {
                    if (cfg) {
                        cfg.sizeMode = targetMode;
                    }
                }
            }
        }
    }

    function saveCredentials() {
        if (!Config.ready || !Config.options.github) return
        var user = usernameInput.text.trim()
        if (user !== "") Config.options.github.githubUsername = user
        Config.options.github.githubToken = tokenInput.text.trim()
    }
}