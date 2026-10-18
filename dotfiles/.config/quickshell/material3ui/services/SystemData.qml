pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

/**
 * Service providing system performance metrics using native Linux APIs (/proc, /sys, df).
 * Uses dgop server daemon exclusively for the Processes tab when active, ensuring 100% accurate
 * 1-second real-time delta CPU calculation without any 'ps 100%' glitches.
 * Zero idle daemon overhead when Processes tab is not open.
 */
Singleton {
    id: root

    property real cpuUsage: 0
    property real cpuTemperature: 0
    property string cpuModel: ""
    property int cpuThreads: 1
    property int physicalCores: 1
    
    FileView {
        id: fileCpuInfo
        path: "/proc/cpuinfo"
    }

    FileView {
        id: fileStat
        path: "/proc/stat"
    }

    FileView {
        id: fileMeminfo
        path: "/proc/meminfo"
    }

    FileView {
        id: fileNetDev
        path: "/proc/net/dev"
    }

    FileView {
        id: fileDiskStats
        path: "/proc/diskstats"
    }

    FileView {
        id: fileLoadAvg
        path: "/proc/loadavg"
    }

    FileView {
        id: fileUptime
        path: "/proc/uptime"
    }

    property real memUsage: 0
    property real swapUsage: 0
    property real totalMemoryMB: 0
    property real usedMemoryMB: 0
    
    property real networkRxRate: 0
    property real networkTxRate: 0
    readonly property real networkTotalRate: networkRxRate + networkTxRate
    
    property real diskReadRate: 0
    property real diskWriteRate: 0
    readonly property real diskTotalRate: diskReadRate + diskWriteRate
    
    // System stats
    property string loadAverage: ""
    property int processCount: 0
    property int threadCount: 0
    property string uptime: ""
    
    // List of objects: { mount: string, usage: real, total: real, used: real }
    property ListModel diskStats: ListModel {}
    property real primaryDiskUsage: 0

    // Emitted whenever diskStats rows are (re)populated so consumers that mirror
    // the list (e.g. the QuickSettings disk carousel) can rebuild immediately
    // instead of lagging behind a fixed polling timer.
    signal diskStatsUpdated()
    
    // Processes
    property var allProcesses: []
    
    // GPUs
    property var availableGpus: []
    readonly property bool hasValidGpuData: availableGpus.length > 0

    // History tracking
    readonly property int historySize: 60
    property var cpuHistory: new Array(historySize).fill(0)
    property var memHistory: new Array(historySize).fill(0)
    property var networkRxHistory: new Array(historySize).fill(0)
    property var networkTxHistory: new Array(historySize).fill(0)
    property var diskReadHistory: new Array(historySize).fill(0)
    property var diskWriteHistory: new Array(historySize).fill(0)
    property var gpuHistory: new Array(historySize).fill(0)

    function addToHistory(array, value) {
        let newArray = (array || []).slice();
        newArray.push(value);
        if (newArray.length > historySize) {
            newArray.shift();
        }
        return newArray;
    }

    // State for adaptive polling
    property int cycleCount: 0
    readonly property bool isMonitorActive: GlobalStates.systemMonitorOpen
    readonly property bool isQuickSettingsOpen: GlobalStates.quickSettingsOpen
    readonly property bool isFullscreen: HyprlandData.fullscreenActive
    
    readonly property bool isProcessesTabActive: isMonitorActive && GlobalStates.systemMonitorIndex === 2
    
    readonly property bool showSpeed: Config.options.bar ? Config.options.bar.show_network_speed : false
    readonly property bool showSystemMonitorOnStatusBar: Config.ready && Config.options.statusBar ? (Config.options.statusBar.systemMonitorPosition !== "hidden") : false
    readonly property bool isDesktopWidgetVisible: Config.ready && Config.options.appearance.systemMonitor ? (Config.options.appearance.systemMonitor.showOnDesktop && !GlobalStates.screenLocked) : false
    
    readonly property bool isAnyPanelOpen: isMonitorActive || isQuickSettingsOpen || isDesktopWidgetVisible || (!isFullscreen && (showSpeed || showSystemMonitorOnStatusBar))
    readonly property bool shouldPause: !isAnyPanelOpen

    readonly property int activeInterval: {
        if (isMonitorActive) return 1000;
        if (showSpeed && Config.ready && Config.options.bar) return Config.options.bar.networkSpeedInterval;
        if (Config.ready) return Config.options.appearance.systemMonitor.updateInterval;
        return 2000;
    }

    // Internal state tracking
    property var previousCpuTotal: 0
    property var previousCpuIdle: 0
    property var lastNetworkStats: null
    property var lastDiskStats: null
    property var lastUpdateTime: 0
    property string lastCursor: ""
    property bool processFetchPending: false
    property bool isDgopAvailable: false

    // Check if dgop binary is installed on the system
    Process {
        id: checkDgopProc
        command: ["which", "dgop"]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                root.isDgopAvailable = this.text.trim() !== "";
            }
        }
    }

    // Long-running dgop daemon process (Runs ONLY when Processes tab is active and dgop is installed)
    Process {
        id: dgopServerProc
        command: ["dgop", "server"]
        running: root.isProcessesTabActive && root.isDgopAvailable
    }

    // Native Linux ps fallback process (Runs when dgop is not installed)
    Process {
        id: psProc
        command: ["bash", "-c", "ps -eo pid,%cpu,rss,user,comm --sort=-%cpu | head -n 101"]
        stdout: StdioCollector {
            onStreamFinished: {
                const text = this.text.trim();
                if (!text) return;

                const lines = text.split("\n").slice(1);
                const procs = [];
                lines.forEach(line => {
                    const parts = line.trim().split(/\s+/);
                    if (parts.length >= 5) {
                        const pid = parseInt(parts[0]) || 0;
                        const rawCpu = parseFloat(parts[1]) || 0;
                        const rssKb = parseInt(parts[2]) || 0;
                        const user = parts[3];
                        const comm = parts.slice(4).join(" ");

                        procs.push({
                            pid: pid,
                            command: comm,
                            fullCommand: comm,
                            cpu: rawCpu / Math.max(1, root.cpuThreads),
                            memoryKB: rssKb,
                            username: user
                        });
                    }
                });
                root.allProcesses = procs;
                if (root.processCount === 0) root.processCount = procs.length;
            }
        }
    }

    // Native CPU Temperature process
    Process {
        id: tempProc
        command: ["bash", "-c", "sensors 2>/dev/null | grep -E 'Package id 0|Tctl|Tdie|temp1' | grep -oP '\\+\\K[0-9.]+(?=°C)' | head -1 || cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null | awk '{print $1/1000}' || echo 0"]
        stdout: StdioCollector {
            onStreamFinished: {
                const val = parseFloat(this.text.trim());
                if (!isNaN(val) && val > 0) root.cpuTemperature = val;
            }
        }
    }

    Process {
        id: coreDetectProc
        command: ["bash", "-c", "MODEL=$(grep -m1 '^model name' /proc/cpuinfo | awk -F: '{print $2}' | xargs); CORES=$(grep -m1 '^cpu cores' /proc/cpuinfo | awk -F: '{print $2}' | xargs); THREADS=$(nproc); echo \"$MODEL|$CORES|$THREADS\""]
        running: true
        stdout: StdioCollector {
            onStreamFinished: {
                const parts = this.text.trim().split("|");
                if (parts.length >= 3) {
                    if (parts[0]) root.cpuModel = parts[0];
                    const cores = parseInt(parts[1]);
                    if (!isNaN(cores) && cores > 0) root.physicalCores = cores;
                    const threads = parseInt(parts[2]);
                    if (!isNaN(threads) && threads > 0) root.cpuThreads = threads;
                }
            }
        }
    }

    // Native GPU Detection process (Supports AMD, Intel, and NVIDIA)
    Process {
        id: gpuProc
        command: ["bash", "-c", "NAME=$(lspci | grep -iE 'vga|3d|display' | head -n1 | awk -F: '{print $3}' | xargs); TEMP=$(cat /sys/class/drm/card*/device/hwmon/hwmon*/temp1_input 2>/dev/null | head -n1 | awk '{print int($1/1000)}'); USAGE=$(cat /sys/class/drm/card*/device/gpu_busy_percent 2>/dev/null | head -n1); echo \"$NAME|${TEMP:-0}|${USAGE:-0}\""]
        stdout: StdioCollector {
            onStreamFinished: {
                const parts = this.text.trim().split("|");
                if (parts.length >= 3 && parts[0]) {
                    const temp = parseFloat(parts[1]) || 0;
                    const usage = parseFloat(parts[2]) || 0;
                    root.availableGpus = [{
                        name: parts[0],
                        vendor: parts[0].includes("AMD") || parts[0].includes("ATI") ? "AMD" : (parts[0].includes("NVIDIA") ? "NVIDIA" : "Intel"),
                        temp: temp,
                        usage: usage
                    }];
                    root.gpuHistory = root.addToHistory(root.gpuHistory, usage);
                }
            }
        }
    }

    // Lightweight Process Count process
    Process {
        id: procCountProc
        command: ["bash", "-c", "ls -d /proc/[0-9]* 2>/dev/null | wc -l"]
        stdout: StdioCollector {
            onStreamFinished: {
                const count = parseInt(this.text.trim());
                if (!isNaN(count) && count > 0 && (!root.isProcessesTabActive || !root.isDgopAvailable)) {
                    root.processCount = count;
                }
            }
        }
    }

    // Custom Disk Monitoring process (Uses df -B1 -P for full custom disk support in bytes)
    Process {
        id: customDiskProc
        command: ["df", "-B1", "-P"]
        stdout: StdioCollector {
            onStreamFinished: {
                const text = this.text.trim();
                if (!text) return;

                const lines = text.split("\n").slice(1);
                const mounts = [];
                lines.forEach(line => {
                    const parts = line.split(/\s+/);
                    if (parts.length >= 6) {
                        const totalBytes = parseInt(parts[1]) || 0;
                        const usedBytes = parseInt(parts[2]) || 0;
                        const mount = parts[5];
                        const pctStr = parts[4].replace("%", "");
                        const pct = (parseInt(pctStr) || 0) / 100;
                        mounts.push({
                            mount: mount,
                            totalBytes: totalBytes,
                            usedBytes: usedBytes,
                            usage: pct
                        });
                    }
                });

                let monitored = [{ "path": "/", "alias": "System" }];
                if (Config.options.system && Config.options.system.monitoredDisks) {
                    monitored = Config.options.system.monitoredDisks;
                }

                if (root.diskStats.count === monitored.length) {
                    monitored.forEach((diskInfo, i) => {
                        const path = diskInfo.path || "/", alias = diskInfo.alias || "", hasAlias = alias !== "" && alias !== path, displayLabel = hasAlias ? alias : path;
                        const disk = mounts.find(m => m.mount === path);
                        if (disk) {
                            root.diskStats.set(i, { path: path, label: displayLabel.toUpperCase(), hasAlias: hasAlias, usage: disk.usage, total: disk.totalBytes, used: disk.usedBytes });
                        }
                    });
                } else {
                    root.diskStats.clear();
                    monitored.forEach(diskInfo => {
                        const path = diskInfo.path || "/", alias = diskInfo.alias || "", hasAlias = alias !== "" && alias !== path, displayLabel = hasAlias ? alias : path;
                        const disk = mounts.find(m => m.mount === path);
                        if (disk) {
                            root.diskStats.append({ path: path, label: displayLabel.toUpperCase(), hasAlias: hasAlias, usage: disk.usage, total: disk.totalBytes, used: disk.usedBytes });
                        }
                    });
                }
                root.primaryDiskUsage = root.diskStats.count > 0 ? root.diskStats.get(0).usage : 0;
                root.diskStatsUpdated();
            }
        }
    }

    // Fetch real-time processes (uses dgop server REST API when installed, or native ps fallback)
    function fetchProcesses() {
        if (!root.isProcessesTabActive) return;

        if (root.isDgopAvailable) {
            if (root.processFetchPending) return;
            root.processFetchPending = true;
            let url = "http://127.0.0.1:63484/gops/meta?modules=processes,system&limit=150";
            if (root.lastCursor !== "") {
                url += "&proc_cursor=" + encodeURIComponent(root.lastCursor) + "&cpu_cursor=" + encodeURIComponent(root.lastCursor);
            }

            const xhr = new XMLHttpRequest();
            xhr.open("GET", url, true);
            xhr.onreadystatechange = function() {
                if (xhr.readyState === XMLHttpRequest.DONE) {
                    root.processFetchPending = false;
                    if (xhr.status === 200) {
                        try {
                            const data = JSON.parse(xhr.responseText);
                            if (data.cursor) root.lastCursor = data.cursor;
                            
                            if (data.system) {
                                root.processCount = data.system.processes || 0;
                                root.threadCount = data.system.threads || 0;
                            }

                            if (data.processes && Array.isArray(data.processes)) {
                                const sorted = data.processes.slice().sort((a, b) => (b.cpu || 0) - (a.cpu || 0));
                                root.allProcesses = sorted.map(proc => ({
                                    pid: proc.pid || 0,
                                    command: proc.command || "",
                                    fullCommand: proc.fullCommand || "",
                                    cpu: (proc.cpu || 0) / Math.max(1, root.cpuThreads),
                                    memoryKB: proc.memoryKB || proc.pssKB || 0,
                                    username: proc.username || ""
                                }));
                            }
                        } catch (e) {}
                    } else {
                        // Server request failed, fallback to native ps
                        if (psProc.running) psProc.running = false;
                        psProc.running = true;
                    }
                }
            };
            xhr.send();
        } else {
            // Pure Native Linux ps Fallback
            if (psProc.running) psProc.running = false;
            psProc.running = true;
        }
    }

    // Main Update Function using Native Linux Files (/proc)
    function update() {
        if (shouldPause) return;
        cycleCount++;

        const now = Date.now();
        const timeDiff = root.lastUpdateTime > 0 ? Math.max(0.1, (now - root.lastUpdateTime) / 1000) : (root.activeInterval / 1000);
        root.lastUpdateTime = now;

        // 1. CPU Usage from /proc/stat
        fileStat.reload();
        const statText = fileStat.text();
        const cpuLine = statText.split("\n").find(l => l.startsWith("cpu "));
        if (cpuLine) {
            const fields = cpuLine.trim().split(/\s+/).slice(1).map(Number);
            const user = fields[0] || 0, nice = fields[1] || 0, sys = fields[2] || 0, idle = fields[3] || 0, iowait = fields[4] || 0, irq = fields[5] || 0, softirq = fields[6] || 0, steal = fields[7] || 0;
            const currentIdle = idle + iowait;
            const currentTotal = user + nice + sys + idle + iowait + irq + softirq + steal;
            
            if (root.previousCpuTotal > 0) {
                const totalDiff = currentTotal - root.previousCpuTotal;
                const idleDiff = currentIdle - root.previousCpuIdle;
                if (totalDiff > 0) {
                    root.cpuUsage = Math.max(0, Math.min(1, (totalDiff - idleDiff) / totalDiff));
                }
            }
            root.previousCpuTotal = currentTotal;
            root.previousCpuIdle = currentIdle;
            root.cpuHistory = root.addToHistory(root.cpuHistory, root.cpuUsage * 100);
        }

        // 2. RAM & Swap from /proc/meminfo
        fileMeminfo.reload();
        const memText = fileMeminfo.text();
        const totalMatch = memText.match(/^MemTotal:\s+(\d+)\s+kB/m);
        const availMatch = memText.match(/^MemAvailable:\s+(\d+)\s+kB/m);
        const swapTotalMatch = memText.match(/^SwapTotal:\s+(\d+)\s+kB/m);
        const swapFreeMatch = memText.match(/^SwapFree:\s+(\d+)\s+kB/m);

        if (totalMatch && availMatch) {
            const totalKb = parseInt(totalMatch[1]);
            const availKb = parseInt(availMatch[1]);
            const usedKb = totalKb - availKb;
            root.totalMemoryMB = Math.round(totalKb / 1024);
            root.usedMemoryMB = Math.round(usedKb / 1024);
            root.memUsage = totalKb > 0 ? usedKb / totalKb : 0;
            root.memHistory = root.addToHistory(root.memHistory, root.memUsage * 100);
        }

        if (swapTotalMatch && swapFreeMatch) {
            const swapTotalKb = parseInt(swapTotalMatch[1]);
            const swapFreeKb = parseInt(swapFreeMatch[1]);
            root.swapUsage = swapTotalKb > 0 ? (swapTotalKb - swapFreeKb) / swapTotalKb : 0;
        }

        // 3. Network Speed from /proc/net/dev
        fileNetDev.reload();
        const netText = fileNetDev.text();
        if (netText) {
            let totalRx = 0, totalTx = 0;
            const netLines = netText.split("\n").slice(2);
            netLines.forEach(line => {
                const parts = line.trim().split(/\s+/);
                if (parts.length >= 10 && !parts[0].startsWith("lo:")) {
                    totalRx += parseInt(parts[1]) || 0;
                    totalTx += parseInt(parts[9]) || 0;
                }
            });
            if (root.lastNetworkStats) {
                root.networkRxRate = Math.max(0, (totalRx - root.lastNetworkStats.rx) / timeDiff);
                root.networkTxRate = Math.max(0, (totalTx - root.lastNetworkStats.tx) / timeDiff);
            }
            root.lastNetworkStats = { rx: totalRx, tx: totalTx };
            root.networkRxHistory = root.addToHistory(root.networkRxHistory, root.networkRxRate / 1024);
            root.networkTxHistory = root.addToHistory(root.networkTxHistory, root.networkTxRate / 1024);
        }

        // 4. Disk I/O Rates from /proc/diskstats
        fileDiskStats.reload();
        const diskText = fileDiskStats.text();
        if (diskText) {
            let totalRead = 0, totalWrite = 0;
            const diskLines = diskText.split("\n");
            diskLines.forEach(line => {
                const parts = line.trim().split(/\s+/);
                if (parts.length >= 14 && (parts[2].startsWith("sd") || parts[2].startsWith("nvme") || parts[2].startsWith("vd"))) {
                    totalRead += (parseInt(parts[5]) || 0) * 512;
                    totalWrite += (parseInt(parts[9]) || 0) * 512;
                }
            });
            if (root.lastDiskStats) {
                root.diskReadRate = Math.max(0, (totalRead - root.lastDiskStats.read) / timeDiff);
                root.diskWriteRate = Math.max(0, (totalWrite - root.lastDiskStats.write) / timeDiff);
            }
            root.lastDiskStats = { read: totalRead, write: totalWrite };
            root.diskReadHistory = root.addToHistory(root.diskReadHistory, root.diskReadRate / (1024 * 1024));
            root.diskWriteHistory = root.addToHistory(root.diskWriteHistory, root.diskWriteRate / (1024 * 1024));
        }

        // 5. System LoadAvg & Uptime from /proc
        fileLoadAvg.reload();
        const loadText = fileLoadAvg.text().trim();
        if (loadText) {
            const parts = loadText.split(/\s+/);
            if (parts.length >= 3) root.loadAverage = `${parts[0]} ${parts[1]} ${parts[2]}`;
            if (parts.length >= 4 && parts[3].includes("/")) {
                const totalThreads = parseInt(parts[3].split("/")[1]);
                if (!isNaN(totalThreads) && totalThreads > 0) {
                    root.threadCount = totalThreads;
                }
            }
        }

        fileUptime.reload();
        const uptimeText = fileUptime.text().trim();
        if (uptimeText) {
            const seconds = Math.floor(parseFloat(uptimeText.split(/\s+/)[0]) || 0);
            const days = Math.floor(seconds / (24 * 3600)), remHours = Math.floor((seconds % (24 * 3600)) / 3600), remMins = Math.floor((seconds % 3600) / 60);
            if (days > 0) root.uptime = `${days}d ${remHours}h ${remMins}m`;
            else if (remHours > 0) root.uptime = `${remHours}h ${remMins}m`;
            else root.uptime = `${remMins}m`;
        }

        // 6. Trigger Subprocesses (Temp, GPU, ProcCount & Custom Disks)
        if (tempProc.running) tempProc.running = false;
        tempProc.running = true;

        if (isMonitorActive || isQuickSettingsOpen) {
            if (gpuProc.running) gpuProc.running = false;
            gpuProc.running = true;

            if (procCountProc.running) procCountProc.running = false;
            procCountProc.running = true;
        }

        if (isMonitorActive || isQuickSettingsOpen || isDesktopWidgetVisible) {
            if (customDiskProc.running) customDiskProc.running = false;
            customDiskProc.running = true;
        }

        // 7. Trigger Processes fetch via dgop REST server (Only when Processes tab is active)
        if (root.isProcessesTabActive) {
            root.fetchProcesses();
        } else {
            root.allProcesses = [];
        }
    }

    Timer {
        id: updateTimer
        interval: root.activeInterval
        running: true
        repeat: true
        triggeredOnStart: false
        onTriggered: root.update()
    }
    
    Connections {
        target: GlobalStates
        function onSystemMonitorOpenChanged() {
            if (GlobalStates.systemMonitorOpen) {
                root.update();
            }
        }
        function onQuickSettingsOpenChanged() {
            // Refresh immediately on open so the panel shows fresh data
            // instead of waiting up to a full poll interval.
            if (GlobalStates.quickSettingsOpen) {
                root.update();
            }
        }
    }

    function initCpuInfo() {
        fileCpuInfo.reload();
        const text = fileCpuInfo.text();
        const modelMatch = text.match(/^model name\s+:\s+(.+)$/m);
        if (modelMatch) root.cpuModel = modelMatch[1].trim();

        const countMatch = text.match(/^processor\s+:\s+\d+/gm);
        if (countMatch) root.cpuThreads = countMatch.length;

        const coreMatch = text.match(/^cpu cores\s+:\s+(\d+)/m);
        if (coreMatch) root.physicalCores = parseInt(coreMatch[1]);
    }

    function prePopulateDisks() {
        let monitored = [{ "path": "/", "alias": "System" }];
        if (Config.options.system && Config.options.system.monitoredDisks) {
            monitored = Config.options.system.monitoredDisks;
        }
        root.diskStats.clear();
        monitored.forEach(d => {
            const path = d.path || "/", alias = d.alias || "", hasAlias = !!alias;
            root.diskStats.append({ path: path, label: (alias || path).toUpperCase(), hasAlias: hasAlias, usage: 0, total: 0, used: 0 });
        });
        root.diskStatsUpdated();
    }

    // The config file may load after this singleton is created; re-seed the disk
    // list once it is ready so the real monitored disks are known up front
    // instead of flipping from the default single disk on the first panel open.
    Connections {
        target: Config
        function onReadyChanged() {
            if (Config.ready) root.prePopulateDisks();
        }
    }

    Component.onCompleted: {
        root.initCpuInfo();
        root.prePopulateDisks();
        Qt.callLater(() => root.update());
    }

    Component.onDestruction: {
        dgopServerProc.running = false;
        psProc.running = false;
        checkDgopProc.running = false;
        tempProc.running = false;
        gpuProc.running = false;
        customDiskProc.running = false;
    }
}
