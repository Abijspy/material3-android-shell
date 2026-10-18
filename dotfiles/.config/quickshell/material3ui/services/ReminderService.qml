pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

/**
 * Reminder Service — manages reminders and persistence.
 *
 * Schema per reminder:
 * {
 *   id          : string  — unique ID
 *   text        : string  — reminder message
 *   date        : string  — YYYY-MM-DD
 *   time        : string  — HH:MM
 *   type        : string  — "basic" | "notepad" | "todo"
 *   linkedId    : string  — ID of linked notepad/todo item (empty if basic)
 *   linkedTitle : string  — display label for linked item (for notifications)
 *   fired       : bool    — true after notification has been sent
 *   lastFiredDate: string — YYYY-MM-DD guard to avoid double-firing
 * }
 */
Singleton {
    id: root

    property var reminders: []
    readonly property string storagePath: Directories.remindersPath

    function save() {
        reminderFile.setText(JSON.stringify(root.reminders, null, 2))
    }

    function addReminder(reminder) {
        root.reminders = [...root.reminders, reminder]
        save()
    }

    function updateReminder(id, updatedFields) {
        root.reminders = root.reminders.map(r =>
            r.id === id ? Object.assign({}, r, updatedFields) : r
        )
        save()
    }

    function deleteReminder(id) {
        root.reminders = root.reminders.filter(r => r.id !== id)
        save()
    }

    property bool _created: false

    FileView {
        id: reminderFile
        path: root.storagePath
        watchChanges: false
        // Silence the "Read of ... failed: File does not exist" warning on first
        // run (Directories.qml touches the file async via execDetached, which can
        // lose the race with this initial read). Instead we create the file here.
        printErrors: false
        blockWrites: true
        onLoaded: {
            try {
                const content = reminderFile.text()
                if (content && content.trim() !== "") {
                    const parsed = JSON.parse(content)
                    if (Array.isArray(parsed)) root.reminders = parsed
                }
            } catch(e) {
                console.warn("ReminderService: failed to parse reminders.json:", e)
            }
        }
        onLoadFailed: {
            // First run: file doesn't exist yet. FileView creates parent dirs on
            // write, so seeding an empty list makes subsequent reads succeed.
            if (root._created) return;
            root._created = true;
            reminderFile.setText("[]")
            reminderFile.reload()
        }
    }

    Component.onCompleted: {
        reminderFile.reload()
    }
}
