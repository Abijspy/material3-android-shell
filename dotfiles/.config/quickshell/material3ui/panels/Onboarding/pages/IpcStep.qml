import "../../../core"
import "../../../core/functions" as Functions
import "../../../widgets"
import "../../../services"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell

/**
 * IPC Step page for Onboarding.
 * Curated list of primary Material3UI IPC commands with category filter counts (Borderless M3 design).
 */
ColumnLayout {
    id: root
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 14 * Appearance.effectiveScale

    property string activeCategory: "All"
    property string searchQuery: ""

    // Helper to pick appropriate icons for IPC commands
    function getIpcIcon(target, method) {
        if (target === "launcher") return "apps";
        if (target === "spotlight") return "search";
        if (target === "notifications") return "notifications";
        if (target === "quicksettings") return "tune";
        if (target === "systemmonitor" || target === "sysmon") return "monitoring";
        if (target === "overview") return "space_dashboard";
        if (target === "session") return "power_settings_new";
        if (target === "dashboard") return "dashboard";
        if (target === "quickactions") return "flash_on";
        if (target === "settings") return "settings";
        if (target === "lock") return "lock";
        if (target === "region") {
            if (method === "screenshot") return "crop";
            if (method === "search") return "saved_search";
            if (method === "ocr") return "document_scanner";
            if (method === "qrcode") return "qr_code_scanner";
            if (method.startsWith("record")) return "videocam";
        }
        if (target === "brightness") return "brightness_6";
        if (target === "pomodoro") return "timer";
        if (target === "wallpaper") return "wallpaper";
        return "terminal";
    }

    // Curated primary IPC items list with readable names
    readonly property var allIpcItems: [
        { name: "Toggle App Launcher", target: "launcher", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call launcher toggle" },
        { name: "Toggle Spotlight Search", target: "spotlight", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call spotlight toggle" },
        { name: "Toggle Notification Center", target: "notifications", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call notifications toggle" },
        { name: "Toggle Quick Settings", target: "quicksettings", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call quicksettings toggle" },
        { name: "Toggle System Monitor", target: "systemmonitor", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call systemmonitor toggle" },
        { name: "Toggle Workspace Overview", target: "overview", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call overview toggle" },
        { name: "Toggle Session Menu", target: "session", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call session toggle" },
        { name: "Toggle Dashboard", target: "dashboard", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call dashboard toggle" },
        { name: "Toggle Quick Actions", target: "quickactions", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call quickactions toggle" },
        { name: "Toggle Settings Window", target: "settings", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call settings toggle" },
        { name: "Toggle Wallpaper Selector", target: "wallpaper", method: "toggle", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call wallpaper toggle" },
        { name: "Select Desktop Wallpaper", target: "wallpaper", method: "openDesktop", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call wallpaper openDesktop" },
        { name: "Select Lockscreen Wallpaper", target: "wallpaper", method: "openLock", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call wallpaper openLock" },
        { name: "Lock Screen", target: "lock", method: "lock", category: "Sidebar & Panels", cmd: "quickshell -c material3ui ipc call lock lock" },

        { name: "Take Region Screenshot", target: "region", method: "screenshot", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region screenshot" },
        { name: "Google Lens Region Search", target: "region", method: "search", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region search" },
        { name: "OCR Screen Text", target: "region", method: "ocr", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region ocr" },
        { name: "Scan Region QR Code", target: "region", method: "qrcode", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region qrcode" },
        { name: "Record Selected Region", target: "region", method: "record", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region record" },
        { name: "Record Region + Audio", target: "region", method: "recordWithSound", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region recordWithSound" },
        { name: "Record Fullscreen + Audio", target: "region", method: "recordFullscreenWithSound", category: "Region Tools", cmd: "quickshell -c material3ui ipc call region recordFullscreenWithSound" },

        { name: "Increase Display Brightness", target: "brightness", method: "increase", category: "Media & System", cmd: "quickshell -c material3ui ipc call brightness increase" },
        { name: "Decrease Display Brightness", target: "brightness", method: "decrease", category: "Media & System", cmd: "quickshell -c material3ui ipc call brightness decrease" },
        { name: "Toggle Pomodoro Timer", target: "pomodoro", method: "toggle", category: "Media & System", cmd: "quickshell -c material3ui ipc call pomodoro toggle" },
        { name: "Select Wallpaper Accent Color", target: "wallpaper", method: "pickAccent", category: "Media & System", cmd: "quickshell -c material3ui ipc call wallpaper pickAccent" }
    ]

    readonly property var filteredIpcItems: {
        return allIpcItems.filter(item => {
            const matchesCat = root.activeCategory === "All" || item.category === root.activeCategory;
            const q = root.searchQuery.toLowerCase().trim();
            const matchesSearch = q === "" || item.name.toLowerCase().includes(q) || item.cmd.toLowerCase().includes(q) || item.category.toLowerCase().includes(q);
            return matchesCat && matchesSearch;
        });
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4 * Appearance.effectiveScale

        StyledText {
            text: I18nService.tr("Step 4: Command Line & IPC Integration")
            font.pixelSize: Appearance.font.pixelSize.larger
            font.family: Appearance.font.family.title
            font.weight: Font.DemiBold
            color: Appearance.colors.colOnLayer1
        }

        StyledText {
            text: I18nService.tr("Material3UI IPC allows instant binding in your Window Manager. Search and test commands in real time!")
            font.pixelSize: Appearance.font.pixelSize.small
            color: Appearance.colors.colSubtext
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    function getCategoryCount(cat) {
        if (cat === "All") return root.allIpcItems.length;
        return root.allIpcItems.filter(i => i.category === cat).length;
    }

    function getCategoryLabel(cat) {
        const count = root.getCategoryCount(cat);
        return `${I18nService.tr(cat)} (${count})`;
    }

    // ── Toolbar Header (SegmentedButtons + Search Input Pill) ──
    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 6 * Appearance.effectiveScale
        spacing: 12 * Appearance.effectiveScale

        // Category Filter (Material 3 SegmentedButton Group)
        RowLayout {
            spacing: 2 * Appearance.effectiveScale
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 36 * Appearance.effectiveScale

            Repeater {
                model: ["All", "Sidebar & Panels", "Region Tools", "Media & System"]
                delegate: SegmentedButton {
                    required property string modelData
                    isHighlighted: root.activeCategory === modelData
                    implicitHeight: 36 * Appearance.effectiveScale
                    buttonText: root.getCategoryLabel(modelData)
                    leftPadding: 14 * Appearance.effectiveScale
                    rightPadding: 14 * Appearance.effectiveScale
                    colActive: Appearance.colors.colPrimary
                    colActiveText: Appearance.colors.colOnPrimary
                    colInactive: Appearance.m3colors.m3surfaceContainerHigh
                    colInactiveText: Appearance.colors.colOnLayer1
                    onClicked: root.activeCategory = modelData
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Search Input Pill
        Rectangle {
            Layout.preferredWidth: 230 * Appearance.effectiveScale
            Layout.preferredHeight: 36 * Appearance.effectiveScale
            radius: 18 * Appearance.effectiveScale
            color: Appearance.m3colors.m3surfaceContainerHigh

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12 * Appearance.effectiveScale
                anchors.rightMargin: 12 * Appearance.effectiveScale
                spacing: 8 * Appearance.effectiveScale

                MaterialSymbol {
                    text: "search"
                    iconSize: 16 * Appearance.effectiveScale
                    color: Appearance.colors.colSubtext
                    Layout.alignment: Qt.AlignVCenter
                }

                StyledTextInput {
                    id: searchInput
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    inputRadius: 0
                    backgroundColor: "transparent"
                    borderInactiveWidth: 0
                    showActiveBorder: false
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    placeholder: I18nService.tr("Search IPC commands...")
                    placeholderColor: Appearance.colors.colSubtext
                    leftMargin: 0
                    rightMargin: 0
                    onTextChanged: root.searchQuery = text
                }

                MaterialSymbol {
                    visible: searchInput.text !== ""
                    text: "close"
                    iconSize: 14 * Appearance.effectiveScale
                    color: Appearance.colors.colSubtext
                    Layout.alignment: Qt.AlignVCenter

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchInput.text = "";
                            root.searchQuery = "";
                        }
                    }
                }
            }
        }
    }

    // ── Code Bind Example Banner ──
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 36 * Appearance.effectiveScale
        color: Appearance.m3colors.m3surfaceContainerHigh
        radius: 10 * Appearance.effectiveScale

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14 * Appearance.effectiveScale
            anchors.rightMargin: 14 * Appearance.effectiveScale
            spacing: 8 * Appearance.effectiveScale

            MaterialSymbol {
                text: "terminal"
                iconSize: 16 * Appearance.effectiveScale
                color: Appearance.colors.colPrimary
            }
            StyledText {
                text: I18nService.tr("WM Bind Example (Hyprland):")
                font.weight: Font.DemiBold
                font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                color: Appearance.colors.colOnLayer0
            }
            StyledText {
                text: 'hl.bind("SUPER + I", hl.dsp.exec_cmd("quickshell -c material3ui ipc call settings toggle"))'
                font.family: "monospace"
                font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                color: Appearance.colors.colPrimary
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    // ── IPC Items List View ──
    ListView {
        id: ipcList
        Layout.fillWidth: true
        Layout.fillHeight: true
        model: root.filteredIpcItems
        clip: true
        spacing: 10 * Appearance.effectiveScale
        boundsBehavior: Flickable.StopAtBounds

        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            implicitHeight: 52 * Appearance.effectiveScale
            radius: 12 * Appearance.effectiveScale
            color: itemHover.hovered ? Appearance.colors.colLayer2Hover : Appearance.m3colors.m3surfaceContainerHigh

            HoverHandler {
                id: itemHover
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16 * Appearance.effectiveScale
                anchors.rightMargin: 16 * Appearance.effectiveScale
                spacing: 14 * Appearance.effectiveScale

                // Action Icon
                Rectangle {
                    width: 34 * Appearance.effectiveScale
                    height: 34 * Appearance.effectiveScale
                    radius: 17 * Appearance.effectiveScale
                    color: Functions.ColorUtils.applyAlpha(Appearance.colors.colPrimary, 0.12)

                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: root.getIpcIcon(modelData.target, modelData.method)
                        iconSize: 18 * Appearance.effectiveScale
                        color: Appearance.colors.colPrimary
                    }
                }

                // Name & Command
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2 * Appearance.effectiveScale

                    RowLayout {
                        spacing: 8 * Appearance.effectiveScale
                        StyledText {
                            text: I18nService.tr(modelData.name)
                            font.weight: Font.DemiBold
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            color: Appearance.colors.colOnLayer1
                        }
                        Rectangle {
                            implicitWidth: catLabel.implicitWidth + 10 * Appearance.effectiveScale
                            implicitHeight: 18 * Appearance.effectiveScale
                            radius: 9 * Appearance.effectiveScale
                            color: Appearance.colors.colLayer2
                            StyledText {
                                id: catLabel
                                anchors.centerIn: parent
                                text: I18nService.tr(modelData.category)
                                font.pixelSize: Math.round(9 * Appearance.effectiveScale)
                                color: Appearance.colors.colSubtext
                            }
                        }
                    }

                    StyledText {
                        text: modelData.cmd
                        font.family: "monospace"
                        font.pixelSize: Appearance.font.pixelSize.smallest
                        color: Appearance.colors.colSubtext
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                // Copy Button
                Rectangle {
                    width: 32 * Appearance.effectiveScale
                    height: 32 * Appearance.effectiveScale
                    radius: 16 * Appearance.effectiveScale
                    color: copyMouse.containsMouse ? Appearance.colors.colLayer2Hover : Appearance.colors.colLayer2

                    MaterialSymbol {
                        anchors.centerIn: parent
                        text: "content_copy"
                        iconSize: 14 * Appearance.effectiveScale
                        color: copyMouse.containsMouse ? Appearance.colors.colPrimary : Appearance.colors.colOnLayer1
                    }
                    MouseArea {
                        id: copyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: {
                            Quickshell.execDetached(["wl-copy", modelData.cmd]);
                            SnackbarService.show(I18nService.tr("IPC Command copied to clipboard!"));
                        }
                    }
                }

                // Run Button
                RippleButton {
                    implicitWidth: 68 * Appearance.effectiveScale
                    implicitHeight: 32 * Appearance.effectiveScale
                    buttonRadius: 16 * Appearance.effectiveScale
                    colBackground: Appearance.colors.colPrimary
                    onClicked: {
                        Quickshell.execDetached(["quickshell", "-c", "material3ui", "ipc", "call", modelData.target, modelData.method]);
                    }

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4 * Appearance.effectiveScale

                        MaterialSymbol {
                            text: "play_arrow"
                            iconSize: 14 * Appearance.effectiveScale
                            color: Appearance.colors.colOnPrimary
                        }
                        StyledText {
                            text: I18nService.tr("Run")
                            font.pixelSize: Math.round(11 * Appearance.effectiveScale)
                            font.weight: Font.DemiBold
                            color: Appearance.colors.colOnPrimary
                        }
                    }
                }
            }
        }
    }
}
