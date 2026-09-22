import Quickshell
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quickshell.Io

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
    property string page: "Connected devices"
    readonly property color primary: "#b9c4ff"
    readonly property color onPrimary: "#1e2860"
    readonly property color primaryContainer: "#364582"
    readonly property color surface: "#111318"
    readonly property color surfaceContainer: "#1d2027"
    readonly property color surfaceHigh: "#282b33"
    readonly property color glass: Qt.rgba(surfaceContainer.r, surfaceContainer.g, surfaceContainer.b, 0.88)
    readonly property color text: "#e2e2e9"
    readonly property color muted: "#c3c6d0"
    readonly property color outline: "#8d9099"

    function closeOverlays() { launcherOpen = false; controlOpen = false; notificationsOpen = false; clipboardOpen = false; powerOpen = false }
    function toggle(which) { closeOverlays(); if (which === "launcher") launcherOpen = true; if (which === "control") controlOpen = true; if (which === "notifications") notificationsOpen = true; if (which === "clipboard") clipboardOpen = true; if (which === "power") powerOpen = true }
    function run(command) { Quickshell.execDetached(["sh", "-lc", command]) }
    function refreshSystemInfo() { batteryInfo = ""; batteryQuery.running = true; aboutInfo = ""; aboutQuery.running = true }

    Component.onCompleted: refreshSystemInfo()
    Timer { interval: 60000; running: true; repeat: true; onTriggered: { shell.batteryInfo = ""; batteryQuery.running = true } }
    Process { id: batteryQuery; command: ["sh", "-lc", "material3ui-system battery"]
        stdout: SplitParser { onRead: data => shell.batteryInfo += (shell.batteryInfo ? " · " : "") + data }
    }
    Process { id: aboutQuery; command: ["sh", "-lc", "material3ui-system about"]
        stdout: SplitParser { onRead: data => shell.aboutInfo += (shell.aboutInfo ? "\n" : "") + data }
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
        function volume(value: string) { shell.volume = Math.max(0, Math.min(100, Number(value))) }
        function brightness(value: string) { shell.brightness = Math.max(0, Math.min(100, Number(value))) }
    }

    PanelWindow {
        WlrLayershell.namespace: "material3ui"
        anchors { top: true; left: true; right: true }
        implicitHeight: 46
        color: shell.surface
        exclusionMode: ExclusionMode.Auto
        Rectangle { anchors.fill: parent; color: Qt.rgba(shell.surface.r, shell.surface.g, shell.surface.b, 0.84)
            RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 12
                ToolButton { text: "◈"; font.pixelSize: 23; onClicked: shell.toggle("launcher") }
                Row { spacing: 5; Repeater { model: 5; delegate: Button { text: index + 1; implicitWidth: 27; implicitHeight: 27; checkable: true; checked: index === 0; onClicked: { } } } }
                Rectangle { width: 1; height: 22; color: shell.outline }
                Label { text: "No media playing  ·  Desktop"; color: shell.muted; Layout.fillWidth: true; elide: Text.ElideRight }
                ToolButton { text: "◌  3"; onClicked: shell.toggle("notifications") }
                ToolButton { text: "▣"; onClicked: shell.toggle("control") }
                Label { text: Qt.formatDateTime(new Date(), "ddd, MMM d   hh:mm"); color: shell.text }
                ToolButton { text: shell.batteryInfo === "" ? "A" : "⌁"; onClicked: shell.toggle("control") }
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
        Settings { anchors.fill: parent; onClose: shell.settingsOpen = false }
    }
}
