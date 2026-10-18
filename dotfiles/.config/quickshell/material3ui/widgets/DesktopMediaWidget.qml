import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import QtQuick.Controls
import "../core"
import "../core/functions" as Functions
import "../services"
import "."

Item {
    id: root
    property var cfg: Config.ready ? Config.options.appearance.mediaWidget : null
    property string sizeMode: cfg ? (cfg.sizeMode || "3x2") : "3x2"

    readonly property real baseWidth: 132 * Appearance.effectiveScale
    readonly property real gap: 12 * Appearance.effectiveScale

    readonly property real width2x2: (baseWidth * 2) + gap
    readonly property real width3x2: (baseWidth * 3) + (gap * 2)

    function getModeForWidth(targetWidth) {
        let mid = (width2x2 + width3x2) / 2;
        if (targetWidth < mid) return "2x2";
        return "3x2";
    }

    implicitWidth: sizeMode === "2x2" ? width2x2 : width3x2
    implicitHeight: 228 * Appearance.effectiveScale
    width: implicitWidth
    height: implicitHeight

    Behavior on width {
        NumberAnimation {
            duration: 250
            easing.bezierCurve: Appearance.animation?.elementResize?.numberAnimation?.easing?.bezierCurve || [0.2, 0, 0, 1]
        }
    }

    HoverHandler {
        id: widgetHoverHandler
    }

    onSizeModeChanged: {
        if (sizeMode === "2x2") {
            viewLyrics = false;
        }
    }

    property bool showLyrics: Config.options.appearance.mediaWidget.showLyrics
    property bool viewLyrics: false

    // Flip settings (like DesktopCurrencyWidget)
    property bool showingSettings: false

    // Widget style: "default" (original) | "art" (full-bleed album art)
    readonly property string widgetStyle: (cfg && cfg.style) ? cfg.style : "default"
    readonly property bool artMode: widgetStyle === "art"

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

    onViewLyricsChanged: {
        LyricsService.desktopWidgetLyricsActive = viewLyrics;
        if (viewLyrics) {
            LyricsService.restartLyrics();
        }
    }

    // Main Card Background
    Rectangle {
        id: bgCard
        anchors.fill: parent
        radius: 30 * Appearance.effectiveScale
        color: Appearance.colors.colOnPrimary // Card bg = play/pause icon color (user request)
        clip: true
    }

    // Art Style: Full-bleed album art background (no gap, rounded mask).
    // Stays visible under the lyrics view too (heavily blurred there).
    Item {
        id: artStyleBgWrapper
        anchors.fill: parent
        visible: opacity > 0
        opacity: (root.artMode && !root.showingSettings) ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: 180 }
        }
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: Rectangle {
                width: artStyleBgWrapper.width
                height: artStyleBgWrapper.height
                radius: bgCard.radius
            }
        }

        // Placeholder background when no image
        Rectangle {
            anchors.fill: parent
            color: MprisController.dynLayer0
            visible: !artStyleImg.visible

            MaterialSymbol {
                anchors.centerIn: parent
                text: "music_note"
                iconSize: 42 * Appearance.effectiveScale
                fill: 1
                color: MprisController.dynSubtext
            }
        }

        // Album Art Image (blurred while viewing lyrics)
        Image {
            id: artStyleImg
            anchors.fill: parent
            source: MprisController.displayedArtFilePath
            visible: source.toString() !== ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            layer.enabled: true
            layer.effect: GaussianBlur {
                radius: viewLyrics ? 24 * Appearance.effectiveScale : 0
                samples: Math.round(32 * Appearance.effectiveScale)
                cached: true
            }
        }

        // Scrim gradient (dark top for controls, dark bottom for texts)
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Functions.ColorUtils.applyAlpha("black", 0.55) }
                GradientStop { position: 0.15; color: Functions.ColorUtils.applyAlpha("black", 0.35) }
                GradientStop { position: 0.38; color: Functions.ColorUtils.applyAlpha("black", 0.12) }
                GradientStop { position: 0.6; color: Functions.ColorUtils.applyAlpha("black", 0.22) }
                GradientStop { position: 0.8; color: Functions.ColorUtils.applyAlpha("black", 0.55) }
                GradientStop { position: 1.0; color: Functions.ColorUtils.applyAlpha("black", 0.92) }
            }
        }

        // Extra dim for lyrics readability
        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: viewLyrics ? 0.3 : 0
            Behavior on opacity { NumberAnimation { duration: 180 } }
        }
    }

    // Toggle button in top right corner (M3 Styled Shape)
    Item {
        id: lyricsToggleBtn
        opacity: root.sizeMode !== "2x2" && !root.artMode && !root.showingSettings ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 16 * Appearance.effectiveScale
        anchors.rightMargin: 16 * Appearance.effectiveScale
        implicitWidth: 32 * Appearance.effectiveScale
        implicitHeight: 32 * Appearance.effectiveScale
        z: 20

        MaterialShape {
            anchors.fill: parent
            shape: MaterialShape.Shape.Cookie4Sided
            // Using colTertiaryContainer in dark mode and colSecondaryContainer in light mode for soft pastel visual
            color: viewLyrics 
                ? Appearance.colors.colPrimary 
                : (Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer)

            MaterialSymbol {
                anchors.centerIn: parent
                text: viewLyrics ? "music_note" : "lyrics"
                iconSize: 18 * Appearance.effectiveScale
                fill: 0
                color: viewLyrics 
                    ? Appearance.colors.colOnPrimary 
                    : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    viewLyrics = !viewLyrics;
                    if (viewLyrics) {
                        if (!Config.options.appearance.lyrics.showFloatingLyrics) {
                            LyricsService.restartLyrics();
                        }
                    }
                }
            }
        }
    }

    // Main Content Container
    Item {
        id: mainStack
        anchors.fill: parent
        anchors.margins: 16 * Appearance.effectiveScale
        clip: true

        // PAGE 0 (3x2 Variant): Media Control & Info View
        ColumnLayout {
            anchors.fill: parent
            visible: opacity > 0
            opacity: (!viewLyrics && root.sizeMode !== "2x2" && !root.artMode && !root.showingSettings) ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }
            spacing: 2 * Appearance.effectiveScale // Tighter spacing for title/artist

            // 1. TITLE (Centered, bounded from lyrics button)
            StyledText {
                Layout.fillWidth: true
                Layout.leftMargin: 48 * Appearance.effectiveScale
                Layout.rightMargin: 48 * Appearance.effectiveScale
                horizontalAlignment: Text.AlignHCenter
                text: {
                    let t = Functions.StringUtils.cleanMusicTitle(MprisController.trackTitle);
                    let isNoMedia = !t || t.toLowerCase() === "no media";
                    return isNoMedia ? I18nService.tr("No media") : t;
                }
                font.pixelSize: Appearance.font.pixelSize.normal
                font.weight: Font.DemiBold
                color: Appearance.colors.colPrimary // Title on colOnPrimary dark card
                elide: Text.ElideRight
            }

            // 2. ARTIST (Centered, bounded from lyrics button)
            StyledText {
                Layout.fillWidth: true
                Layout.leftMargin: 48 * Appearance.effectiveScale
                Layout.rightMargin: 48 * Appearance.effectiveScale
                horizontalAlignment: Text.AlignHCenter
                text: {
                    let rawTitle = (MprisController.trackTitle || "").trim().toLowerCase();
                    let hasTitle = rawTitle !== "" && rawTitle !== "no media" && rawTitle !== "no music playing";
                    let hasArtist = MprisController.trackArtist && MprisController.trackArtist.trim() !== "";
                    if (hasTitle) {
                        return hasArtist ? MprisController.trackArtist : I18nService.tr("Unknown Artist");
                    }
                    return I18nService.tr("Play some media");
                }
                font.pixelSize: Appearance.font.pixelSize.smaller
                font.weight: Font.DemiBold
                color: Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.75) // Artist subtitle on dark card
                elide: Text.ElideRight
            }
            
            Item { Layout.fillHeight: true } // Flexible spacer to push down to center

            // 3. BUTTONS (Centered, SANGAT BESAR)
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12 * Appearance.effectiveScale

                // Prev Button
                Item {
                    id: prevBtn
                    implicitWidth: 62 * Appearance.effectiveScale
                    implicitHeight: 62 * Appearance.effectiveScale

                    property bool hovered: false
                    property bool pressed: false

                    MaterialShape {
                        anchors.fill: parent
                        shape: MaterialShape.Shape.Cookie12Sided
                        color: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer

                        MaterialSymbol {
                            id: prevIcon
                            anchors.centerIn: parent
                            text: "skip_previous"
                            iconSize: 28 * Appearance.effectiveScale
                            fill: 0
                            color: prevBtn.hovered
                                ? (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colPrimary)
                                : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: prevBtn.hovered = true
                            onExited: prevBtn.hovered = false
                            onPressed: prevBtn.pressed = true
                            onReleased: prevBtn.pressed = false
                            onClicked: MprisController.previous()
                        }
                    }
                }

                // Play Button (Wide Pill)
                Rectangle {
                    id: playBtn
                    implicitWidth: 192 * Appearance.effectiveScale
                    implicitHeight: 66 * Appearance.effectiveScale
                    radius: 33 * Appearance.effectiveScale
                    color: Appearance.colors.colPrimary
                    Layout.alignment: Qt.AlignVCenter

                    property bool hovered: false
                    property bool pressed: false

                    MaterialSymbol {
                        id: playIcon
                        anchors.centerIn: parent
                        text: MprisController.isPlaying ? "pause" : "play_arrow"
                        iconSize: 40 * Appearance.effectiveScale
                        fill: 0
                        color: playBtn.pressed
                            ? Functions.ColorUtils.applyAlpha(Appearance.colors.colOnPrimary, 0.7)
                            : Appearance.colors.colOnPrimary
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Appearance.colors.colOnPrimary
                        opacity: playBtn.pressed ? 0.15 : (playBtn.hovered ? 0.08 : 0)
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onEntered: playBtn.hovered = true
                        onExited: playBtn.hovered = false
                        onPressed: playBtn.pressed = true
                        onReleased: playBtn.pressed = false
                        onClicked: MprisController.togglePlaying()
                    }
                }

                // Next Button
                Item {
                    id: nextBtn
                    implicitWidth: 62 * Appearance.effectiveScale
                    implicitHeight: 62 * Appearance.effectiveScale

                    property bool hovered: false
                    property bool pressed: false

                    MaterialShape {
                        anchors.fill: parent
                        shape: MaterialShape.Shape.Cookie12Sided
                        color: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer

                        MaterialSymbol {
                            id: nextIcon
                            anchors.centerIn: parent
                            text: "skip_next"
                            iconSize: 28 * Appearance.effectiveScale
                            fill: 0
                            color: nextBtn.hovered
                                ? (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colPrimary)
                                : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: nextBtn.hovered = true
                            onExited: nextBtn.hovered = false
                            onPressed: nextBtn.pressed = true
                            onReleased: nextBtn.pressed = false
                            onClicked: MprisController.next()
                        }
                    }
                }
            }
            
            Item { Layout.fillHeight: true } // Flexible spacer to balance vertical distribution

            // 4. DURASI SAAT INI / DURASI TOTAL (Centered with tabular figures)
            StyledText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Functions.StringUtils.friendlyTimeForSeconds(MprisController.position) + " / " + Functions.StringUtils.friendlyTimeForSeconds(MprisController.length)
                font.pixelSize: Appearance.font.pixelSize.smallest
                font.family: Appearance.font.family.numbers
                font.features: { "tnum": 1 }
                font.weight: Font.DemiBold
                color: Appearance.colors.colPrimary
                renderType: Text.QtRendering
            }

            // 5. PROGRESS BAR
            StyledSlider {
                id: progressSlider
                Layout.preferredWidth: 170 * Appearance.effectiveScale
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: 12 * Appearance.effectiveScale
                handleMargins: 0
                configuration: StyledSlider.Configuration.X0
                stopIndicatorValues: []
                animateValue: false
                value: (MprisController.length > 0 ? (MprisController.position / MprisController.length) : 0) || 0
                highlightColor: Appearance.colors.colPrimary
                trackColor: Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.25)

                handle: Rectangle {
                    x: progressSlider.leftPadding + (progressSlider.visualPosition * (progressSlider.availableWidth - width))
                    y: (progressSlider.height - height) / 2
                    width: 14 * Appearance.effectiveScale
                    height: 14 * Appearance.effectiveScale
                    radius: width / 2
                    color: Appearance.colors.colPrimary
                }

                onMoved: {
                    if (MprisController.activePlayer && MprisController.activePlayer.canSeek) {
                        MprisController.activePlayer.position = value * MprisController.activePlayer.length;
                    }
                }

                Connections {
                    target: MprisController
                    function onPositionChanged() {
                        if (!progressSlider.pressed) {
                            progressSlider.value = (MprisController.length > 0 ? (MprisController.position / MprisController.length) : 0) || 0;
                        }
                    }
                }
            }
        }

        // PAGE 0 (2x2 Variant): Thumbnail Masking Rounded + Controls + Progress Slider + Timer
        ColumnLayout {
            anchors.fill: parent
            visible: opacity > 0
            opacity: (!viewLyrics && root.sizeMode === "2x2" && !root.artMode && !root.showingSettings) ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }
            spacing: 0

            // 1. Thumbnail masking rounded (Full Rounded / Capsule Shape)
            Item {
                id: artContainer2x2
                Layout.fillWidth: true
                Layout.preferredHeight: 102 * Appearance.effectiveScale

                Item {
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: artContainer2x2.width
                            height: artContainer2x2.height
                            radius: artContainer2x2.height / 2
                        }
                    }

                    // Placeholder background when no image
                    Rectangle {
                        anchors.fill: parent
                        color: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer
                        visible: !artImg2x2.visible

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: "music_note"
                            iconSize: 32 * Appearance.effectiveScale
                            color: Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer
                        }
                    }

                    // Album Art Image
                    Image {
                        id: artImg2x2
                        anchors.fill: parent
                        source: MprisController.displayedArtFilePath
                        visible: source.toString() !== ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: false
                    }

                    // Gradient overlay for title text visibility
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 44 * Appearance.effectiveScale
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: Functions.ColorUtils.applyAlpha(Appearance.colors.colOnPrimary, 0.8) }
                        }
                    }

                    // Title & Artist on Thumbnail
                    ColumnLayout {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 4 * Appearance.effectiveScale
                        anchors.bottomMargin: 6 * Appearance.effectiveScale
                        spacing: 0

                        // Title (Bigger font size with '...' truncation)
                        StyledText {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12 * Appearance.effectiveScale
                            Layout.rightMargin: 12 * Appearance.effectiveScale
                            horizontalAlignment: Text.AlignHCenter
                            text: (MprisController.trackTitle && MprisController.trackTitle !== "No media") ? Functions.StringUtils.cleanMusicTitle(MprisController.trackTitle) : I18nService.tr("No media")
                            font.pixelSize: Appearance.font.pixelSize.small
                            font.weight: Font.Bold
                            color: Appearance.colors.colPrimary
                            elide: Text.ElideRight
                        }

                        // Artist (With '...' truncation)
                        StyledText {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12 * Appearance.effectiveScale
                            Layout.rightMargin: 12 * Appearance.effectiveScale
                            horizontalAlignment: Text.AlignHCenter
                            text: (MprisController.activePlayer && MprisController.trackTitle !== "No media") ? (MprisController.trackArtist || I18nService.tr("Unknown Artist")) : I18nService.tr("Play some media")
                            font.pixelSize: Appearance.font.pixelSize.smallest
                            font.weight: Font.Normal
                            color: Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.8)
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 6 * Appearance.effectiveScale }

            // 2. Control Buttons [prev][play/pause][next]
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12 * Appearance.effectiveScale

                // Prev Button (Simple Rounded Circle)
                Item {
                    id: prevBtn2x2
                    implicitWidth: 38 * Appearance.effectiveScale
                    implicitHeight: 38 * Appearance.effectiveScale

                    property bool hovered: false
                    property bool pressed: false

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: prevBtn2x2.pressed
                            ? Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.3)
                            : (Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer)

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: "skip_previous"
                            iconSize: 20 * Appearance.effectiveScale
                            fill: 0
                            color: prevBtn2x2.hovered
                                ? (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colPrimary)
                                : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: prevBtn2x2.hovered = true
                            onExited: prevBtn2x2.hovered = false
                            onPressed: prevBtn2x2.pressed = true
                            onReleased: prevBtn2x2.pressed = false
                            onClicked: MprisController.previous()
                        }
                    }
                }

                // Play/Pause Button (Flower Cookie 12-Sided)
                Item {
                    id: playBtn2x2
                    implicitWidth: 46 * Appearance.effectiveScale
                    implicitHeight: 46 * Appearance.effectiveScale

                    property bool hovered: false
                    property bool pressed: false

                    MaterialShape {
                        anchors.fill: parent
                        shape: MaterialShape.Shape.Cookie12Sided
                        color: Appearance.colors.colPrimary

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: MprisController.isPlaying ? "pause" : "play_arrow"
                            iconSize: 26 * Appearance.effectiveScale
                            fill: 0
                            color: playBtn2x2.pressed
                                ? Functions.ColorUtils.applyAlpha(Appearance.colors.colOnPrimary, 0.7)
                                : Appearance.colors.colOnPrimary
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: playBtn2x2.hovered = true
                            onExited: playBtn2x2.hovered = false
                            onPressed: playBtn2x2.pressed = true
                            onReleased: playBtn2x2.pressed = false
                            onClicked: MprisController.togglePlaying()
                        }
                    }
                }

                // Next Button (Simple Rounded Circle)
                Item {
                    id: nextBtn2x2
                    implicitWidth: 38 * Appearance.effectiveScale
                    implicitHeight: 38 * Appearance.effectiveScale

                    property bool hovered: false
                    property bool pressed: false

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: nextBtn2x2.pressed
                            ? Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.3)
                            : (Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer)

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: "skip_next"
                            iconSize: 20 * Appearance.effectiveScale
                            fill: 0
                            color: nextBtn2x2.hovered
                                ? (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colPrimary)
                                : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            hoverEnabled: true
                            onEntered: nextBtn2x2.hovered = true
                            onExited: nextBtn2x2.hovered = false
                            onPressed: nextBtn2x2.pressed = true
                            onReleased: nextBtn2x2.pressed = false
                            onClicked: MprisController.next()
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 * Appearance.effectiveScale }

            // 3. Progress Slider (Same as 3x2)
            StyledSlider {
                id: progressSlider2x2
                Layout.fillWidth: true
                Layout.preferredHeight: 12 * Appearance.effectiveScale
                handleMargins: 0
                configuration: StyledSlider.Configuration.X0
                stopIndicatorValues: []
                animateValue: false
                value: (MprisController.length > 0 ? (MprisController.position / MprisController.length) : 0) || 0
                highlightColor: Appearance.colors.colPrimary
                trackColor: Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.25)

                handle: Rectangle {
                    x: progressSlider2x2.leftPadding + (progressSlider2x2.visualPosition * (progressSlider2x2.availableWidth - width))
                    y: (progressSlider2x2.height - height) / 2
                    width: 14 * Appearance.effectiveScale
                    height: 14 * Appearance.effectiveScale
                    radius: width / 2
                    color: Appearance.colors.colPrimary
                }

                onMoved: {
                    if (MprisController.activePlayer && MprisController.activePlayer.canSeek) {
                        MprisController.activePlayer.position = value * MprisController.activePlayer.length;
                    }
                }

                Connections {
                    target: MprisController
                    function onPositionChanged() {
                        if (!progressSlider2x2.pressed) {
                            progressSlider2x2.value = (MprisController.length > 0 ? (MprisController.position / MprisController.length) : 0) || 0;
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 6 * Appearance.effectiveScale }

            // 4. Timer Media (Formatted Position / Length)
            StyledText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: Functions.StringUtils.friendlyTimeForSeconds(MprisController.position) + " / " + Functions.StringUtils.friendlyTimeForSeconds(MprisController.length)
                font.pixelSize: Appearance.font.pixelSize.smallest
                font.family: Appearance.font.family.numbers
                font.features: { "tnum": 1 }
                font.weight: Font.DemiBold
                color: Appearance.colors.colPrimary
                renderType: Text.QtRendering
            }
        }

        // PAGE (Art Style, both 3x2 & 2x2): Full-bleed album art immersive view
        // NOTE: The album art background itself lives outside mainStack (artStyleBgWrapper) so it fills the card without gaps.
        // This page also stays mounted while viewing lyrics so the top controls remain accessible to toggle back.
        Item {
            anchors.fill: parent
            visible: opacity > 0
            opacity: (root.artMode && !root.showingSettings) ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }

            // Top-right M3 playback controls in a frosted pill (matches the scrim style)
            Rectangle {
                id: artControlsPill
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 0
                anchors.rightMargin: 2 * Appearance.effectiveScale
                implicitWidth: artControlsRow.implicitWidth + 14 * Appearance.effectiveScale
                implicitHeight: artControlsRow.implicitHeight + 6 * Appearance.effectiveScale
                radius: height / 2
                color: Functions.ColorUtils.applyAlpha("black", 0.4)

                RowLayout {
                    id: artControlsRow
                    anchors.centerIn: parent
                    spacing: 2 * Appearance.effectiveScale

                    // Prev Button
                    RippleButton {
                        id: artPrevBtn
                        padding: 0
                        implicitWidth: 28 * Appearance.effectiveScale
                        implicitHeight: 28 * Appearance.effectiveScale
                        buttonRadius: 14 * Appearance.effectiveScale
                        colBackground: "transparent"
                        colBackgroundHover: "transparent"
                        colRipple: Functions.ColorUtils.applyAlpha("white", 0.25)
                        enabled: MprisController.canGoPrevious
                        onClicked: MprisController.previous()

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: "skip_previous"
                            iconSize: 18 * Appearance.effectiveScale
                            fill: 1
                            color: artPrevBtn.hovered ? MprisController.dynPrimary : "white"
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }
                    }

                    // Play/Pause Button
                    RippleButton {
                        id: artPlayBtn
                        padding: 0
                        implicitWidth: 28 * Appearance.effectiveScale
                        implicitHeight: 28 * Appearance.effectiveScale
                        buttonRadius: 14 * Appearance.effectiveScale
                        colBackground: "transparent"
                        colBackgroundHover: "transparent"
                        colRipple: Functions.ColorUtils.applyAlpha("white", 0.25)
                        onClicked: MprisController.togglePlaying()

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: MprisController.isPlaying ? "pause" : "play_arrow"
                            iconSize: 20 * Appearance.effectiveScale
                            fill: 1
                            color: artPlayBtn.hovered ? MprisController.dynPrimary : "white"
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }
                    }

                    // Next Button
                    RippleButton {
                        id: artNextBtn
                        padding: 0
                        implicitWidth: 28 * Appearance.effectiveScale
                        implicitHeight: 28 * Appearance.effectiveScale
                        buttonRadius: 14 * Appearance.effectiveScale
                        colBackground: "transparent"
                        colBackgroundHover: "transparent"
                        colRipple: Functions.ColorUtils.applyAlpha("white", 0.25)
                        enabled: MprisController.canGoNext
                        onClicked: MprisController.next()

                        MaterialSymbol {
                            anchors.centerIn: parent
                            text: "skip_next"
                            iconSize: 18 * Appearance.effectiveScale
                            fill: 1
                            color: artNextBtn.hovered ? MprisController.dynPrimary : "white"
                            Behavior on color { ColorAnimation { duration: 150 } }
                        }
                    }
                }
            }

            // Lyrics Toggle (3x2 only) — floats separately next to the playback pill, same frosted card style
            RippleButton {
                id: artLyricsBtn
                visible: root.sizeMode !== "2x2"
                anchors.right: artControlsPill.left
                anchors.rightMargin: 4 * Appearance.effectiveScale
                anchors.verticalCenter: artControlsPill.verticalCenter
                padding: 0
                implicitWidth: 34 * Appearance.effectiveScale
                implicitHeight: 34 * Appearance.effectiveScale
                buttonRadius: 17 * Appearance.effectiveScale
                colBackground: Functions.ColorUtils.applyAlpha("black", 0.4)
                colBackgroundHover: Functions.ColorUtils.applyAlpha("black", 0.55)
                colRipple: Functions.ColorUtils.applyAlpha("white", 0.25)
                onClicked: {
                    viewLyrics = !viewLyrics;
                    if (viewLyrics) {
                        if (!Config.options.appearance.lyrics.showFloatingLyrics) {
                            LyricsService.restartLyrics();
                        }
                    }
                }

                MaterialSymbol {
                    anchors.centerIn: parent
                    text: viewLyrics ? "music_note" : "lyrics"
                    iconSize: 18 * Appearance.effectiveScale
                    fill: viewLyrics ? 1 : 0
                    color: (viewLyrics || artLyricsBtn.hovered) ? MprisController.dynPrimary : "white"
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
            }

            // Bottom: Track info + handle-less progress bar (hidden while viewing lyrics)
            ColumnLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 2 * Appearance.effectiveScale
                anchors.rightMargin: 2 * Appearance.effectiveScale
                anchors.bottomMargin: 0
                spacing: 0
                visible: opacity > 0
                opacity: viewLyrics ? 0 : 1
                Behavior on opacity {
                    NumberAnimation { duration: 180 }
                }

                StyledText {
                    Layout.fillWidth: true
                    text: {
                        let t = Functions.StringUtils.cleanMusicTitle(MprisController.trackTitle);
                        let isNoMedia = !t || t.toLowerCase() === "no media";
                        return isNoMedia ? I18nService.tr("No media") : t;
                    }
                    font.pixelSize: Appearance.font.pixelSize.normal
                    color: "white"
                    elide: Text.ElideRight
                }

                StyledText {
                    Layout.fillWidth: true
                    text: {
                        let rawTitle = (MprisController.trackTitle || "").trim().toLowerCase();
                        let hasTitle = rawTitle !== "" && rawTitle !== "no media" && rawTitle !== "no music playing";
                        let hasArtist = MprisController.trackArtist && MprisController.trackArtist.trim() !== "";
                        if (hasTitle) {
                            return hasArtist ? MprisController.trackArtist : I18nService.tr("Unknown Artist");
                        }
                        return I18nService.tr("Play some media");
                    }
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Functions.ColorUtils.applyAlpha("white", 0.8)
                    elide: Text.ElideRight
                }

                Item { Layout.preferredHeight: 8 * Appearance.effectiveScale }

                StyledSlider {
                    id: artProgressSlider
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20 * Appearance.effectiveScale
                    handleMargins: 0
                    configuration: StyledSlider.Configuration.X0
                    stopIndicatorValues: []
                    showTrailingDot: false
                    usePercentTooltip: false
                    animateValue: false
                    value: (MprisController.length > 0 ? (MprisController.position / MprisController.length) : 0) || 0
                    highlightColor: MprisController.dynPrimary
                    trackColor: Functions.ColorUtils.applyAlpha("white", 0.3)

                    // Handle-less design (no knob)
                    handle: Item {}

                    onMoved: {
                        if (MprisController.activePlayer && MprisController.activePlayer.canSeek) {
                            MprisController.activePlayer.position = value * MprisController.activePlayer.length;
                        }
                    }

                    Connections {
                        target: MprisController
                        function onPositionChanged() {
                            if (!artProgressSlider.pressed) {
                                artProgressSlider.value = (MprisController.length > 0 ? (MprisController.position / MprisController.length) : 0) || 0;
                            }
                        }
                    }
                }
            }
        }

        // PAGE 1: Lyrics View (Clean 5 Lines Display)
        ColumnLayout {
            anchors.fill: parent
            visible: opacity > 0
            opacity: (viewLyrics && root.sizeMode !== "2x2" && !root.showingSettings) ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: 180 }
            }
            spacing: 0

            Item { Layout.fillHeight: true } // Spacer

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 4 * Appearance.effectiveScale

                // 5 Line Lyrics Display (dynamically centered around the active line index 'before')
                Repeater {
                    model: {
                        if (LyricsService.slots.length === 0) return [];
                        let mid = LyricsService.before;
                        // Returns 5 indices centered around 'mid': [mid-2, mid-1, mid, mid+1, mid+2]
                        return [mid - 2, mid - 1, mid, mid + 1, mid + 2];
                    }
                    delegate: StyledText {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: {
                            let slotIndex = modelData;
                            if (slotIndex < 0 || slotIndex >= LyricsService.slots.length) return "";
                            let slot = LyricsService.slots[slotIndex];
                            if (!slot) return "";
                            let raw = Config.options.appearance.lyrics.lyricsUseRomaji ? slot.romajiText : slot.originalText;
                            // Translate LyricsService status placeholder strings
                            const statusKeys = ["No lyrics found", "No track playing", "Preparing lyrics..."];
                            if (statusKeys.includes(raw)) return I18nService.tr(raw);
                            return raw;
                        }
                        font.pixelSize: modelData === LyricsService.before // Active line is bigger
                            ? Appearance.font.pixelSize.large
                            : Appearance.font.pixelSize.small
                        font.weight: modelData === LyricsService.before ? Font.Bold : Font.Normal
                        color: {
                            // Art mode: white like the title/artist texts; default: theme primary
                            let baseColor = root.artMode ? "white" : Appearance.colors.colPrimary;
                            if (modelData === LyricsService.before) return baseColor;
                            // Make outer lines even more faded
                            let isOuter = (modelData === LyricsService.before - 2 || modelData === LyricsService.before + 2);
                            let alpha = isOuter ? 0.25 : 0.45;
                            return Functions.ColorUtils.applyAlpha(baseColor, alpha);
                        }
                        elide: modelData === LyricsService.before ? Text.ElideNone : Text.ElideRight
                        maximumLineCount: modelData === LyricsService.before ? 2 : 1 // Active line can wrap up to 2 lines for karaoke
                        
                        Behavior on font.pixelSize { NumberAnimation { duration: 200 } }
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                }
                
                // Fallback if no lyrics/loading
                StyledText {
                    visible: LyricsService.slots.length === 0
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: LyricsService.status === "loading" ? I18nService.tr("Loading lyrics...") : I18nService.tr("No lyrics available")
                    color: Functions.ColorUtils.applyAlpha(root.artMode ? "white" : Appearance.colors.colPrimary, 0.6)
                    font.pixelSize: Appearance.font.pixelSize.normal
                }
            }

            Item { Layout.fillHeight: true } // Spacer
        }
    }

    // Romaji/Original switcher (outside layout, anchored - won't affect centering)
    Item {
        id: romajiToggleBtn
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: 16 * Appearance.effectiveScale
        anchors.leftMargin: 16 * Appearance.effectiveScale
        implicitWidth: 32 * Appearance.effectiveScale
        implicitHeight: 32 * Appearance.effectiveScale
        visible: viewLyrics && !root.showingSettings
        z: 20

        property bool hovered: false

        MaterialShape {
            anchors.fill: parent
            shape: root.artMode ? MaterialShape.Shape.Circle : MaterialShape.Shape.Pill
            // Art mode: frosted dark card to match the album-art controls; default: theme container colors
            color: root.artMode
                ? Functions.ColorUtils.applyAlpha("black", 0.4)
                : (Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer)

            MaterialSymbol {
                anchors.centerIn: parent
                text: Config.options.appearance.lyrics.lyricsUseRomaji ? "text_fields" : "translate"
                iconSize: 18 * Appearance.effectiveScale
                fill: 1
                color: {
                    if (root.artMode) return romajiToggleBtn.hovered ? MprisController.dynPrimary : "white";
                    return romajiToggleBtn.hovered
                        ? (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colPrimary)
                        : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer);
                }
                Behavior on color { ColorAnimation { duration: 150 } }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                onEntered: romajiToggleBtn.hovered = true
                onExited: romajiToggleBtn.hovered = false
                onClicked: {
                    if (Config.ready) {
                        Config.options.appearance.lyrics.lyricsUseRomaji = !Config.options.appearance.lyrics.lyricsUseRomaji;
                    }
                }
            }
        }
    }

    // Settings button (appears on hover, hidden when locked) — triggers style flip
    Item {
        id: settingsBtn
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
            color: Appearance.colors.colPrimary

            MaterialSymbol {
                anchors.centerIn: parent
                text: root.showingSettings ? "close" : "settings"
                iconSize: 14 * Appearance.effectiveScale
                color: Appearance.colors.colOnPrimary
            }

            MouseArea {
                anchors.fill: parent
                enabled: widgetHoverHandler.hovered
                cursorShape: Qt.PointingHandCursor
                onClicked: root.toggleFlip()
            }
        }
    }

    // Flip Settings Page (style picker)
    Item {
        anchors.fill: parent
        visible: root.showingSettings

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
                text: I18nService.tr("Widget Style")
                font.pixelSize: Appearance.font.pixelSize.small
                font.weight: Font.Bold
                color: Appearance.colors.colPrimary
            }

            Repeater {
                model: [
                    { key: "default", name: I18nService.tr("Default"), icon: "queue_music" },
                    { key: "art", name: I18nService.tr("Album Art"), icon: "album" }
                ]
                delegate: RippleButton {
                    id: styleOptBtn
                    Layout.fillWidth: true
                    implicitHeight: 42 * Appearance.effectiveScale
                    padding: 0
                    leftPadding: 14 * Appearance.effectiveScale
                    rightPadding: 14 * Appearance.effectiveScale
                    buttonRadius: 14 * Appearance.effectiveScale
                    toggled: root.widgetStyle === modelData.key

                    colBackground: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer
                    colBackgroundHover: colBackground
                    colBackgroundToggled: Appearance.colors.colPrimary
                    colBackgroundToggledHover: colBackgroundToggled
                    colRipple: Functions.ColorUtils.applyAlpha(styleOptBtn.toggled ? Appearance.colors.colOnPrimary : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer), 0.15)

                    onClicked: {
                        if (root.cfg && Config.ready) {
                            root.cfg.style = modelData.key;
                        }
                    }

                    contentItem: RowLayout {
                        spacing: 8 * Appearance.effectiveScale

                        MaterialSymbol {
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.icon
                            iconSize: 18 * Appearance.effectiveScale
                            fill: 1
                            color: styleOptBtn.toggled ? Appearance.colors.colOnPrimary : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
                        }

                        StyledText {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.name
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            font.weight: Font.DemiBold
                            color: styleOptBtn.toggled ? Appearance.colors.colOnPrimary : (Appearance.m3colors.darkmode ? Appearance.colors.colTertiaryContainer : Appearance.colors.colOnSecondaryContainer)
                        }

                        MaterialSymbol {
                            Layout.alignment: Qt.AlignVCenter
                            text: "check_circle"
                            iconSize: 16 * Appearance.effectiveScale
                            visible: styleOptBtn.toggled
                            color: Appearance.colors.colOnPrimary
                        }
                    }
                }
            }
        }
    }

    // Bottom-right Drag Resize Handle (matching WeatherWidget / CurrencyWidget)
    Rectangle {
        id: resizeHandle
        z: 30
        implicitWidth: 24 * Appearance.effectiveScale
        implicitHeight: 24 * Appearance.effectiveScale
        radius: 8 * Appearance.effectiveScale
        color: Appearance.m3colors.darkmode ? Appearance.colors.colOnTertiaryContainer : Appearance.colors.colSecondaryContainer

        anchors {
            right: root.right
            bottom: root.bottom
            margins: -8 * Appearance.effectiveScale
        }

        opacity: (cfg && !cfg.locked) && (widgetHoverHandler.hovered || resizeArea.containsMouse || resizeArea.pressed) ? 0.9 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

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
            preventStealing: true

            property real startWidth: 0
            property real startGlobalX: 0

            onPressed: (mouse) => {
                startWidth = root.width;
                let p = mapToItem(null, mouse.x, mouse.y);
                startGlobalX = p.x;
            }

            onPositionChanged: (mouse) => {
                if (!pressed) return;
                let p = mapToItem(null, mouse.x, mouse.y);
                let deltaX = p.x - startGlobalX;
                let targetWidth = startWidth + deltaX;

                let targetMode = root.getModeForWidth(targetWidth);
                if (targetMode !== root.sizeMode) {
                    if (cfg) {
                        cfg.sizeMode = targetMode;
                    }
                }
            }
        }
    }
}
