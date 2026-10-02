import Quickshell
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell.Io
import Quickshell.Hyprland

ShellRoot {
    id: shell
    property bool launcherOpen: false
    property bool controlOpen: false
    property bool notificationsOpen: false
    property bool clipboardOpen: false
    property bool powerOpen: false
    property bool settingsOpen: false
    property bool wifi: true
    property bool bluetooth: true
    property bool dnd: false
    property int volume: 62
    property int balance: 0
    property int brightness: 76
    property string batteryInfo: "Checking battery…"
    property string aboutInfo: "Loading system details…"
    property string activeWindow: "Desktop"
    property var barWidgets: ({})
    property bool barVisible: true
    property real glassOpacity: 0.88
    property int barHeight: 46
    property string barPosition: "top"
    property string page: "Connected devices"
    GeneratedColors { id: palette }
    readonly property color primary: palette.primary
    readonly property color onPrimary: palette.onPrimary
    readonly property color primaryContainer: palette.primaryContainer
    readonly property color surface: palette.surface
    readonly property color surfaceContainer: palette.surfaceContainer
    readonly property color surfaceHigh: palette.surfaceContainerHigh
    readonly property color glass: Qt.rgba(surfaceContainer.r, surfaceContainer.g, surfaceContainer.b, glassOpacity)
    readonly property color text: palette.onSurface
    readonly property color muted: palette.onSurfaceVariant
    readonly property color outline: palette.outline

    function closeOverlays() { launcherOpen = false; controlOpen = false; notificationsOpen = false; clipboardOpen = false; powerOpen = false }
    function toggle(which) { closeOverlays(); if (which === "launcher") launcherOpen = true; if (which === "control") controlOpen = true; if (which === "notifications") notificationsOpen = true; if (which === "clipboard") clipboardOpen = true; if (which === "power") powerOpen = true }
    function run(command) { Quickshell.execDetached(["sh", "-lc", command]) }
    function widgetEnabled(name) { return barWidgets[name] !== false }
    function setBarWidget(name, enabled) { if (name === "visible") barVisible = enabled; else barWidgets = Object.assign({}, barWidgets, { [name]: enabled }) }
    function setBarStyle(name, value) { if (name === "opacity") glassOpacity = Number(value); if (name === "height") barHeight = Number(value); if (name === "position") barPosition = value }
    function refreshSystemInfo() { batteryInfo = ""; batteryQuery.running = true; aboutInfo = ""; aboutQuery.running = true }

    Component.onCompleted: { refreshSystemInfo(); barConfigQuery.running = true; activeWindowQuery.running = true }
    Timer { interval: 60000; running: true; repeat: true; onTriggered: { shell.batteryInfo = ""; batteryQuery.running = true } }
    Process { id: batteryQuery; command: ["sh", "-lc", "material3ui-system battery"]
        stdout: SplitParser { onRead: data => shell.batteryInfo += (shell.batteryInfo ? " · " : "") + data }
    }
    Process { id: aboutQuery; command: ["sh", "-lc", "material3ui-system about"]
        stdout: SplitParser { onRead: data => shell.aboutInfo += (shell.aboutInfo ? "\n" : "") + data }
    }
    Process { id: barConfigQuery; command: ["sh", "-lc", "cat ~/.config/material3ui/bar.conf 2>/dev/null || true"]
        stdout: SplitParser { onRead: data => { const pair = data.split("="); if (pair.length === 2) { if (["opacity", "height", "position"].includes(pair[0])) shell.setBarStyle(pair[0], pair[1]); else shell.setBarWidget(pair[0], pair[1] !== "false") } } }
    }
    Timer { interval: 2000; running: true; repeat: true; onTriggered: { activeWindow = "Desktop"; activeWindowQuery.running = true } }
    Process { id: activeWindowQuery; command: ["sh", "-lc", "hyprctl activewindow -j 2>/dev/null | jq -r '.title // \"Desktop\"'"]
        stdout: SplitParser { onRead: data => { if (data.length > 0) shell.activeWindow = data } }
    }

    // Public control plane. Examples:
    // quickshell -c material3ui ipc call material3ui controlCenter
    // quickshell -c material3ui ipc call material3ui settings "Sound & vibration"
    IpcHandler {
        target: "material3ui"
        function launcher() { shell.toggle("launcher") }
        function controlCenter() { shell.toggle("control") }
        function notifications() { shell.toggle("notifications") }
        function clipboard() { shell.toggle("clipboard") }
        function power() { shell.toggle("power") }
        function close() { shell.closeOverlays(); shell.settingsOpen = false }
        function settings(section: string) {
            shell.closeOverlays()
            if (section !== undefined && section !== "") shell.page = section
            shell.settingsOpen = true
        }
        function wifi(enabled: string) { shell.wifi = enabled === "on" || enabled === "true" }
        function bluetooth(enabled: string) { shell.bluetooth = enabled === "on" || enabled === "true" }
        function reloadColours() { Quickshell.reload() }
        function barWidget(name: string, enabled: string) { shell.setBarWidget(name, enabled === "true"); shell.run("material3ui-system bar-widget " + name + " " + enabled) }
        function volume(value: string) { shell.volume = Math.max(0, Math.min(100, Number(value))) }
        function brightness(value: string) { shell.brightness = Math.max(0, Math.min(100, Number(value))) }
    }

    // Hyprland-native shortcuts avoid spawning another shell process for panels.
    GlobalShortcut { appid: "material3ui"; name: "launcher"; description: "Open Material3UI launcher"; onPressed: shell.toggle("launcher") }
    GlobalShortcut { appid: "material3ui"; name: "controlCenter"; description: "Open quick settings"; onPressed: shell.toggle("control") }
    GlobalShortcut { appid: "material3ui"; name: "notifications"; description: "Open notifications"; onPressed: shell.toggle("notifications") }
    GlobalShortcut { appid: "material3ui"; name: "clipboard"; description: "Open clipboard"; onPressed: shell.toggle("clipboard") }
    GlobalShortcut { appid: "material3ui"; name: "power"; description: "Open power menu"; onPressed: shell.toggle("power") }
    GlobalShortcut { appid: "material3ui"; name: "settings"; description: "Open Material3UI settings"; onPressed: { shell.closeOverlays(); shell.settingsOpen = true } }

    PanelWindow {
        visible: shell.barVisible
        WlrLayershell.namespace: "material3ui"
        anchors { top: shell.barPosition === "top"; bottom: shell.barPosition === "bottom"; left: true; right: true }
        implicitHeight: shell.barHeight
        color: "transparent"
        exclusionMode: ExclusionMode.Auto
        Rectangle { anchors.fill: parent; color: Qt.rgba(shell.surface.r, shell.surface.g, shell.surface.b, shell.glassOpacity)
            RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 12
                ToolButton { visible: shell.widgetEnabled("launcher"); text: "◈"; font.pixelSize: 23; onClicked: shell.toggle("launcher") }
                Row { visible: shell.widgetEnabled("workspaces"); spacing: 5; Repeater { model: 5; delegate: Button { text: index + 1; implicitWidth: 27; implicitHeight: 27; checkable: true; checked: Hyprland.focusedWorkspace && Hyprland.focusedWorkspace.id === index + 1; onClicked: Hyprland.dispatch("workspace " + (index + 1)) } } }
                Label { visible: shell.widgetEnabled("activeWindow"); text: shell.activeWindow; color: shell.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.maximumWidth: 180 }
                Rectangle { width: 1; height: 22; color: shell.outline }
                Item { Layout.fillWidth: true }
                DynamicIsland { visible: shell.widgetEnabled("dynamicIsland"); Layout.alignment: Qt.AlignHCenter }
                Item { Layout.fillWidth: true }
                ToolButton { visible: shell.widgetEnabled("screenshot"); text: "▣"; font.pixelSize: 18; onClicked: shell.run("material3ui-system screenshot") }
                ToolButton { visible: shell.widgetEnabled("clipboard"); text: "▤"; font.pixelSize: 18; onClicked: shell.toggle("clipboard") }
                ToolButton { visible: shell.widgetEnabled("notifications"); text: "◌  3"; onClicked: shell.toggle("notifications") }
                ToolButton { visible: shell.widgetEnabled("quickSettings"); text: "▣"; onClicked: shell.toggle("control") }
                Label { visible: shell.widgetEnabled("clock"); text: Qt.formatDateTime(new Date(), "ddd, MMM d   hh:mm"); color: shell.text }
                Label { visible: shell.widgetEnabled("battery"); text: shell.batteryInfo.split(" · ")[1] || ""; color: shell.muted; font.pixelSize: 12 }
                ToolButton { visible: shell.widgetEnabled("profile"); text: "A"; onClicked: shell.toggle("control") }
            }
        }
    }

    PopupWindow { WlrLayershell.namespace: "material3ui"; visible: shell.launcherOpen; anchor.window: null; anchor.rect.x: 18; anchor.rect.y: 56; implicitWidth: 520; implicitHeight: 560
        color: "transparent"
        Launcher { anchors.fill: parent; onClose: shell.closeOverlays(); onSettings: { shell.closeOverlays(); shell.settingsOpen = true } }
    }
    PopupWindow { WlrLayershell.namespace: "material3ui"; visible: shell.controlOpen; anchor.window: null; anchor.rect.x: 0; anchor.rect.y: 56; implicitWidth: 1; implicitHeight: 1
        ControlCenter { x: Screen.width - width - 16; width: 430; height: 670; onClose: shell.closeOverlays() }
    }
    PopupWindow { WlrLayershell.namespace: "material3ui"; visible: shell.notificationsOpen; anchor.window: null; anchor.rect.x: 0; anchor.rect.y: 56; implicitWidth: 1; implicitHeight: 1
        NotificationPanel { x: Screen.width - width - 16; width: 390; height: 470; onClose: shell.closeOverlays() }
    }
    PopupWindow { WlrLayershell.namespace: "material3ui"; visible: shell.clipboardOpen; anchor.window: null; anchor.rect.x: 0; anchor.rect.y: 56; implicitWidth: 1; implicitHeight: 1
        ClipboardPanel { x: Screen.width - width - 16; width: 390; height: 430; onClose: shell.closeOverlays() }
    }
    PopupWindow { WlrLayershell.namespace: "material3ui"; visible: shell.powerOpen; anchor.window: null; anchor.rect.x: Screen.width / 2 - 210; anchor.rect.y: Screen.height / 2 - 145; implicitWidth: 420; implicitHeight: 290
        PowerPanel { anchors.fill: parent; onClose: shell.closeOverlays() }
    }
    FloatingWindow { visible: shell.settingsOpen; title: "Material3UI Settings"; minimumWidth: 980; minimumHeight: 680; implicitWidth: 1120; implicitHeight: 760
        HyprlandWindow.opacity: shell.glassOpacity
        Settings { anchors.fill: parent; onClose: shell.settingsOpen = false }
    }
}
