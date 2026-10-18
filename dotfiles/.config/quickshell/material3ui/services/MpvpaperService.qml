pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

/**
 * Service for video wallpapers backed by mpvpaper (mpv).
 * Scans video files (default ~/Videos/Wallpapers + static wallpaper folders),
 * generates ffmpeg thumbnails on demand and manages the mpvpaper process.
 */
Singleton {
    id: root

    property bool loading: false
    property string errorMessage: ""
    property bool isInstalled: false
    property bool isRunning: activeProcess.running
    property bool isPaused: false
    property bool isApplying: false

    // Settings (bound to Config)
    property int volume: Config.ready ? Config.options.mpvpaper.volume : 15
    property bool mute: Config.ready ? Config.options.mpvpaper.mute : false
    property bool autoPause: Config.ready ? Config.options.mpvpaper.autoPause : true
    property string scaling: Config.ready ? Config.options.mpvpaper.scaling : "fill"
    property real speed: Config.ready ? Config.options.mpvpaper.speed : 1.0

    readonly property bool active: (Config.ready && Config.options.appearance.background.liveWallpaperBackend === "mpvpaper" && Config.options.appearance.background.liveWallpaperPath !== "") || isRunning

    // Default video folder
    property string videoDir: Directories.videos.toString().replace("file://", "") + "/Wallpapers"

    readonly property string cacheDir: Directories.home.replace("file://", "") + "/.cache/material3ui/mpvpaper_thumbs"
    readonly property string ipcSocket: "/tmp/mpvpaper-material3ui.sock"
    readonly property string framePath: "/tmp/material3ui_mpvpaper.png"
    property int frameVersion: 0

    property int maxConcurrentThumbs: 3

    readonly property ListModel allResults: ListModel { id: allResultsModel }
    readonly property ListModel results: ListModel { id: resultsModel }

    property string searchQuery: ""
    property bool sortReversed: false

    onSearchQueryChanged: updateFilteredResults()
    onSortReversedChanged: updateFilteredResults()

    signal thumbnailGenerated(string videoPath)

    function normalizePath(p) {
        let s = p.toString();
        if (s.startsWith("file://")) s = s.substring(7);
        return s;
    }

    function previewFor(videoPath) {
        const src = root.normalizePath(videoPath);
        for (let i = 0; i < allResultsModel.count; i++) {
            if (root.normalizePath(allResultsModel.get(i).folder) === src) {
                return String(allResultsModel.get(i).preview);
            }
        }
        return "";
    }

    // Best-available static representation of the active video wallpaper:
    // prefers the cached browser thumbnail, falls back to the extracted theme frame.
    function livePreviewSource() {
        if (!root.active) return "";
        const livePath = Config.ready ? Config.options.appearance.background.liveWallpaperPath : "";
        if (!livePath) return "";
        const cached = root.previewFor(livePath);
        if (cached !== "") return cached;
        return "file://" + root.framePath + "?v=" + root.frameVersion;
    }

    // Deterministic djb2 hash used by the scan script (kept in sync for lookups).
    function pathHash(src) {
        let hash = 5381;
        for (let i = 0; i < src.length; i++) {
            hash = (((hash << 5) + hash) + src.charCodeAt(i)) & 0x7FFFFFFF;
        }
        return hash.toString(16).padStart(8, "0");
    }

    function updateFilteredResults() {
        resultsModel.clear();
        const query = searchQuery.toLowerCase().trim();
        const tempResults = [];
        for (let i = 0; i < allResultsModel.count; i++) {
            const item = allResultsModel.get(i);
            if (query === "" || item.title.toLowerCase().includes(query) || item.folder.toLowerCase().includes(query)) {
                const copy = {};
                for (let key in item) {
                    if (key !== "index") copy[key] = item[key];
                }
                tempResults.push(copy);
            }
        }
        tempResults.sort((a, b) => {
            const cmp = a.title.localeCompare(b.title, undefined, {sensitivity: 'base'});
            return sortReversed ? -cmp : cmp;
        });
        for (let item of tempResults) resultsModel.append(item);
    }

    function getScanDirs() {
        const dirs = [root.videoDir];
        if (Config.ready && Config.options.appearance.background.customFolders) {
            for (let f of Config.options.appearance.background.customFolders) {
                const d = String(f);
                if (d && d !== "") dirs.push(d.startsWith("file://") ? d.substring(7) : d);
            }
        }
        if (Wallpapers.directory) {
            let d = Wallpapers.directory.toString();
            if (d.startsWith("file://")) d = d.substring(7);
            if (d && d !== "") dirs.push(d);
        }
        return [...new Set(dirs)];
    }

    function fetch() {
        if (loading) return;
        Quickshell.execDetached(["mkdir", "-p", root.cacheDir]);
        loading = true;
        errorMessage = "";
        allResults.clear();
        scanProcess.command = ["python3", "-c", scanProcess.script, JSON.stringify(root.getScanDirs()), root.cacheDir];
        scanProcess.running = true;
    }

    Process {
        id: scanProcess
        readonly property string script: `
import os, json, sys
dirs = json.loads(sys.argv[1])
cache = sys.argv[2]
exts = {'.mp4', '.webm', '.mkv', '.mov', '.m4v', '.gif'}
def djb2(s):
    h = 5381
    for c in s:
        h = ((h << 5) + h) + ord(c)
        h = h & 0x7FFFFFFF
    return format(h, '08x')
out = []
seen = set()
for d in dirs:
    if not os.path.isdir(d):
        continue
    try:
        entries = sorted(os.listdir(d))
    except Exception:
        continue
    for name in entries:
        p = os.path.join(d, name)
        if not os.path.isfile(p):
            continue
        ext = os.path.splitext(name)[1].lower()
        if ext not in exts:
            continue
        if p in seen:
            continue
        seen.add(p)
        try:
            st = os.stat(p)
        except Exception:
            continue
        mtime = int(st.st_mtime)
        thumb = os.path.join(cache, djb2(p) + '@' + str(mtime) + '.webp')
        out.append({
            "path": p,
            "folder": p,
            "id": djb2(p),
            "title": os.path.splitext(name)[0],
            "preview": "file://" + thumb
        })
print(json.dumps(out))
        `
        command: []
        stdout: StdioCollector {
            onStreamFinished: {
                root.loading = false;
                try {
                    const data = JSON.parse(this.text);
                    if (data.length === 0) root.errorMessage = "No videos found";
                    else {
                        for (let item of data) root.allResults.append(item);
                        root.updateFilteredResults();
                    }
                } catch (e) { root.errorMessage = "Error parsing video data"; }
            }
        }
    }

    // --- Thumbnail queue ---

    property var _thumbQueue: []
    property int _thumbRunning: 0
    property var _thumbDone: ({})

    function requestThumbnail(videoPath) {
        const src = root.normalizePath(videoPath);
        if (src === "" || _thumbDone[src] === true) return;
        for (let i = 0; i < allResultsModel.count; i++) {
            const item = allResultsModel.get(i);
            if (root.normalizePath(item.folder) === src) {
                if (_thumbQueue.some(q => q.src === src)) return;
                _thumbQueue.push({ "src": src, "out": String(item.preview).replace("file://", "") });
                root.pumpThumbQueue();
                return;
            }
        }
    }

    function pumpThumbQueue() {
        // A single Process is available, so dispatch at most one job at a time.
        // The next job is started from onExited.
        if (thumbProc.running || _thumbRunning > 0 || _thumbQueue.length === 0) return;
        const job = _thumbQueue.shift();
        _thumbRunning = 1;
        thumbProc._job = job;
        thumbProc.command = ["ffmpeg", "-y", "-ss", "3", "-i", job.src, "-frames:v", "1", "-an", "-vf", "scale=min(1024\\,iw):-2", "-loglevel", "error", job.out];
        thumbProc.running = true;
    }

    Process {
        id: thumbProc
        command: []
        property var _job: null
        onExited: (code) => {
            const job = thumbProc._job;
            thumbProc._job = null;
            root._thumbRunning = 0;
            if (job && code === 0) {
                root._thumbDone[job.src] = true;
                root.thumbnailGenerated(root.normalizePath(job.src));
            }
            root.pumpThumbQueue();
        }
    }

    // --- Core lifecycle ---

    property string _currentPath: ""

    function apply(videoPath) {
        if (!videoPath) return;
        const cleanPath = root.normalizePath(videoPath);
        root.isApplying = true;
        stopInternal();
        if (Config.ready) {
            Config.options.appearance.background.liveWallpaperPath = cleanPath;
            Config.options.appearance.background.liveWallpaperBackend = "mpvpaper";
        }
        applyInternal(cleanPath);
    }

    function applyInternal(path) {
        root._currentPath = path;
        root.resume();
        applyTimer.targetFolder = path;
        applyTimer.start();
    }

    Timer {
        id: applyTimer
        property string targetFolder
        interval: 500; repeat: false
        onTriggered: {
            const pan = root.scaling === "fill" ? "1.0" : "0.0";
            let opts = `--loop-file=inf --panscan=${pan} --volume=${String(root.volume)} --speed=${String(root.speed)}`;
            if (root.mute) opts += " --mute=yes";
            opts += ` --input-ipc-server=${root.ipcSocket}`;
            activeProcess.command = ["mpvpaper", "-o", opts, "ALL", root.normalizePath(targetFolder)];
            activeProcess.running = true;
            themeFrameProc.command = ["ffmpeg", "-y", "-ss", "3", "-i", root.normalizePath(targetFolder), "-frames:v", "1", "-an", "-vf", "scale='min(1920,iw)':-2", "-loglevel", "error", root.framePath];
            themeFrameProc.running = true;
            applyFinishFailsafe.restart();
        }
    }

    Process {
        id: themeFrameProc
        command: []
        stderr: StdioCollector {}
        onExited: (code) => {
            applyFinishFailsafe.stop();
            if (code === 0) {
                root.frameVersion = root.frameVersion + 1;
                Wallpapers.generateColors(root.framePath);
            } else {
                Wallpapers.sendNotification("Live Wallpaper Warning", "Failed to extract theme frame. Colors may be stale.");
            }
            root.isApplying = false;
            CavaService.restart();
            root.updatePauseState();
        }
    }

    Timer {
        id: applyFinishFailsafe
        interval: 30000
        repeat: false
        onTriggered: {
            if (root.isApplying) {
                root.isApplying = false;
                CavaService.restart();
                root.updatePauseState();
            }
        }
    }

    property bool _isIntentionalStop: false

    Process {
        id: activeProcess
        onExited: (exitCode) => {
            root.isPaused = false;
            if (root._isIntentionalStop) {
                root._isIntentionalStop = false;
                return;
            }
            if (root.isApplying) {
                if (exitCode !== 0) root.handleApplyError("Process exited with error code " + exitCode);
                else if (!activeProcess.running) root.handleApplyError("Process exited unexpectedly");
            }
        }
        stderr: StdioCollector { id: activeStderr }
    }

    // Watch for fatal errors reported by mpv/mpvpaper
    Timer {
        id: errorWatchTimer
        interval: 1500; repeat: true; property int attempts: 0
        onTriggered: {
            attempts++;
            const logs = activeStderr.text.toLowerCase();
            if (logs.includes("failed to open") || logs.includes("error opening") || logs.includes("no decoder") || logs.includes("cannot open")) {
                errorWatchTimer.stop();
                root.handleApplyError("mpv reported a fatal error during playback.");
                return;
            }
            if (attempts > 40) { errorWatchTimer.stop(); return; }
        }
    }

    function handleApplyError(reason) {
        if (!root.isApplying) return;
        console.error("[Mpvpaper] Apply Error:", reason);
        root.isApplying = false;
        errorWatchTimer.stop();
        applyTimer.stop();
        applyFinishFailsafe.stop();

        Wallpapers.sendNotification("Live Wallpaper Error", "Failed to apply video wallpaper. Falling back to a random static wallpaper.");

        root.stopInternal();

        if (Config.ready) {
            Config.options.appearance.background.liveWallpaperPath = "";
            Config.options.appearance.background.liveWallpaperBackend = "";
            const success = Wallpapers.selectRandomFavorite();
            if (!success) Wallpapers.initializeMatugen();
        }
    }

    function stop() {
        stopInternal();
        if (Config.ready) {
            Config.options.appearance.background.liveWallpaperPath = "";
            Config.options.appearance.background.liveWallpaperBackend = "";
        }
    }

    function stopInternal() {
        root.isApplying = false;
        errorWatchTimer.stop();
        applyTimer.stop();
        applyFinishFailsafe.stop();

        if (activeProcess.running) {
            root._isIntentionalStop = true;
            activeProcess.running = false;
        }

        // Pattern matches both the mpvpaper launcher and its mpv child (which shares the ipc-socket path in its cmdline)
        Quickshell.execDetached(["sh", "-c", "pkill -CONT -f 'mpvpaper-material3ui'; pkill -KILL -f 'mpvpaper-material3ui'"]);
        root.isPaused = false;
    }

    function pause() {
        if (root.isRunning && !root.isPaused && !root.isApplying) {
            Quickshell.execDetached(["bash", "-c", `echo "set pause yes" | socat - ${root.ipcSocket} 2>/dev/null || pkill -STOP -f 'mpvpaper-material3ui'`]);
            root.isPaused = true;
        }
    }

    function resume() {
        if (root.isRunning && root.isPaused) {
            Quickshell.execDetached(["bash", "-c", `echo "set pause no" | socat - ${root.ipcSocket} 2>/dev/null || pkill -CONT -f 'mpvpaper-material3ui'`]);
            root.isPaused = false;
        }
    }

    function updatePauseState() {
        if (GameMode.active || root.isApplying || !root.autoPause || !root.isRunning || !HyprlandData.activeWorkspace) return;
        const currentWsId = HyprlandData.activeWorkspace.id;
        const shellClasses = ["Quickshell", "material3ui-settings", "material3ui-monitor", "wayland-dashboard", "waybar", "ags", "fuzzel", "linux-wallpaperengine", "mpvpaper", "mpv"];
        const realWindows = HyprlandData.windowList.filter(win => {
            return win.workspace.id === currentWsId && !shellClasses.includes(win.class) && win.mapped && win.class !== "";
        });
        if (realWindows.length > 0) root.pause();
        else root.resume();
    }

    Timer { id: pauseDebounceTimer; interval: 500; repeat: false; onTriggered: updatePauseState() }

    Connections {
        target: HyprlandData; enabled: root.autoPause && root.isRunning
        function onWindowListChanged() { pauseDebounceTimer.restart(); }
        function onActiveWindowChanged() { pauseDebounceTimer.restart(); }
    }

    function checkInitialApply() {
        if (Config.ready) {
            const backend = Config.options.appearance.background.liveWallpaperBackend;
            const lastPath = Config.options.appearance.background.liveWallpaperPath;
            if (backend === "mpvpaper" && lastPath && lastPath !== "") {
                root.applyInternal(lastPath);
            }
        }
    }

    Connections {
        target: Config; function onReadyChanged() { if (Config.ready) root.checkInitialApply(); }
    }

    Connections {
        target: Session; ignoreUnknownSignals: true
        function onLockedChanged() {
            if (Session.locked) {
                if (Config.ready && Config.options.lock && Config.options.lock.useSeparateWallpaper) root.pause();
            } else pauseDebounceTimer.restart();
        }
    }

    Process {
        id: checkInstallation; command: ["which", "mpvpaper"]; running: true
        onExited: (code) => { root.isInstalled = (code === 0); }
    }

    Component.onCompleted: {
        Quickshell.execDetached(["mkdir", "-p", root.cacheDir]);
        if (Config.ready) root.checkInitialApply();
    }

    Component.onDestruction: {
        root.stopInternal();
    }
}