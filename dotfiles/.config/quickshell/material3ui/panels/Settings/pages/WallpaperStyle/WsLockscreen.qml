import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import "."
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Qt5Compat.GraphicalEffects
import Quickshell
import Quickshell.Io

ColumnLayout {
    Layout.fillWidth: true
    spacing: 0
    Component.onCompleted: GreeterSync.refresh()
    // ── Lockscreen Section ──
    ColumnLayout {
        id: lockscreenStyleSection
        Layout.fillWidth: true
        Layout.bottomMargin: 16 * Appearance.effectiveScale
        spacing: 4 * Appearance.effectiveScale
                
                SearchHandler { 
                    visible: false
                    searchString: "Lockscreen"
                    aliases: ["Lock", "Lock Screen"]
                }
                
                SearchHandler {
                    visible: false
                    searchString: "Lockscreen Clock"
                    aliases: ["Clock", "Time", "Watch", "Clock Style"]
                }
    
                // Section Header
                RowLayout {
                    spacing: 12 * Appearance.effectiveScale
                    Layout.bottomMargin: 8 * Appearance.effectiveScale
    
                    MaterialSymbol {
                        text: "lock"
                        iconSize: 24 * Appearance.effectiveScale
                        color: Appearance.colors.colPrimary
                    }
                    StyledText {
                        text: I18nService.tr("Lockscreen")
                        font.pixelSize: Appearance.font.pixelSize.large
                        font.family: Appearance.font.family.title
                        font.weight: Font.Medium
                        color: Appearance.colors.colOnLayer1
                        Layout.fillWidth: true
                    }
                }
    
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2 * Appearance.effectiveScale
    
                    // ── Show Weather ──────────────
                    SegmentedWrapper {
                        id: showWeatherCard
                        Layout.fillWidth: true
                        implicitHeight: Math.max(64 * Appearance.effectiveScale, showWeatherRow.implicitHeight)
                        orientation: Qt.Vertical
                        color: Appearance.m3colors.m3surfaceContainerHigh

                        RippleButton {
                            anchors.fill: parent
                            colBackground: Appearance.m3colors.m3surfaceContainerHigh
                            colBackgroundHover: Appearance.m3colors.m3surfaceContainerHigh
                            buttonRadius: 0
                            topLeftRadius: showWeatherCard.rTopLeft
                            topRightRadius: showWeatherCard.rTopRight
                            bottomLeftRadius: showWeatherCard.rBottomLeft
                            bottomRightRadius: showWeatherCard.rBottomRight
                            onClicked: if(Config.ready) Config.options.lock.showWeather = !Config.options.lock.showWeather
                        }

                        RowLayout {
                            id: showWeatherRow
                            anchors.fill: parent
                            anchors {
                                leftMargin: 16 * Appearance.effectiveScale
                                rightMargin: 16 * Appearance.effectiveScale
                            }
                            spacing: 16 * Appearance.effectiveScale
                            MaterialSymbol { text: "cloud"; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                            StyledText { text: I18nService.tr("Show Weather Text"); Layout.fillWidth: true; color: Appearance.colors.colOnLayer1 }
                            AndroidToggle {
                                checked: Config.ready && Config.options.lock.showWeather
                                onToggled: if(Config.ready) Config.options.lock.showWeather = !Config.options.lock.showWeather
                            }
                        }
                    }

                    // ── Weather text color mode (Adaptive) ────────────
                    SegmentedWrapper {
                        Layout.fillWidth: true
                        visible: Config.ready && (Config.options.lock?.showWeather ?? true)
                        implicitHeight: Math.max(64 * Appearance.effectiveScale, weatherTextRow.implicitHeight)
                        orientation: Qt.Vertical
                        color: Appearance.m3colors.m3surfaceContainerHigh
                        RowLayout {
                            id: weatherTextRow
                            anchors.fill: parent
                            anchors {
                                leftMargin: 16 * Appearance.effectiveScale
                                rightMargin: 16 * Appearance.effectiveScale
                            }
                            spacing: 16 * Appearance.effectiveScale
                            MaterialSymbol { text: "palette"; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                            StyledText { text: I18nService.tr("Weather text color"); Layout.fillWidth: true; color: Appearance.colors.colOnLayer1 }
                            RowLayout {
                                spacing: 2 * Appearance.effectiveScale
                                Repeater {
                                    model: [
                                        { id: "adaptive", label: I18nService.tr("Adaptive") },
                                        { id: "light",    label: I18nService.tr("Light") },
                                        { id: "dark",     label: I18nService.tr("Dark") }
                                    ]
                                    delegate: SegmentedButton {
                                        required property var modelData
                                        buttonText: modelData.label
                                        isHighlighted: Config.ready && Config.options.lock && Config.options.lock.weather
                                            ? Config.options.lock.weather.textColorMode === modelData.id
                                            : modelData.id === "adaptive"
                                        colActive: Appearance.m3colors.m3primary
                                        colActiveText: Appearance.m3colors.m3onPrimary
                                        colInactive: Appearance.m3colors.m3surfaceContainerLow
                                        onClicked: if (Config.ready && Config.options.lock && Config.options.lock.weather)
                                            Config.options.lock.weather.textColorMode = modelData.id
                                    }
                                }
                            }
                        }
                    }

                    // ── Show Media Controls ──────────────
                    SegmentedWrapper {
                        id: showMediaCard
                        Layout.fillWidth: true
                        implicitHeight: Math.max(64 * Appearance.effectiveScale, showMediaRow.implicitHeight)
                        orientation: Qt.Vertical
                        color: Appearance.m3colors.m3surfaceContainerHigh

                        RippleButton {
                            anchors.fill: parent
                            colBackground: Appearance.m3colors.m3surfaceContainerHigh
                            colBackgroundHover: Appearance.m3colors.m3surfaceContainerHigh
                            buttonRadius: 0
                            topLeftRadius: showMediaCard.rTopLeft
                            topRightRadius: showMediaCard.rTopRight
                            bottomLeftRadius: showMediaCard.rBottomLeft
                            bottomRightRadius: showMediaCard.rBottomRight
                            onClicked: if(Config.ready) Config.options.lock.showMediaCard = !Config.options.lock.showMediaCard
                        }

                        RowLayout {
                            id: showMediaRow
                            anchors.fill: parent
                            anchors {
                                leftMargin: 16 * Appearance.effectiveScale
                                rightMargin: 16 * Appearance.effectiveScale
                            }
                            spacing: 16 * Appearance.effectiveScale
                            MaterialSymbol { text: "movie"; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                            StyledText { text: I18nService.tr("Show Media Controls"); Layout.fillWidth: true; color: Appearance.colors.colOnLayer1 }
                            AndroidToggle {
                                checked: Config.ready && Config.options.lock.showMediaCard
                                onToggled: if(Config.ready) Config.options.lock.showMediaCard = !Config.options.lock.showMediaCard
                            }
                        }
                    }

                    // ── Login screen (greetd greeter) ──────────────
                    SearchHandler {
                        visible: false
                        searchString: "Login screen"
                        aliases: ["Greeter", "greetd", "Login", "Sync login screen"]
                    }

                    SegmentedWrapper {
                        id: greeterSyncCard
                        Layout.fillWidth: true
                        implicitHeight: Math.max(72 * Appearance.effectiveScale, greeterSyncRow.implicitHeight + 24 * Appearance.effectiveScale)
                        orientation: Qt.Vertical
                        color: Appearance.m3colors.m3surfaceContainerHigh

                        RippleButton {
                            anchors.fill: parent
                            enabled: GreeterSync.installed
                            colBackground: Appearance.m3colors.m3surfaceContainerHigh
                            colBackgroundHover: Appearance.m3colors.m3surfaceContainerHigh
                            buttonRadius: 0
                            topLeftRadius: greeterSyncCard.rTopLeft
                            topRightRadius: greeterSyncCard.rTopRight
                            bottomLeftRadius: greeterSyncCard.rBottomLeft
                            bottomRightRadius: greeterSyncCard.rBottomRight
                            onClicked: if (Config.ready) Config.options.lock.greeter.autoSync = !Config.options.lock.greeter.autoSync
                        }

                        RowLayout {
                            id: greeterSyncRow
                            anchors.fill: parent
                            anchors {
                                leftMargin: 16 * Appearance.effectiveScale
                                rightMargin: 16 * Appearance.effectiveScale
                            }
                            spacing: 16 * Appearance.effectiveScale
                            MaterialSymbol { text: "login"; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2 * Appearance.effectiveScale
                                StyledText {
                                    text: I18nService.tr("Auto sync login screen")
                                    Layout.fillWidth: true
                                    color: Appearance.colors.colOnLayer1
                                }
                                StyledText {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Appearance.font.pixelSize.smaller
                                    color: GreeterSync.lastResult === "error" ? Appearance.m3colors.m3error : Appearance.colors.colSubtext
                                    text: {
                                        if (!GreeterSync.installed)
                                            return I18nService.tr("Greeter not installed. Run: material3ui-greeter install --enable");
                                        if (!GreeterSync.writable)
                                            return I18nService.tr("No write access to the login screen. Re-run: material3ui-greeter install");
                                        if (GreeterSync.syncing)
                                            return I18nService.tr("Syncing…");
                                        if (GreeterSync.lastResult === "error")
                                            return GreeterSync.lastMessage;
                                        if (GreeterSync.lastSyncTime > 0)
                                            return I18nService.tr("Synced at %1").arg(Qt.formatTime(new Date(GreeterSync.lastSyncTime), Config.timeFormat));
                                        return I18nService.tr("Mirror wallpaper, colours, avatar and clock to the greetd login screen");
                                    }
                                }
                            }
                            AndroidToggle {
                                enabled: GreeterSync.installed
                                checked: GreeterSync.autoSync
                                onToggled: if (Config.ready) Config.options.lock.greeter.autoSync = !Config.options.lock.greeter.autoSync
                            }
                        }
                    }

                    SegmentedWrapper {
                        Layout.fillWidth: true
                        visible: GreeterSync.installed
                        implicitHeight: Math.max(64 * Appearance.effectiveScale, greeterActionsRow.implicitHeight)
                        orientation: Qt.Vertical
                        color: Appearance.m3colors.m3surfaceContainerHigh

                        RowLayout {
                            id: greeterActionsRow
                            anchors.fill: parent
                            anchors {
                                leftMargin: 16 * Appearance.effectiveScale
                                rightMargin: 16 * Appearance.effectiveScale
                            }
                            spacing: 8 * Appearance.effectiveScale
                            MaterialSymbol { text: "sync"; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                            StyledText {
                                text: I18nService.tr("Login screen")
                                Layout.fillWidth: true
                                Layout.leftMargin: 8 * Appearance.effectiveScale
                                color: Appearance.colors.colOnLayer1
                            }
                            Repeater {
                                model: [
                                    { label: I18nService.tr("Preview"), icon: "visibility", primary: false },
                                    { label: I18nService.tr("Sync now"), icon: "sync", primary: true }
                                ]
                                delegate: RippleButton {
                                    id: greeterBtn
                                    required property var modelData
                                    implicitHeight: 36 * Appearance.effectiveScale
                                    implicitWidth: greeterBtnRow.implicitWidth + 28 * Appearance.effectiveScale
                                    buttonRadius: 18 * Appearance.effectiveScale
                                    enabled: !(modelData.primary && (GreeterSync.syncing || !GreeterSync.writable))
                                    opacity: enabled ? 1 : 0.5
                                    colBackground: modelData.primary ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerLow
                                    onClicked: modelData.primary ? GreeterSync.syncNow() : GreeterSync.preview()
                                    RowLayout {
                                        id: greeterBtnRow
                                        anchors.centerIn: parent
                                        spacing: 6 * Appearance.effectiveScale
                                        MaterialSymbol {
                                            text: greeterBtn.modelData.icon
                                            iconSize: 18 * Appearance.effectiveScale
                                            color: greeterBtn.modelData.primary ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer1
                                        }
                                        StyledText {
                                            text: greeterBtn.modelData.label
                                            font.pixelSize: Appearance.font.pixelSize.small
                                            color: greeterBtn.modelData.primary ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer1
                                        }
                                    }
                                }
                            }
                        }
                    }
                    }
                }
    
            // ── Lockscreen Clock Section ──
            WsClock { 
                Layout.fillWidth: true
                isDedicatedContext: true
                dedicatedIsLock: true 
                isSubSection: true
            }
}
