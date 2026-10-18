pragma Singleton
pragma ComponentBehavior: Bound
import "../core"
import QtQuick
import Quickshell
import Quickshell.Services.Pipewire

/**
 * A nice wrapper for default Pipewire audio sink and source.
 */
Singleton {
    id: root

    // Misc props
    property bool ready: Pipewire.defaultAudioSink?.ready ?? false
    property PwNode sink: Pipewire.defaultAudioSink
    property PwNode source: Pipewire.defaultAudioSource
    
    // Convenience properties for UI
    property real volume: sink?.audio.volume ?? 0
    property real microphoneVolume: source?.audio.volume ?? 0
    property bool muted: sink?.audio.muted ?? false
    property bool microphoneMuted: source?.audio.muted ?? false

    // Setters that update hardware (safely)
    function setVolume(v) { if (sink && sink.audio) sink.audio.volume = v }
    function setMicrophoneVolume(v) { if (source && source.audio) source.audio.volume = v }
    function setMuted(m) { if (sink && sink.audio) sink.audio.muted = m }
    function setMicrophoneMuted(m) { if (source && source.audio) source.audio.muted = m }
    function setNodeVolume(node, v) { if (node && node.audio) node.audio.volume = v }

    readonly property real hardMaxValue: 2.00 
    property string audioTheme: (Config.options.sounds && Config.options.sounds.theme) ? Config.options.sounds.theme : "freedesktop"
    
    // For backward compatibility or internal use
    property real value: volume

    function friendlyDeviceName(node) {
        return (node.nickname || node.description || qsTr("Unknown"));
    }
    function appNodeDisplayName(node) {
        const name = (node.properties["application.name"] || node.description || node.name);
        if (name && name.length > 0) {
            return name.charAt(0).toUpperCase() + name.slice(1);
        }
        return name;
    }

    function appNodeIconName(node) {
        if (!node) return "settings_input_component";

        // Get all possible identifiers
        const iconMetadata = node.properties["application.icon-name"] 
                          || node.properties["app.icon"] 
                          || node.properties["window.icon"]
                          || node.properties["icon-name"];
        const appName = node.properties["application.name"];
        const nodeName = node.name;
        const appProcessName = node.properties["application.process.binary"];

        // Always pass through AppSearch.guessIcon to apply substitutions (like brave-browser -> brave-desktop)
        // We pass the metadata icon as the primary search key
        return AppSearch.guessIcon(iconMetadata || appName || nodeName || appProcessName, appProcessName, appName);
    }

    // Lists for UI
    function getNodesByType(isSink) {
        return Pipewire.nodes.values.filter(node => {
            const isDummy = (node.name || "").toLowerCase().includes("dummy") 
                         || (node.description || "").toLowerCase().includes("dummy")
                         || (node.nickname || "").toLowerCase().includes("dummy");
            return (node.isSink === isSink) && node.audio && !node.isStream && !isDummy
        })
    }

    function getStreamNodesByType(isSink) {
        return Pipewire.nodes.values.filter(node => {
            const isDummy = (node.name || "").toLowerCase().includes("dummy") 
                         || (node.description || "").toLowerCase().includes("dummy")
                         || (node.nickname || "").toLowerCase().includes("dummy");
            return (node.isSink === isSink) && node.audio && node.isStream && !isDummy
        })
    }

    readonly property list<var> outputDevices: getNodesByType(true)
    readonly property list<var> inputDevices: getNodesByType(false)
    readonly property list<var> streamNodes: getStreamNodesByType(true)
    readonly property list<var> micStreamNodes: getStreamNodesByType(false)
    readonly property list<var> sinks: outputDevices // alias
    readonly property list<var> sources: inputDevices // alias

    // Selection
    function setDefaultSink(node) {
        Pipewire.preferredDefaultAudioSink = node;
    }
    function setDefaultSource(node) {
        Pipewire.preferredDefaultAudioSource = node;
    }

    // Signals
    signal sinkProtectionTriggered(string reason);

    // Controls
    function toggleMute() { setMuted(!muted) }
    function toggleMicMute() { setMicrophoneMuted(!microphoneMuted) }

    function incrementVolume() {
        setVolume(Math.min(1.0, volume + (volume < 0.1 ? 0.01 : 0.02)));
    }
    
    function decrementVolume() {
        setVolume(Math.max(0, volume - (volume < 0.1 ? 0.01 : 0.02)));
    }

    // Internals
    PwObjectTracker {
        objects: [sink, source]
    }

    function playSystemSound(soundName) {
        // Resolve .oga/.ogg at runtime — themes ship different variants
        // (freedesktop has both: firing two ffplay would double the sound).
        // ~/.local/share/sounds wins over /usr/share/sounds; falls back to
        // the freedesktop theme when a custom theme lacks the sound.
        const local = `${Directories.home.replace("file://", "")}/.local/share/sounds/${root.audioTheme}/stereo/${soundName}`;
        const system = `/usr/share/sounds/${root.audioTheme}/stereo/${soundName}`;
        const freedesktop = `/usr/share/sounds/freedesktop/stereo/${soundName}`;
        const script = `f='${local}.oga'; [ -f "$f" ] || f='${local}.ogg'; [ -f "$f" ] || f='${system}.oga'; [ -f "$f" ] || f='${system}.ogg'; [ -f "$f" ] || f='${freedesktop}.oga'; [ -f "$f" ] || f='${freedesktop}.ogg'; exec ffplay -nodisp -autoexit "$f"`;
        Quickshell.execDetached(["bash", "-c", script]);
    }

    // Plays the configured notification sound; falls back to the theme default.
    function playNotificationSound() {
        const custom = (Config.ready && Config.options.sounds) ? Config.options.sounds.notification : "";
        if (custom !== "") {
            Quickshell.execDetached(["ffplay", "-nodisp", "-autoexit", custom]);
            return;
        }
        playSystemSound("message-new-instant");
    }
}
