pragma Singleton
import QtQuick
import Quickshell

Singleton {
    id: root

    property double startTimestamp: Date.now()
    
    property int focusTime: 1500 // 25 min default
    property int breakTime: 300 // 5 min default
    property bool autoContinue: true
    property int rotations: 0

    property int duration: mode === 0 ? focusTime : breakTime
    property int remainingTime: duration
    
    property bool active: false
    property int mode: 0 // 0: Focus, 1: Break
    
    readonly property bool isSessionRunning: active || (remainingTime < duration && remainingTime > 0)
    
    readonly property string timeString: {
        const h = Math.floor(remainingTime / 3600);
        const m = Math.floor((remainingTime % 3600) / 60);
        const secs = remainingTime % 60;
        
        if (h > 0) {
            return `${h}:${m.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
        } else if (m > 0) {
            return `${m}:${secs.toString().padStart(2, '0')}`;
        } else {
            return `${secs}`;
        }
    }

    readonly property string modeName: {
        switch (mode) {
            case 0: return "Focus";
            case 1: return "Break";
            default: return "";
        }
    }

    property double elapsedMs: 0
    readonly property real progress: (duration > 0) ? Math.max(0.0, Math.min(1.0, remainingTime / duration)) : 0

    function start() { 
        if (remainingTime === duration) {
            // New session, start fresh
            startTimestamp = Date.now();
        } else {
            // Resume from pause
            startTimestamp = Date.now() - (duration - remainingTime) * 1000;
        }
        active = true; 
    }
    function pause() { active = false; }
    function stop() {
        active = false;
        reset();
    }

    function reset() {
        active = false;
        duration = mode === 0 ? focusTime : breakTime;
        remainingTime = duration;
        startTimestamp = Date.now();
        elapsedMs = 0;
    }

    function setMode(newMode) {
        mode = newMode;
        active = false;
        reset();
    }

    // Comprehensive session completion logic
    function completeSession() {
        // Prevent race condition: stop timer immediately
        active = false;

        // 1. Feedback (Sound)
        Audio.playSystemSound("message");

        // 2. State update & Rotation logic
        const wasFocus = (mode === 0);
        if (wasFocus) {
            mode = 1; // Go to break
        } else {
            rotations++;
            mode = 0; // Back to focus
        }
        
        // 3. Reset (updates duration, remainingTime, startTimestamp)
        reset(); 
        
        // 4. Auto-continue handling
        if (autoContinue) {
            active = true;
        }
    }

    Timer {
        id: timer
        interval: 1000 // 1 tick per second optimization
        repeat: true
        running: root.active
        onTriggered: {
            const now = Date.now();
            root.elapsedMs = now - root.startTimestamp;
            const elapsed = Math.floor(root.elapsedMs / 1000);
            
            if (elapsed > root.duration) {
                root.completeSession();
            } else {
                const newRemaining = Math.max(0, root.duration - elapsed);
                if (newRemaining !== root.remainingTime) {
                    root.remainingTime = newRemaining;
                }
            }
        }
    }
}
