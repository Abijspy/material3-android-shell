pragma Singleton

import QtQuick
import Quickshell
import "../core"

/**
 * Snackbar Service — global Material 3 snackbar.
 * Transient bottom-center feedback with optional action button.
 * DND-proof by design: rendered by the shell itself, never goes through
 * the notification pipeline (SmartAutomation/Notifications can't suppress it).
 *
 * Usage: SnackbarService.show("Text", "Action", () => { ... })
 *        SnackbarService.show("Text", "Undo", cb, SnackbarService.undoDuration)
 */
Singleton {
    id: root

    readonly property int duration: 4000
    readonly property int undoDuration: 8000 // destructive/undoable actions need more reaction time
    property bool visible: false
    property string text: ""
    property string actionLabel: ""
    // Inert snackbars are fully click-through (no mask, no buttons, no focus
    // grab registration) — safe over games where every px of input matters
    property bool interactive: true
    property var _actionCallback: null

    // Incremented per show()/dismiss(); stale timer ticks are ignored
    property int _generation: 0

    function show(message, actionLabel, actionCallback, durationMs) {
        root._generation++;
        root.text = message || "";
        root.actionLabel = actionLabel || "";
        root.interactive = true;
        root._actionCallback = (root.actionLabel !== "" && typeof actionCallback === "function") ? actionCallback : null;
        dismissTimer.interval = (typeof durationMs === "number" && durationMs > 0) ? durationMs : root.duration;
        root.visible = true;
        dismissTimer.restart();
    }

    function showInert(message) {
        root.show(message);
        root.interactive = false;
    }

    function dismiss() {
        root._generation++;
        root.visible = false;
        dismissTimer.stop();
    }

    function triggerAction() {
        const cb = root._actionCallback;
        root.dismiss();
        if (cb) cb();
    }

    Timer {
        id: dismissTimer
        interval: root.duration
        repeat: false
        onTriggered: root.dismiss()
    }
}
