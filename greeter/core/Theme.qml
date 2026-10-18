pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io

/**
 * Greeter theme: Material 3 roles synced from the user's Matugen palette
 * (colors.json), plus wallpaper, fonts and clock format from greeter.json.
 * Both files are written by `material3ui-greeter sync`; dark Material 3
 * baseline colours are used until they exist.
 */
Singleton {
    id: root

    readonly property string dataDir: Quickshell.env("M3UI_GREETER_DATA") || "/var/lib/material3ui-greeter"

    // Scale with the primary screen, matching the shell's effectiveScale feel
    readonly property real s: {
        const scr = Quickshell.screens[0];
        return scr ? Math.max(0.8, Math.min(1.6, scr.height / 1080)) : 1;
    }

    // ── Material 3 roles (baseline dark) ──
    property color primary: "#d0bcff"
    property color onPrimary: "#381e72"
    property color primaryContainer: "#4f378b"
    property color onPrimaryContainer: "#eaddff"
    property color secondary: "#ccc2dc"
    property color onSecondary: "#332d41"
    property color secondaryContainer: "#4a4458"
    property color onSecondaryContainer: "#e8def8"
    property color tertiary: "#efb8c8"
    property color error: "#f2b8b5"
    property color onError: "#601410"
    property color errorContainer: "#8c1d18"
    property color surface: "#141218"
    property color onSurface: "#e6e0e9"
    property color onSurfaceVariant: "#cac4d0"
    property color surfaceContainerLowest: "#0f0d13"
    property color surfaceContainerLow: "#1d1b20"
    property color surfaceContainer: "#211f26"
    property color surfaceContainerHigh: "#2b2930"
    property color surfaceContainerHighest: "#36343b"
    property color outline: "#938f99"
    property color outlineVariant: "#49454f"
    property color scrim: "#000000"

    // ── Greeter settings ──
    property string wallpaper: ""
    property string timeFormat: "HH:mm"
    property string dateFormat: "dddd, d MMMM"
    property string fontMain: "Google Sans Flex"
    readonly property string fontIcon: "Material Symbols Rounded"
    property var avatars: ({})

    // Lock screen clock, synced from the shell's digital(Locked) clock config
    property var clock: ({
        colorStyle: "primary", fontSize: 84, dateFontSize: 24, dateGap: 4,
        isVertical: false, hideAmPm: false, showDate: true,
        timeFont: "Google Sans Flex", dateFont: "Google Sans Flex",
    })
    readonly property color clockColor: {
        const s = clock.colorStyle;
        if (s === "secondary") return secondary;
        if (s === "onSurface") return onSurface;
        if (s === "surface") return surfaceContainerHighest;
        return primary;
    }
    // Appearance.colors.colLockscreenDate
    readonly property color dateColor: withAlpha(onSurfaceVariant, 0.8)

    readonly property var motion: ({ fast: 200, normal: 350, slow: 500 })

    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    function applyColors(text) {
        let json;
        try { json = JSON.parse(text); } catch (e) { return; }
        for (const key in json) {
            const prop = key.replace(/_([a-z])/g, g => g[1].toUpperCase());
            if (root.hasOwnProperty(prop) && typeof json[key] === "string" && json[key].startsWith("#"))
                root[prop] = json[key];
        }
    }

    function applySettings(text) {
        let json;
        try { json = JSON.parse(text); } catch (e) { return; }
        if (json.wallpaper) root.wallpaper = json.wallpaper;
        if (json.timeFormat) root.timeFormat = json.timeFormat;
        if (json.dateFormat) root.dateFormat = json.dateFormat;
        if (json.font) root.fontMain = json.font;
        if (json.clock) root.clock = Object.assign({}, root.clock, json.clock);
        if (json.avatars) root.avatars = json.avatars;
    }

    FileView {
        path: root.dataDir + "/colors.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.applyColors(text())
    }

    FileView {
        path: root.dataDir + "/greeter.json"
        watchChanges: true
        onFileChanged: reload()
        onLoaded: root.applySettings(text())
    }
}
