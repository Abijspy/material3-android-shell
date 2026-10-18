pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import "../core"

/**
 * Hyprland keybind editor backend.
 *
 * Default binds are registered through hypr/material3ui/bindings.lua with a
 * "Group: Action" description and keyed by their normalised default combo,
 * lowercased ("super + ctrl + t"). Edits are kept in ~/.config/material3ui/keybinds.json
 * and rendered to hypr/material3ui/keybinds_custom.lua, which bindings.lua
 * reads before binding; then Hyprland reloads its config.
 *
 * binds:  [{ id, combo, label, group, changed, conflict }]  (live, editable)
 * others: [{ combo, label }]  binds Material3UI doesn't own (user.lua, ...)
 * store:  { overrides: { id: combo | false }, labels: { id: label },
 *           custom: [{ keys, command, label }] }
 */
Singleton {
    id: root

    readonly property bool supported: HyprlandCompat.isLua
    property bool loading: false
    property var binds: []
    property var others: []
    property var store: ({ overrides: {}, labels: {}, custom: [] })
    // Disabled defaults no longer appear in `hyprctl binds`, so list them from the store
    readonly property var disabled: Object.keys(store.overrides)
        .filter(id => store.overrides[id] === false)
        .map(id => ({ id: id, label: store.labels[id] || id, group: root.groupOf(store.labels[id] || "") }))

    readonly property string storePath: Directories.shellConfig + "/keybinds.json"
    readonly property string luaPath: Directories.home.replace("file://", "") + "/.config/hypr/material3ui/keybinds_custom.lua"

    readonly property var modOrder: ["SUPER", "CTRL", "ALT", "SHIFT"]

    // ── Combos ──
    function normalize(keys) {
        const mods = {};
        let key = "";
        for (const raw of (keys || "").split("+")) {
            const p = raw.trim();
            if (!p) continue;
            const up = ({ CONTROL: "CTRL", WIN: "SUPER", META: "SUPER", MOD4: "SUPER", MOD1: "ALT" })[p.toUpperCase()] || p.toUpperCase();
            if (root.modOrder.includes(up)) mods[up] = true; else key = p;
        }
        return root.modOrder.filter(m => mods[m]).concat([key]).join(" + ");
    }

    function idOf(keys) {
        return root.normalize(keys).toLowerCase();
    }

    function comboFromHyprctl(b) {
        const bits = { SUPER: 64, CTRL: 4, ALT: 8, SHIFT: 1 };
        return root.modOrder.filter(m => b.modmask & bits[m]).concat([b.key]).join(" + ");
    }

    // ── Display ──
    readonly property var _pretty: ({
        SUPER: "Super", CTRL: "Ctrl", ALT: "Alt", SHIFT: "Shift",
        super_l: "Super (tap)", return: "Enter", escape: "Esc", period: ".", comma: ",",
        minus: "-", equal: "=", slash: "/", backslash: "\\", semicolon: ";", apostrophe: "'",
        grave: "`", bracketleft: "[", bracketright: "]", backspace: "Backspace", delete: "Del",
        prior: "PgUp", page_up: "PgUp", next: "PgDn", page_down: "PgDn",
        left: "←", right: "→", up: "↑", down: "↓", space: "Space", tab: "Tab", print: "PrtSc",
        "mouse:272": "Left click", "mouse:273": "Right click", "mouse:274": "Middle click",
        mouse_down: "Scroll down", mouse_up: "Scroll up",
        xf86audioraisevolume: "Vol +", xf86audiolowervolume: "Vol −", xf86audiomute: "Mute",
        xf86audiomicmute: "Mic mute", xf86audioplay: "Play", xf86audiopause: "Pause",
        xf86audionext: "Next", xf86audioprev: "Prev",
        xf86monbrightnessup: "Bright +", xf86monbrightnessdown: "Bright −"
    })

    function prettyKey(k) {
        const s = k || "";
        return root._pretty[s] || root._pretty[s.toLowerCase()] || (s.length === 1 ? s.toUpperCase() : s);
    }

    function prettyCombo(keys) {
        return root.normalize(keys).split(" + ").map(root.prettyKey).join(" + ");
    }

    // Qt key → Hyprland (xkb) key name; "" for bare modifiers
    function keyName(k) {
        if (k >= Qt.Key_A && k <= Qt.Key_Z) return String.fromCharCode(k);
        if (k >= Qt.Key_0 && k <= Qt.Key_9) return String.fromCharCode(k);
        if (k >= Qt.Key_F1 && k <= Qt.Key_F24) return "F" + (k - Qt.Key_F1 + 1);
        const map = {};
        map[Qt.Key_Return] = "Return"; map[Qt.Key_Enter] = "KP_Enter"; map[Qt.Key_Space] = "Space";
        map[Qt.Key_Tab] = "Tab"; map[Qt.Key_Backtab] = "Tab"; map[Qt.Key_Escape] = "Escape";
        map[Qt.Key_Backspace] = "BackSpace"; map[Qt.Key_Delete] = "Delete"; map[Qt.Key_Insert] = "Insert";
        map[Qt.Key_Home] = "Home"; map[Qt.Key_End] = "End"; map[Qt.Key_PageUp] = "Prior"; map[Qt.Key_PageDown] = "Next";
        map[Qt.Key_Left] = "Left"; map[Qt.Key_Right] = "Right"; map[Qt.Key_Up] = "Up"; map[Qt.Key_Down] = "Down";
        map[Qt.Key_Print] = "Print"; map[Qt.Key_Super_L] = "Super_L";
        map[Qt.Key_Period] = "period"; map[Qt.Key_Greater] = "period"; map[Qt.Key_Comma] = "comma"; map[Qt.Key_Less] = "comma";
        map[Qt.Key_Minus] = "minus"; map[Qt.Key_Underscore] = "minus"; map[Qt.Key_Equal] = "equal"; map[Qt.Key_Plus] = "equal";
        map[Qt.Key_Slash] = "slash"; map[Qt.Key_Question] = "slash"; map[Qt.Key_Backslash] = "backslash"; map[Qt.Key_Bar] = "backslash";
        map[Qt.Key_Semicolon] = "semicolon"; map[Qt.Key_Colon] = "semicolon"; map[Qt.Key_Apostrophe] = "apostrophe"; map[Qt.Key_QuoteDbl] = "apostrophe";
        map[Qt.Key_QuoteLeft] = "grave"; map[Qt.Key_AsciiTilde] = "grave";
        map[Qt.Key_BracketLeft] = "bracketleft"; map[Qt.Key_BraceLeft] = "bracketleft";
        map[Qt.Key_BracketRight] = "bracketright"; map[Qt.Key_BraceRight] = "bracketright";
        // Shift + digit arrives as the symbol (US layout)
        map[Qt.Key_Exclam] = "1"; map[Qt.Key_At] = "2"; map[Qt.Key_NumberSign] = "3"; map[Qt.Key_Dollar] = "4";
        map[Qt.Key_Percent] = "5"; map[Qt.Key_AsciiCircum] = "6"; map[Qt.Key_Ampersand] = "7"; map[Qt.Key_Asterisk] = "8";
        map[Qt.Key_ParenLeft] = "9"; map[Qt.Key_ParenRight] = "0";
        map[Qt.Key_VolumeUp] = "XF86AudioRaiseVolume"; map[Qt.Key_VolumeDown] = "XF86AudioLowerVolume";
        map[Qt.Key_VolumeMute] = "XF86AudioMute"; map[Qt.Key_MicMute] = "XF86AudioMicMute";
        map[Qt.Key_MediaPlay] = "XF86AudioPlay"; map[Qt.Key_MediaTogglePlayPause] = "XF86AudioPlay";
        map[Qt.Key_MediaPause] = "XF86AudioPause"; map[Qt.Key_MediaNext] = "XF86AudioNext"; map[Qt.Key_MediaPrevious] = "XF86AudioPrev";
        map[Qt.Key_MonBrightnessUp] = "XF86MonBrightnessUp"; map[Qt.Key_MonBrightnessDown] = "XF86MonBrightnessDown";
        return map[k] || "";
    }

    function groupOf(label) {
        const i = label.indexOf(": ");
        return i > 0 ? label.slice(0, i) : "Other";
    }

    function actionOf(label) {
        const i = label.indexOf(": ");
        return i > 0 ? label.slice(i + 2) : label;
    }

    // Binds (any source) already using this combo, other than `exceptId`
    function conflictsFor(combo, exceptId) {
        const c = root.idOf(combo);
        const hits = root.binds.filter(b => root.idOf(b.combo) === c && b.id !== exceptId).map(b => root.actionOf(b.label));
        for (const o of root.others) if (root.idOf(o.combo) === c) hits.push(o.label || o.combo);
        for (const k of root.store.custom) if (root.idOf(k.keys) === c) hits.push(k.label || k.command);
        return hits;
    }

    // ── Edits ──
    function remap(id, combo, label) {
        const s = root._copy();
        const c = root.normalize(combo);
        if (root.idOf(c) === id) delete s.overrides[id]; else s.overrides[id] = c;
        s.labels[id] = label;
        root._commit(s);
    }

    function disable(id, label) {
        const s = root._copy();
        s.overrides[id] = false;
        s.labels[id] = label;
        root._commit(s);
    }

    function reset(id) {
        const s = root._copy();
        delete s.overrides[id];
        delete s.labels[id];
        root._commit(s);
    }

    function resetAll() {
        root._commit({ overrides: {}, labels: {}, custom: root.store.custom });
    }

    function addCustom(keys, command, label) {
        const s = root._copy();
        s.custom.push({ keys: root.normalize(keys), command: command, label: label || command });
        root._commit(s);
    }

    function removeCustom(index) {
        const s = root._copy();
        s.custom.splice(index, 1);
        root._commit(s);
    }

    function refresh() {
        if (!root.supported) return;
        root.loading = true;
        bindsProc.running = false;
        bindsProc.running = true;
    }

    function _copy() {
        return JSON.parse(JSON.stringify(root.store));
    }

    function _commit(s) {
        root.store = s;
        storeFile.setText(JSON.stringify(s, null, 2));
        luaFile.setText(root._renderLua(s));
    }

    function _luaString(v) {
        return '"' + String(v).replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, "\\n") + '"';
    }

    function _renderLua(s) {
        const lines = [
            "-- Written by Material3UI (Settings → System → Keybinds).",
            "-- Edit binds there; changes made here are overwritten.",
            "return {",
            "\toverrides = {"
        ];
        for (const id of Object.keys(s.overrides).sort())
            lines.push("\t\t[" + root._luaString(id) + "] = " + (s.overrides[id] === false ? "false" : root._luaString(s.overrides[id])) + ",");
        lines.push("\t},", "\tcustom = {");
        for (const c of s.custom)
            lines.push("\t\t{ keys = " + root._luaString(c.keys) + ", command = " + root._luaString(c.command) + ", label = " + root._luaString(c.label) + " },");
        lines.push("\t},", "}", "");
        return lines.join("\n");
    }

    // ── Reading Hyprland ──
    Process {
        id: bindsProc
        command: ["hyprctl", "binds", "-j"]
        stdout: StdioCollector {
            onStreamFinished: {
                let list = [];
                try { list = JSON.parse(this.text); } catch (e) { list = []; }
                const reverse = {};
                for (const id of Object.keys(root.store.overrides))
                    if (root.store.overrides[id]) reverse[root.idOf(root.store.overrides[id])] = id;
                const customIds = root.store.custom.map(c => root.idOf(c.keys));

                const owned = [];
                const other = [];
                for (const b of list) {
                    if (b.submap) continue;
                    const combo = root.comboFromHyprctl(b);
                    const label = b.has_description ? b.description : "";
                    if (label.startsWith("Custom: ") && customIds.includes(root.idOf(combo))) continue;
                    if (label && label.indexOf(": ") > 0) {
                        const id = reverse[root.idOf(combo)] || root.idOf(combo);
                        owned.push({ id: id, combo: combo, label: label, group: root.groupOf(label), changed: !!reverse[root.idOf(combo)] });
                    } else {
                        other.push({ combo: combo, label: label || ((b.dispatcher || "") + " " + (b.arg || "")).trim() });
                    }
                }
                const counts = {};
                for (const b of owned.concat(other)) counts[root.idOf(b.combo)] = (counts[root.idOf(b.combo)] || 0) + 1;
                for (const c of customIds) counts[c] = (counts[c] || 0) + 1;
                root.binds = owned.map(b => Object.assign(b, { conflict: counts[root.idOf(b.combo)] > 1 }));
                root.others = other;
                root.loading = false;
            }
        }
        onExited: root.loading = false
    }

    // configreloaded fires after our write → reload; re-read binds then
    Connections {
        target: Hyprland
        function onRawEvent(event) {
            if (event.name === "configreloaded") reloadDebounce.restart();
        }
    }
    Timer {
        id: reloadDebounce
        interval: 300
        onTriggered: root.refresh()
    }

    // ── Storage ──
    FileView {
        id: storeFile
        path: root.storePath
        watchChanges: false
        printErrors: false
        blockWrites: true
        onLoaded: {
            try {
                const s = JSON.parse(storeFile.text());
                root.store = { overrides: s.overrides || {}, labels: s.labels || {}, custom: s.custom || [] };
            } catch (e) {
                console.warn("Keybinds: failed to parse keybinds.json:", e);
            }
            root.refresh();
        }
        onLoadFailed: root.refresh()
    }

    FileView {
        id: luaFile
        path: root.luaPath
        watchChanges: false
        printErrors: false
        blockWrites: true
        onSaved: Quickshell.execDetached(["hyprctl", "reload"])
    }
}
