pragma Singleton
pragma ComponentBehavior: Bound

import "../core"
import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    property bool active: false
    property int seconds: 0
    property string geometry: ""
    property int recordingMode: 0 // 0: Region (no audio), 1: Region (with audio), 2: Fullscreen (with audio)

    readonly property string modeLabel: {
        if (recordingMode === 0) return "Region";
        if (recordingMode === 1) return "Region + Audio";
        if (recordingMode === 2) return "Screen + Audio";
        return "Unknown";
    }

    function cycleMode() {
        recordingMode = (recordingMode + 1) % 3;
    }

    function toggle(region, sound, fullscreen) {
        root.active = true;
        let args = [Quickshell.shellPath("scripts/videos/record.sh")];
        if (region) {
            args.push("--region");
            args.push(region);
        }
        if (sound) args.push("--sound");
        if (fullscreen) args.push("--fullscreen");
        Quickshell.execDetached(args);
    }

    function stop() {
        Quickshell.execDetached([Quickshell.shellPath("scripts/videos/record.sh")]);
    }

    // pidof polling — Cuma jalan pas aktif, deteksi kapan wf-recorder mati
    Process {
        id: wfChecker
        command: ["pidof", "wf-recorder"]
        onExited: (exitCode, exitStatus) => {
            if (exitCode === 0) {
                if (!root.active) root.active = true;
            } else {
                if (root.active) {
                    root.active = false;
                    root.geometry = "";
                    root.seconds = 0;
                }
            }
        }
    }

    Timer {
        interval: 1000
        running: root.active
        repeat: true
        onTriggered: wfChecker.running = true
    }

    Timer {
        interval: 1000
        running: root.active
        repeat: true
        onTriggered: root.seconds++
    }

    // Startup: detect wf-recorder from previous session (e.g. shell restart)
    Component.onCompleted: wfChecker.running = true
}
