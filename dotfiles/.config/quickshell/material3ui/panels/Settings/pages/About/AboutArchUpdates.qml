import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import QtQuick
import QtQuick.Layouts
import Quickshell

/**
 * Settings → About → Packages & updates: Arch (pacman + AUR) and Flatpak
 * updates with check / upgrade actions, package search and install across the
 * official repositories, AUR and Flatpak, schedule settings and the status
 * bar plugin.
 */
ColumnLayout {
    id: root
    spacing: 16 * Appearance.effectiveScale

    readonly property bool inBar: Config.ready && Config.options.statusBar
        && (Array.from(Config.options.statusBar.leftModules).includes("archUpdates")
            || Array.from(Config.options.statusBar.rightModules).includes("archUpdates"))

    function addToBar() {
        if (!Config.ready || root.inBar) return;
        const mods = Array.from(Config.options.statusBar.rightModules);
        mods.unshift("archUpdates");
        Config.options.statusBar.rightModules = mods;
    }

    function removeFromBar() {
        if (!Config.ready) return;
        Config.options.statusBar.leftModules = Array.from(Config.options.statusBar.leftModules).filter(m => m !== "archUpdates");
        Config.options.statusBar.rightModules = Array.from(Config.options.statusBar.rightModules).filter(m => m !== "archUpdates");
    }

    onVisibleChanged: {
        // Refresh when the page opens and the last check is older than 10 minutes
        if (visible && Date.now() - ArchUpdates.lastChecked > 10 * 60 * 1000) ArchUpdates.check();
    }

    property string tab: "updates" // "updates" | "repo" | "aur" | "flatpak"

    // ── Tabs ──
    RowLayout {
        Layout.fillWidth: true
        spacing: 2 * Appearance.effectiveScale
        Repeater {
            model: [
                { id: "updates", label: I18nService.tr("Updates"), icon: "system_update_alt" },
                { id: "repo", label: I18nService.tr("Official"), icon: "package_2" },
                { id: "aur", label: "AUR", icon: "deployed_code_account" },
                { id: "flatpak", label: "Flatpak", icon: "apps" }
            ]
            delegate: SegmentedButton {
                required property var modelData
                Layout.fillWidth: true
                visible: modelData.id !== "flatpak" || PackageSearch.flatpakAvailable || ArchUpdates.flatpakAvailable
                buttonText: modelData.label + (modelData.id === "updates" && ArchUpdates.count > 0 ? "  " + ArchUpdates.count : "")
                isHighlighted: root.tab === modelData.id
                colActive: Appearance.m3colors.m3primary
                colActiveText: Appearance.m3colors.m3onPrimary
                colInactive: Appearance.m3colors.m3surfaceContainerHigh
                onClicked: root.tab = modelData.id
            }
        }
    }

    // ── Status hero ──
    Rectangle {
        id: hero
        visible: root.tab === "updates"
        Layout.fillWidth: true
        implicitHeight: heroCol.implicitHeight + 40 * Appearance.effectiveScale
        radius: 28 * Appearance.effectiveScale
        color: ArchUpdates.count > 0 ? Appearance.m3colors.m3primaryContainer : Appearance.m3colors.m3surfaceContainerHigh
        Behavior on color { ColorAnimation { duration: 250 } }

        readonly property color fg: ArchUpdates.count > 0 ? Appearance.m3colors.m3onPrimaryContainer : Appearance.colors.colOnLayer1

        ColumnLayout {
            id: heroCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 20 * Appearance.effectiveScale
            spacing: 14 * Appearance.effectiveScale

            RowLayout {
                spacing: 16 * Appearance.effectiveScale
                Layout.fillWidth: true

                Rectangle {
                    implicitWidth: 56 * Appearance.effectiveScale
                    implicitHeight: 56 * Appearance.effectiveScale
                    radius: width / 2
                    color: ArchUpdates.count > 0 ? Appearance.m3colors.m3primary : Appearance.m3colors.m3secondaryContainer
                    MaterialSymbol {
                        id: heroIcon
                        anchors.centerIn: parent
                        text: ArchUpdates.checking || ArchUpdates.upgrading ? "progress_activity"
                            : ArchUpdates.count > 0 ? "system_update_alt" : "verified"
                        iconSize: 28 * Appearance.effectiveScale
                        fill: 1
                        color: ArchUpdates.count > 0 ? Appearance.m3colors.m3onPrimary : Appearance.m3colors.m3onSecondaryContainer
                        RotationAnimation on rotation {
                            running: ArchUpdates.checking || ArchUpdates.upgrading
                            from: 0; to: 360; duration: 900; loops: Animation.Infinite
                            onRunningChanged: if (!running) heroIcon.rotation = 0
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2 * Appearance.effectiveScale
                    StyledText {
                        Layout.fillWidth: true
                        text: ArchUpdates.count > 0
                            ? (ArchUpdates.count === 1 ? I18nService.tr("1 update") : I18nService.tr("%1 updates").arg(ArchUpdates.count))
                            : I18nService.tr("System updates")
                        font.pixelSize: Appearance.font.pixelSize.huge
                        font.family: Appearance.font.family.title
                        font.weight: Font.DemiBold
                        color: hero.fg
                    }
                    StyledText {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: I18nService.tr(ArchUpdates.statusText)
                        font.pixelSize: Appearance.font.pixelSize.small
                        color: ArchUpdates.error ? Appearance.m3colors.m3error : hero.fg
                        opacity: 0.85
                    }
                    StyledText {
                        visible: ArchUpdates.lastChecked > 0
                        text: I18nService.tr("Last checked %1").arg(Qt.formatTime(new Date(ArchUpdates.lastChecked), Config.timeFormat))
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: hero.fg
                        opacity: 0.7
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8 * Appearance.effectiveScale

                ActionButton {
                    label: I18nService.tr("Check now")
                    icon: "refresh"
                    primary: false
                    enabled: !ArchUpdates.checking && !ArchUpdates.upgrading
                    onClicked: ArchUpdates.check()
                }
                ActionButton {
                    label: ArchUpdates.upgrading ? I18nService.tr("Updating…") : I18nService.tr("Update now")
                    icon: "download"
                    primary: true
                    enabled: !ArchUpdates.upgrading && !ArchUpdates.checking
                    onClicked: ArchUpdates.upgrade()
                }
                Item { Layout.fillWidth: true }
                StyledText {
                    visible: ArchUpdates.aurHelper !== ""
                    text: I18nService.tr("via %1").arg(ArchUpdates.aurHelper)
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colSubtext
                }
            }
        }
    }

    // ── Package list ──
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "updates" && ArchUpdates.count > 0
        spacing: 2 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Packages")
            font.pixelSize: Appearance.font.pixelSize.normal
            font.weight: Font.Medium
            color: Appearance.colors.colOnLayer1
            Layout.bottomMargin: 6 * Appearance.effectiveScale
        }

        Repeater {
            model: ArchUpdates.packages
            delegate: SegmentedWrapper {
                id: pkgRow
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 56 * Appearance.effectiveScale
                orientation: Qt.Vertical
                color: Appearance.m3colors.m3surfaceContainerHigh

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16 * Appearance.effectiveScale
                    anchors.rightMargin: 16 * Appearance.effectiveScale
                    spacing: 12 * Appearance.effectiveScale

                    MaterialSymbol {
                        text: pkgRow.modelData.source === "aur" ? "deployed_code_account" : pkgRow.modelData.source === "flatpak" ? "apps" : "package_2"
                        iconSize: 22 * Appearance.effectiveScale
                        color: pkgRow.modelData.source === "aur" ? Appearance.m3colors.m3tertiary : pkgRow.modelData.source === "flatpak" ? Appearance.m3colors.m3secondary : Appearance.colors.colPrimary
                    }
                    StyledText {
                        Layout.fillWidth: true
                        text: pkgRow.modelData.name
                        elide: Text.ElideRight
                        color: Appearance.colors.colOnLayer1
                    }
                    StyledText {
                        text: pkgRow.modelData.from ? pkgRow.modelData.from + "  →  " + pkgRow.modelData.to : pkgRow.modelData.to
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        font.family: Appearance.font.family.monospace ?? Appearance.font.family.main
                        color: Appearance.colors.colSubtext
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: 260 * Appearance.effectiveScale
                    }
                    Rectangle {
                        implicitWidth: srcLabel.implicitWidth + 16 * Appearance.effectiveScale
                        implicitHeight: 24 * Appearance.effectiveScale
                        radius: height / 2
                        color: pkgRow.modelData.source === "aur" ? Appearance.m3colors.m3tertiaryContainer : Appearance.m3colors.m3secondaryContainer
                        StyledText {
                            id: srcLabel
                            anchors.centerIn: parent
                            text: pkgRow.modelData.source === "aur" ? "AUR" : pkgRow.modelData.source === "flatpak" ? "Flatpak" : I18nService.tr("Official")
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            color: pkgRow.modelData.source === "aur" ? Appearance.m3colors.m3onTertiaryContainer : Appearance.m3colors.m3onSecondaryContainer
                        }
                    }
                }
            }
        }
    }

    // ── Find packages (official repos · AUR · Flatpak) ──
    ColumnLayout {
        id: finder
        Layout.fillWidth: true
        visible: root.tab !== "updates"
        spacing: 8 * Appearance.effectiveScale

        readonly property string sourceFilter: root.tab
        readonly property var shown: PackageSearch.results.filter(r => r.source === finder.sourceFilter)

        SearchHandler {
            visible: false
            searchString: "Install packages"
            aliases: ["Package search", "AUR", "Flatpak", "pacman", "Install"]
        }


        StyledTextInput {
            id: pkgQuery
            Layout.fillWidth: true
            implicitHeight: 52 * Appearance.effectiveScale
            inputRadius: 26
            leftMargin: 48
            placeholder: finder.sourceFilter === "aur" ? I18nService.tr("Search the AUR")
                : finder.sourceFilter === "flatpak" ? I18nService.tr("Search Flathub and your Flatpak remotes")
                : I18nService.tr("Search the official Arch repositories")
            onTextChanged: PackageSearch.search(text)

            MaterialSymbol {
                anchors.left: parent.left
                anchors.leftMargin: 16 * Appearance.effectiveScale
                anchors.verticalCenter: parent.verticalCenter
                text: PackageSearch.busy ? "progress_activity" : "search"
                iconSize: 22 * Appearance.effectiveScale
                color: Appearance.colors.colSubtext
            }
        }

        RowLayout {
            Layout.fillWidth: true
            StyledText {
                Layout.fillWidth: true
                text: PackageSearch.query.length < 2
                    ? (finder.sourceFilter === "aur" && !ArchUpdates.aurHelper
                        ? I18nService.tr("Install paru or yay to install AUR packages")
                        : I18nService.tr("Type at least two characters"))
                    : I18nService.tr("%1 results").arg(finder.shown.length)
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.colors.colSubtext
            }
            StyledText {
                visible: finder.sourceFilter === "aur" && PackageSearch.aurError !== ""
                text: PackageSearch.aurError
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.m3colors.m3error
            }
        }

        StyledText {
            visible: PackageSearch.query.length >= 2 && !PackageSearch.busy && finder.shown.length === 0
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8 * Appearance.effectiveScale
            text: I18nService.tr("No packages found")
            color: Appearance.colors.colSubtext
        }

        Repeater {
            model: finder.shown
            delegate: SegmentedWrapper {
                id: resultRow
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: Math.max(64 * Appearance.effectiveScale, resultLayout.implicitHeight + 20 * Appearance.effectiveScale)
                orientation: Qt.Vertical
                color: Appearance.m3colors.m3surfaceContainerHigh

                readonly property string src: modelData.source
                readonly property color srcColor: src === "aur" ? Appearance.m3colors.m3tertiary : src === "flatpak" ? Appearance.m3colors.m3secondary : Appearance.colors.colPrimary

                RowLayout {
                    id: resultLayout
                    anchors.fill: parent
                    anchors.leftMargin: 16 * Appearance.effectiveScale
                    anchors.rightMargin: 12 * Appearance.effectiveScale
                    spacing: 12 * Appearance.effectiveScale

                    MaterialSymbol {
                        text: resultRow.src === "aur" ? "deployed_code_account" : resultRow.src === "flatpak" ? "apps" : "package_2"
                        iconSize: 22 * Appearance.effectiveScale
                        color: resultRow.srcColor
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1 * Appearance.effectiveScale
                        RowLayout {
                            spacing: 8 * Appearance.effectiveScale
                            StyledText {
                                text: resultRow.modelData.name
                                font.weight: Font.Medium
                                color: Appearance.colors.colOnLayer1
                                elide: Text.ElideRight
                                Layout.maximumWidth: 260 * Appearance.effectiveScale
                            }
                            StyledText {
                                text: resultRow.modelData.version
                                font.pixelSize: Appearance.font.pixelSize.smaller
                                color: Appearance.colors.colSubtext
                            }
                            StyledText {
                                text: resultRow.src === "repo" ? resultRow.modelData.repo : resultRow.src === "aur" ? "AUR" : resultRow.modelData.remote
                                font.pixelSize: Appearance.font.pixelSize.smaller
                                font.weight: Font.Medium
                                color: resultRow.srcColor
                            }
                        }
                        StyledText {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: resultRow.modelData.description
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            color: Appearance.colors.colSubtext
                            elide: Text.ElideRight
                        }
                    }

                    // Installed badge or install button
                    Rectangle {
                        visible: resultRow.modelData.installed
                        implicitWidth: installedRow.implicitWidth + 20 * Appearance.effectiveScale
                        implicitHeight: 32 * Appearance.effectiveScale
                        radius: height / 2
                        color: Appearance.m3colors.m3secondaryContainer
                        RowLayout {
                            id: installedRow
                            anchors.centerIn: parent
                            spacing: 4 * Appearance.effectiveScale
                            MaterialSymbol { text: "check"; iconSize: 16 * Appearance.effectiveScale; color: Appearance.m3colors.m3onSecondaryContainer }
                            StyledText { text: I18nService.tr("Installed"); font.pixelSize: Appearance.font.pixelSize.smaller; color: Appearance.m3colors.m3onSecondaryContainer }
                        }
                    }
                    ActionButton {
                        visible: !resultRow.modelData.installed
                        label: PackageSearch.installing === resultRow.modelData.id ? I18nService.tr("Installing…") : I18nService.tr("Install")
                        icon: "download"
                        primary: true
                        enabled: PackageSearch.installing === "" && (resultRow.src !== "aur" || ArchUpdates.aurHelper !== "")
                        onClicked: PackageSearch.install(resultRow.modelData)
                    }
                }
            }
        }
    }

    // ── Settings ──
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.tab === "updates"
        spacing: 2 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Settings")
            font.pixelSize: Appearance.font.pixelSize.normal
            font.weight: Font.Medium
            color: Appearance.colors.colOnLayer1
            Layout.bottomMargin: 6 * Appearance.effectiveScale
        }

        ToggleRow {
            icon: "schedule"
            title: I18nService.tr("Check automatically")
            checked: ArchUpdates.autoCheck
            onToggled: Config.options.archUpdates.autoCheck = !Config.options.archUpdates.autoCheck
        }

        SegmentedWrapper {
            Layout.fillWidth: true
            visible: ArchUpdates.autoCheck
            implicitHeight: 64 * Appearance.effectiveScale
            orientation: Qt.Vertical
            color: Appearance.m3colors.m3surfaceContainerHigh
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16 * Appearance.effectiveScale
                anchors.rightMargin: 16 * Appearance.effectiveScale
                spacing: 16 * Appearance.effectiveScale
                MaterialSymbol { text: "timer"; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                StyledText { text: I18nService.tr("Check every"); Layout.fillWidth: true; color: Appearance.colors.colOnLayer1 }
                RowLayout {
                    spacing: 2 * Appearance.effectiveScale
                    Repeater {
                        model: [
                            { minutes: 30, label: "30m" }, { minutes: 60, label: "1h" },
                            { minutes: 180, label: "3h" }, { minutes: 360, label: "6h" }
                        ]
                        delegate: SegmentedButton {
                            required property var modelData
                            buttonText: modelData.label
                            isHighlighted: ArchUpdates.intervalMinutes === modelData.minutes
                            colActive: Appearance.m3colors.m3primary
                            colActiveText: Appearance.m3colors.m3onPrimary
                            colInactive: Appearance.m3colors.m3surfaceContainerLow
                            onClicked: if (Config.ready) Config.options.archUpdates.intervalMinutes = modelData.minutes
                        }
                    }
                }
            }
        }

        ToggleRow {
            icon: "deployed_code_account"
            title: I18nService.tr("Include AUR packages")
            subtitle: ArchUpdates.aurHelper ? "" : I18nService.tr("No AUR helper (paru or yay) found")
            checked: ArchUpdates.includeAur
            onToggled: { Config.options.archUpdates.includeAur = !Config.options.archUpdates.includeAur; ArchUpdates.check(); }
        }

        ToggleRow {
            visible: ArchUpdates.flatpakAvailable
            icon: "apps"
            title: I18nService.tr("Include Flatpak apps")
            checked: ArchUpdates.includeFlatpak
            onToggled: { Config.options.archUpdates.includeFlatpak = !Config.options.archUpdates.includeFlatpak; ArchUpdates.check(); }
        }

        ToggleRow {
            icon: "notifications"
            title: I18nService.tr("Notify when updates are found")
            checked: ArchUpdates.notify
            onToggled: Config.options.archUpdates.notify = !Config.options.archUpdates.notify
        }

        ToggleRow {
            icon: "widgets"
            title: I18nService.tr("Show updates in the status bar")
            subtitle: I18nService.tr("Left click opens this page, right click checks, middle click updates")
            checked: root.inBar
            onToggled: root.inBar ? root.removeFromBar() : root.addToBar()
        }

        ToggleRow {
            visible: root.inBar
            icon: "visibility"
            title: I18nService.tr("Keep the bar icon when up to date")
            checked: Config.ready && Config.options.archUpdates.showWhenUpToDate
            onToggled: Config.options.archUpdates.showWhenUpToDate = !Config.options.archUpdates.showWhenUpToDate
        }
    }

    // ── Components ──
    component ActionButton: RippleButton {
        id: btn
        property string label
        property string icon
        property bool primary: false
        implicitHeight: 40 * Appearance.effectiveScale
        implicitWidth: btnRow.implicitWidth + 32 * Appearance.effectiveScale
        buttonRadius: 20 * Appearance.effectiveScale
        opacity: enabled ? 1 : 0.5
        colBackground: primary ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerLow
        RowLayout {
            id: btnRow
            anchors.centerIn: parent
            spacing: 8 * Appearance.effectiveScale
            MaterialSymbol {
                text: btn.icon
                iconSize: 20 * Appearance.effectiveScale
                color: btn.primary ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer1
            }
            StyledText {
                text: btn.label
                font.pixelSize: Appearance.font.pixelSize.small
                font.weight: Font.Medium
                color: btn.primary ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer1
            }
        }
    }

    component ToggleRow: SegmentedWrapper {
        id: toggleRow
        property string icon
        property string title
        property string subtitle: ""
        property bool checked
        signal toggled()

        Layout.fillWidth: true
        implicitHeight: Math.max(64 * Appearance.effectiveScale, toggleRowLayout.implicitHeight + 20 * Appearance.effectiveScale)
        orientation: Qt.Vertical
        color: Appearance.m3colors.m3surfaceContainerHigh

        RippleButton {
            anchors.fill: parent
            colBackground: Appearance.m3colors.m3surfaceContainerHigh
            colBackgroundHover: Appearance.m3colors.m3surfaceContainerHigh
            buttonRadius: 0
            topLeftRadius: toggleRow.rTopLeft
            topRightRadius: toggleRow.rTopRight
            bottomLeftRadius: toggleRow.rBottomLeft
            bottomRightRadius: toggleRow.rBottomRight
            onClicked: if (Config.ready) toggleRow.toggled()
        }

        RowLayout {
            id: toggleRowLayout
            anchors.fill: parent
            anchors.leftMargin: 16 * Appearance.effectiveScale
            anchors.rightMargin: 16 * Appearance.effectiveScale
            spacing: 16 * Appearance.effectiveScale
            MaterialSymbol { text: toggleRow.icon; iconSize: 24 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2 * Appearance.effectiveScale
                StyledText { text: toggleRow.title; Layout.fillWidth: true; color: Appearance.colors.colOnLayer1 }
                StyledText {
                    visible: toggleRow.subtitle !== ""
                    text: toggleRow.subtitle
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colSubtext
                }
            }
            AndroidToggle {
                checked: toggleRow.checked
                onToggled: if (Config.ready) toggleRow.toggled()
            }
        }
    }
}
