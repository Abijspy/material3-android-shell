pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

/**
 * Keeps the greetd login screen (material3ui-greeter) looking like the lock
 * screen. When auto sync is on, any change to the generated Material colours
 * or to the shell config (wallpaper, avatar, clock style, time format...)
 * triggers `material3ui-greeter sync` after a short debounce.
 */
Singleton {
    id: root

    readonly property string cli: "/usr/local/bin/material3ui-greeter"
    readonly property string dataDir: "/var/lib/material3ui-greeter"

    property bool installed: false
    property bool writable: false
    property bool syncing: false
    property string lastResult: ""      // "ok" | "error"
    property string lastMessage: ""
    property real lastSyncTime: 0

    readonly property bool autoSync: Config.ready && (Config.options.lock?.greeter?.autoSync ?? true)

    function refresh() {
        probe.running = false;
        probe.running = true;
    }

    function syncNow() {
        if (!root.installed || root.syncing) return;
        debounce.stop();
        root.syncing = true;
        syncProc.running = true;
    }

    function preview() {
        if (root.installed) Quickshell.execDetached([root.cli, "preview"]);
    }

    function schedule() {
        if (root.autoSync && root.installed && root.writable) debounce.restart();
    }

    Component.onCompleted: refresh()
    onAutoSyncChanged: if (autoSync) schedule()

    Process {
        id: probe
        command: ["sh", "-c", 'test -x "$1" && test -d "$2" || exit 2; test -w "$2" || exit 3', "sh", root.cli, root.dataDir]
        onExited: code => {
            root.installed = code !== 2;
            root.writable = code === 0;
            if (code === 0) root.schedule();
        }
    }

    Timer {
        id: debounce
        interval: 4000
        onTriggered: root.syncNow()
    }

    Process {
        id: syncProc
        command: [root.cli, "sync", "--quiet"]
        stderr: StdioCollector { id: syncErr }
        onExited: code => {
            root.syncing = false;
            root.lastResult = code === 0 ? "ok" : "error";
            root.lastMessage = code === 0 ? "" : syncErr.text.replace(/\x1b\[[0-9;]*m/g, "").replace(/^✗\s*/, "").trim();
            if (code === 0) root.lastSyncTime = Date.now();
        }
    }

    // Wallpaper colours change on every wallpaper / scheme switch
    FileView {
        path: Directories.generatedMaterialThemePath
        watchChanges: true
        onFileChanged: root.schedule()
    }
    FileView {
        path: Directories.generatedLockColorsPath
        watchChanges: true
        onFileChanged: root.schedule()
    }
    // Wallpaper paths, avatar, clock style and time format live in config.json
    FileView {
        path: Directories.shellConfigPath
        watchChanges: true
        onFileChanged: root.schedule()
    }
}
