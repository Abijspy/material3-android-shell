pragma Singleton
pragma ComponentBehavior: Bound

import "../core"
import QtQuick
import Quickshell
import Quickshell.Io

Singleton {
    id: root

    enum MonitorSource { Monitor, Input }

    property var monitorSource: SongRec.MonitorSource.Monitor
    property int timeoutInterval: (Config.ready && Config.options.musicRecognition) ? Config.options.musicRecognition.interval : 2
    property int timeoutDuration: (Config.ready && Config.options.musicRecognition) ? Config.options.musicRecognition.timeout : 30
    readonly property bool running: recognizeMusicProc.running

    function toggleRunning(runningState) {
        if (recognizeMusicProc.running && runningState === false) root.manuallyStopped = true;
        if (runningState !== undefined) {
            recognizeMusicProc.running = runningState
        } else {
            recognizeMusicProc.running = !recognizeMusicProc.running
        }
        musicRecognizedProc.running = false
    }

    function toggleMonitorSource(source) {
        if (source !== undefined) {
            root.monitorSource = source
            return
        }
        root.monitorSource = (root.monitorSource === SongRec.MonitorSource.Monitor) ? SongRec.MonitorSource.Input : SongRec.MonitorSource.Monitor
    }

    function monitorSourceToString(src) {
        return src === SongRec.MonitorSource.Monitor ? "monitor" : "input"
    }

    readonly property string monitorSourceString: monitorSourceToString(monitorSource)
    readonly property string material3uiIcon: Directories.home.replace("file://", "") + "/.config/quickshell/material3ui/assets/icons/Material3UI.svg"
    property var recognizedTrack: ({ title:"", subtitle:"", url:""})
    property bool manuallyStopped: false

    function handleRecognition(jsonText) {
        try {
            var obj = JSON.parse(jsonText);
            if (obj.track) {
                root.recognizedTrack = {
                    title: obj.track.title,
                    subtitle: obj.track.subtitle,
                    url: obj.track.url
                }
                musicRecognizedProc.running = true
            }
        } catch(e) {
            SnackbarService.show(I18nService.tr("Couldn't recognize music") + " — " + I18nService.tr("Perhaps what you're listening to is too niche"));
        }
    }

    Process {
        id: recognizeMusicProc
        running: false
        command: [Quickshell.shellPath("scripts/musicRecognition/recognize-music.sh"), "-i", root.timeoutInterval, "-t", root.timeoutDuration, "-s", root.monitorSourceString]
        stdout: StdioCollector {
            onStreamFinished: {
                if (root.manuallyStopped) {
                    root.manuallyStopped = false
                    return
                }
                if (this.text.trim() !== "") {
                    handleRecognition(this.text)
                }
            }
        }
        onExited: (exitCode, exitStatus) => {
            if (exitCode === 1) {
                SnackbarService.show(I18nService.tr("Couldn't recognize music") + " — " + I18nService.tr("Make sure you have songrec installed"));
            }
        }
    }

    Process {
        id: musicRecognizedProc
        running: false
        command: [
            "notify-send",
            "-A", "Shazam",
            "-A", "YouTube",
            "-a", "Material3UI",
            "-i", root.material3uiIcon,
            "-t", "10000",
            "-e", // Transient
            "--",
            "Music Recognized", 
            root.recognizedTrack.title + " - " + root.recognizedTrack.subtitle
        ]
        stdout: StdioCollector {
            onStreamFinished: {
                if (this.text === "") return
                if (this.text.trim() == "0") {
                    Qt.openUrlExternally(root.recognizedTrack.url);
                } else {
                    Qt.openUrlExternally("https://www.youtube.com/results?search_query=" + encodeURIComponent(root.recognizedTrack.title + " - " + root.recognizedTrack.subtitle));
                }
            }
        }
    }
}
