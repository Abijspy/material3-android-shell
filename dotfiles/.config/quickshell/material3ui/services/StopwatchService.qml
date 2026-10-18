pragma Singleton
import QtQuick
import Quickshell

Singleton {
    id: root
    
    property bool active: false
    property double startTimestamp: 0
    property double lastLapTimestamp: 0
    property double elapsedMs: 0
    property double currentLapMs: 0
    
    // Total accumulated time prior to current run
    property double accumulatedMs: 0 
    // Accumulated lap time prior to current run
    property double accumulatedLapMs: 0
    
    property var laps: [] // [{lapNum, lapMs, totalMs}]

    readonly property string timeString: formatTime(elapsedMs)
    property string lapTimeString: formatTime(currentLapMs)

    property int fastestLapIndex: -1
    property int slowestLapIndex: -1

    function formatTime(ms) {
        if (ms < 0) ms = 0;
        const totalCs = Math.floor(ms / 10); // centiseconds
        const cs = (totalCs % 100).toString().padStart(2, '0');
        const s = Math.floor(ms / 1000);
        const secs = (s % 60).toString().padStart(2, '0');
        const mins = Math.floor(s / 60);
        
        if (mins >= 60) {
            const h = Math.floor(mins / 60).toString().padStart(2, '0');
            const m = (mins % 60).toString().padStart(2, '0');
            return `${h}:${m}:${secs}.${cs}`;
        }
        return `${mins.toString().padStart(2, '0')}:${secs}.${cs}`;
    }

    function start() {
        const now = Date.now();
        startTimestamp = now;
        lastLapTimestamp = now;
        active = true;
    }

    function pause() {
        if (!active) return;
        active = false;
        accumulatedMs = elapsedMs;
        accumulatedLapMs = currentLapMs;
    }

    function reset() {
        active = false;
        accumulatedMs = 0;
        accumulatedLapMs = 0;
        elapsedMs = 0;
        currentLapMs = 0;
        startTimestamp = 0;
        lastLapTimestamp = 0;
        laps = [];
        fastestLapIndex = -1;
        slowestLapIndex = -1;
    }

    function lap() {
        if (!active) return;
        
        // Push current lap
        let currentLaps = root.laps.slice();
        currentLaps.push({
            lapNum: currentLaps.length + 1,
            lapMs: currentLapMs,
            totalMs: elapsedMs
        });
        root.laps = currentLaps;
        
        // Reset lap timer
        const now = Date.now();
        lastLapTimestamp = now;
        accumulatedLapMs = 0;
        currentLapMs = 0;
        
        // Update stats
        if (root.laps.length >= 2) {
            let min = Infinity, max = -1;
            let minIdx = -1, maxIdx = -1;
            for (let i = 0; i < root.laps.length; i++) {
                if (root.laps[i].lapMs < min) { min = root.laps[i].lapMs; minIdx = i; }
                if (root.laps[i].lapMs > max) { max = root.laps[i].lapMs; maxIdx = i; }
            }
            fastestLapIndex = minIdx;
            slowestLapIndex = maxIdx;
        } else {
            fastestLapIndex = -1;
            slowestLapIndex = -1;
        }
    }

    Timer {
        id: timer
        interval: 32 // ~30fps for smooth centiseconds update
        repeat: true
        running: root.active
        onTriggered: {
            const now = Date.now();
            root.elapsedMs = root.accumulatedMs + (now - root.startTimestamp);
            root.currentLapMs = root.accumulatedLapMs + (now - root.lastLapTimestamp);
        }
    }
}
