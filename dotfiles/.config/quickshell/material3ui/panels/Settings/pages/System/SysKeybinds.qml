import "../../../../core"
import "../../../../services"
import "../../../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Quickshell

/**
 * Hyprland keybind editor.
 * Lists Material3UI's binds live from Hyprland, grouped and searchable. Click a
 * bind to remap it (modifier chips + press a key), disable it or reset it.
 * Also adds custom command binds and shows binds from user.lua read-only.
 */
ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: 0

    property string query: ""
    property string editingId: ""      // bind being remapped
    property bool addingCustom: false
    property bool showOthers: false

    readonly property var groupOrder: ["Shell", "Apps", "Windows", "Workspaces", "Tools", "Media"]
    readonly property var groupIcons: ({
        Shell: "dashboard", Apps: "apps", Windows: "select_window", Workspaces: "view_carousel",
        Tools: "screenshot_region", Media: "music_note", Other: "keyboard"
    })

    readonly property var filtered: {
        const q = root.query.trim().toLowerCase();
        return Keybinds.binds.filter(b => !q || b.label.toLowerCase().includes(q) || b.combo.toLowerCase().includes(q));
    }
    readonly property var groups: {
        const present = [...new Set(root.filtered.map(b => b.group))];
        return root.groupOrder.filter(g => present.includes(g)).concat(present.filter(g => !root.groupOrder.includes(g)));
    }

    Component.onCompleted: Keybinds.refresh()

    SearchHandler {
        searchString: "Keybinds"
        aliases: ["Keyboard Shortcuts", "Shortcuts", "Hotkeys", "Key bindings", "Hyprland binds", "Remap"]
    }

    // ── Header ──
    RowLayout {
        Layout.fillWidth: true
        spacing: 12 * Appearance.effectiveScale
        Layout.bottomMargin: 8 * Appearance.effectiveScale
        MaterialSymbol {
            text: "keyboard"
            iconSize: 24 * Appearance.effectiveScale
            color: Appearance.colors.colPrimary
        }
        StyledText {
            text: I18nService.tr("Keybinds")
            font.pixelSize: Appearance.font.pixelSize.large
            font.family: Appearance.font.family.title
            font.weight: Font.Medium
            color: Appearance.colors.colOnLayer1
            Layout.fillWidth: true
        }
        IconAction {
            visible: Keybinds.supported
            icon: Keybinds.loading ? "progress_activity" : "refresh"
            tooltip: I18nService.tr("Reload binds from Hyprland")
            onClicked: Keybinds.refresh()
        }
    }

    // ── Legacy config notice ──
    Notice {
        visible: !Keybinds.supported
        icon: "info"
        text: I18nService.tr("The keybind editor needs Hyprland's Lua config (0.55+). Edit binds in your hyprland.conf instead.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: Keybinds.supported
        spacing: 12 * Appearance.effectiveScale

        StyledText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: I18nService.tr("Click a bind to change its keys. Changes apply immediately and survive updates.")
            font.pixelSize: Appearance.font.pixelSize.small
            color: Appearance.colors.colSubtext
        }

        StyledTextInput {
            Layout.fillWidth: true
            implicitHeight: 48 * Appearance.effectiveScale
            inputRadius: 24
            leftMargin: 48
            placeholder: I18nService.tr("Search binds by action or key")
            onTextChanged: root.query = text
            MaterialSymbol {
                anchors.left: parent.left
                anchors.leftMargin: 16 * Appearance.effectiveScale
                anchors.verticalCenter: parent.verticalCenter
                text: "search"
                iconSize: 22 * Appearance.effectiveScale
                color: Appearance.colors.colSubtext
            }
        }

        StyledText {
            visible: !Keybinds.loading && Keybinds.binds.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: I18nService.tr("No Material3UI binds found. Make sure hyprland.lua loads material3ui.shell and material3ui.keybinds.")
            color: Appearance.colors.colSubtext
        }

        // ── Groups ──
        Repeater {
            model: root.groups
            delegate: ColumnLayout {
                id: groupCol
                required property string modelData
                Layout.fillWidth: true
                spacing: 2 * Appearance.effectiveScale

                GroupTitle {
                    icon: root.groupIcons[groupCol.modelData] || "keyboard"
                    text: groupCol.modelData
                }

                Repeater {
                    model: root.filtered.filter(b => b.group === groupCol.modelData)
                    delegate: BindRow {
                        required property var modelData
                        bind: modelData
                        editing: root.editingId === modelData.id
                        onEditRequested: { root.addingCustom = false; root.editingId = modelData.id; }
                        onEditFinished: root.editingId = ""
                    }
                }
            }
        }

        // ── Disabled defaults ──
        ColumnLayout {
            Layout.fillWidth: true
            visible: Keybinds.disabled.length > 0
            spacing: 2 * Appearance.effectiveScale

            GroupTitle { icon: "block"; text: I18nService.tr("Disabled") }

            Repeater {
                model: Keybinds.disabled
                delegate: CardRow {
                    id: disabledRow
                    required property var modelData
                    StyledText {
                        text: Keybinds.actionOf(disabledRow.modelData.label)
                        color: Appearance.colors.colSubtext
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    StyledText {
                        text: disabledRow.modelData.group
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colSubtext
                    }
                    PillButton {
                        label: I18nService.tr("Enable")
                        icon: "undo"
                        onClicked: Keybinds.reset(disabledRow.modelData.id)
                    }
                }
            }
        }

        // ── Custom binds ──
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2 * Appearance.effectiveScale

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8 * Appearance.effectiveScale
                Layout.bottomMargin: 6 * Appearance.effectiveScale
                spacing: 8 * Appearance.effectiveScale
                MaterialSymbol { text: "add_circle"; iconSize: 20 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
                StyledText {
                    text: I18nService.tr("Custom")
                    font.pixelSize: Appearance.font.pixelSize.normal
                    font.weight: Font.Medium
                    color: Appearance.colors.colOnLayer1
                    Layout.fillWidth: true
                }
                PillButton {
                    visible: !root.addingCustom
                    label: I18nService.tr("Add bind")
                    icon: "add"
                    primary: true
                    onClicked: { root.editingId = ""; root.addingCustom = true; }
                }
            }

            Repeater {
                model: Keybinds.store.custom
                delegate: CardRow {
                    id: customRow
                    required property var modelData
                    required property int index
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1 * Appearance.effectiveScale
                        StyledText {
                            text: customRow.modelData.label
                            color: Appearance.colors.colOnLayer1
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        StyledText {
                            text: customRow.modelData.command
                            font.family: Appearance.font.family.monospace
                            font.pixelSize: Appearance.font.pixelSize.smaller
                            color: Appearance.colors.colSubtext
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }
                    KeyCombo { combo: customRow.modelData.keys }
                    IconAction {
                        icon: "delete"
                        tooltip: I18nService.tr("Remove")
                        onClicked: Keybinds.removeCustom(customRow.index)
                    }
                }
            }

            StyledText {
                visible: Keybinds.store.custom.length === 0 && !root.addingCustom
                text: I18nService.tr("Bind any key combination to a command, e.g. a script or an app.")
                font.pixelSize: Appearance.font.pixelSize.small
                color: Appearance.colors.colSubtext
            }

            Card {
                visible: root.addingCustom
                Layout.fillWidth: true

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10 * Appearance.effectiveScale

                    StyledTextInput {
                        id: customLabel
                        Layout.fillWidth: true
                        placeholder: I18nService.tr("Name (e.g. Open VS Code)")
                    }
                    StyledTextInput {
                        id: customCommand
                        Layout.fillWidth: true
                        placeholder: I18nService.tr("Command (e.g. code)")
                        font.family: Appearance.font.family.monospace
                    }
                    ComboEditor {
                        id: customCombo
                        Layout.fillWidth: true
                        exceptId: ""
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8 * Appearance.effectiveScale
                        Item { Layout.fillWidth: true }
                        PillButton {
                            label: I18nService.tr("Cancel")
                            onClicked: root.closeCustom()
                        }
                        PillButton {
                            label: I18nService.tr("Add")
                            icon: "check"
                            primary: true
                            enabled: customCommand.text.trim() !== "" && customCombo.valid
                            onClicked: {
                                Keybinds.addCustom(customCombo.combo, customCommand.text.trim(), customLabel.text.trim());
                                root.closeCustom();
                            }
                        }
                    }
                }
            }
        }

        // ── Binds Material3UI doesn't manage ──
        ColumnLayout {
            Layout.fillWidth: true
            visible: Keybinds.others.length > 0
            spacing: 2 * Appearance.effectiveScale

            RippleButton {
                Layout.fillWidth: true
                Layout.topMargin: 8 * Appearance.effectiveScale
                implicitHeight: 40 * Appearance.effectiveScale
                buttonRadius: 20 * Appearance.effectiveScale
                colBackground: "transparent"
                onClicked: root.showOthers = !root.showOthers
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4 * Appearance.effectiveScale
                    spacing: 8 * Appearance.effectiveScale
                    MaterialSymbol { text: root.showOthers ? "expand_less" : "expand_more"; iconSize: 20 * Appearance.effectiveScale; color: Appearance.colors.colSubtext }
                    StyledText {
                        text: I18nService.tr("Other binds (%1) — from user.lua or other configs; edit them there").arg(Keybinds.others.length)
                        font.pixelSize: Appearance.font.pixelSize.small
                        color: Appearance.colors.colSubtext
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
            }

            Repeater {
                model: root.showOthers ? Keybinds.others : []
                delegate: CardRow {
                    id: otherRow
                    required property var modelData
                    StyledText {
                        text: otherRow.modelData.label
                        font.family: Appearance.font.family.monospace
                        font.pixelSize: Appearance.font.pixelSize.smaller
                        color: Appearance.colors.colSubtext
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    KeyCombo { combo: otherRow.modelData.combo }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4 * Appearance.effectiveScale
            visible: Object.keys(Keybinds.store.overrides).length > 0
            Item { Layout.fillWidth: true }
            PillButton {
                label: I18nService.tr("Reset all to defaults")
                icon: "restart_alt"
                onClicked: { root.editingId = ""; Keybinds.resetAll(); }
            }
        }
    }

    function closeCustom() {
        root.addingCustom = false;
        customLabel.text = "";
        customCommand.text = "";
        customCombo.load("");
    }

    // ── Components ──

    // One bind; expands into the editor when clicked
    component BindRow: SegmentedWrapper {
        id: bindRow
        property var bind
        property bool editing: false
        signal editRequested()
        signal editFinished()

        Layout.fillWidth: true
        implicitHeight: editing ? bindEditCol.implicitHeight + 28 * Appearance.effectiveScale : 56 * Appearance.effectiveScale
        orientation: Qt.Vertical
        color: Appearance.m3colors.m3surfaceContainerHigh
        forcePill: false

        RippleButton {
            anchors.fill: parent
            visible: !bindRow.editing
            colBackground: Appearance.m3colors.m3surfaceContainerHigh
            buttonRadius: 0
            topLeftRadius: bindRow.rTopLeft
            topRightRadius: bindRow.rTopRight
            bottomLeftRadius: bindRow.rBottomLeft
            bottomRightRadius: bindRow.rBottomRight
            onClicked: {
                bindEditor.load(bindRow.bind.combo);
                bindRow.editRequested();
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16 * Appearance.effectiveScale
            anchors.rightMargin: 12 * Appearance.effectiveScale
            visible: !bindRow.editing
            spacing: 10 * Appearance.effectiveScale

            StyledText {
                text: Keybinds.actionOf(bindRow.bind.label)
                color: Appearance.colors.colOnLayer1
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            MaterialSymbol {
                visible: bindRow.bind.conflict
                text: "warning"
                iconSize: 18 * Appearance.effectiveScale
                color: Appearance.m3colors.m3error
                StyledToolTip {
                    alternativeVisibleCondition: conflictHover.hovered
                    text: I18nService.tr("Another bind uses the same keys")
                }
                HoverHandler { id: conflictHover }
            }
            Rectangle {
                visible: bindRow.bind.changed
                implicitWidth: 6 * Appearance.effectiveScale
                implicitHeight: implicitWidth
                radius: width / 2
                color: Appearance.colors.colPrimary
            }
            KeyCombo { combo: bindRow.bind.combo }
        }

        ColumnLayout {
            id: bindEditCol
            visible: bindRow.editing
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14 * Appearance.effectiveScale
            anchors.leftMargin: 16 * Appearance.effectiveScale
            spacing: 10 * Appearance.effectiveScale

            RowLayout {
                Layout.fillWidth: true
                StyledText {
                    text: Keybinds.actionOf(bindRow.bind.label)
                    font.weight: Font.Medium
                    color: Appearance.colors.colOnLayer1
                    Layout.fillWidth: true
                }
                StyledText {
                    visible: bindRow.bind.changed
                    text: I18nService.tr("Default: %1").arg(Keybinds.prettyCombo(bindRow.bind.id))
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: Appearance.colors.colSubtext
                }
            }

            ComboEditor {
                id: bindEditor
                Layout.fillWidth: true
                exceptId: bindRow.bind.id
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8 * Appearance.effectiveScale
                PillButton {
                    label: I18nService.tr("Disable")
                    icon: "block"
                    onClicked: { Keybinds.disable(bindRow.bind.id, bindRow.bind.label); bindRow.editFinished(); }
                }
                PillButton {
                    visible: bindRow.bind.changed
                    label: I18nService.tr("Reset")
                    icon: "undo"
                    onClicked: { Keybinds.reset(bindRow.bind.id); bindRow.editFinished(); }
                }
                Item { Layout.fillWidth: true }
                PillButton {
                    label: I18nService.tr("Cancel")
                    onClicked: bindRow.editFinished()
                }
                PillButton {
                    label: I18nService.tr("Save")
                    icon: "check"
                    primary: true
                    enabled: bindEditor.valid && Keybinds.idOf(bindEditor.combo) !== Keybinds.idOf(bindRow.bind.combo)
                    onClicked: { Keybinds.remap(bindRow.bind.id, bindEditor.combo, bindRow.bind.label); bindRow.editFinished(); }
                }
            }
        }
    }

    // Modifier chips + a "press a key" field. Hyprland grabs combos it already
    // knows before the shell sees them, so modifiers are picked with chips and
    // only the key itself is captured.
    component ComboEditor: ColumnLayout {
        id: editor
        property string exceptId: ""
        property var mods: ({ SUPER: true, CTRL: false, ALT: false, SHIFT: false })
        property string key: ""
        property bool capturing: false
        readonly property string combo: Keybinds.modOrder.filter(m => editor.mods[m]).concat([editor.key]).join(" + ")
        readonly property bool valid: editor.key !== ""
        readonly property var conflicts: valid ? Keybinds.conflictsFor(combo, exceptId) : []

        spacing: 8 * Appearance.effectiveScale

        function load(combo) {
            const parts = Keybinds.normalize(combo).split(" + ");
            const m = { SUPER: false, CTRL: false, ALT: false, SHIFT: false };
            let k = "";
            for (const p of parts) { if (p in m) m[p] = true; else k = p; }
            if (!combo) m.SUPER = true;
            editor.mods = m;
            editor.key = k;
            editor.capturing = false;
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6 * Appearance.effectiveScale

            Repeater {
                model: Keybinds.modOrder
                delegate: SegmentedButton {
                    required property string modelData
                    buttonText: Keybinds.prettyKey(modelData)
                    isHighlighted: editor.mods[modelData]
                    colActive: Appearance.m3colors.m3secondaryContainer
                    colActiveText: Appearance.m3colors.m3onSecondaryContainer
                    colInactive: Appearance.m3colors.m3surfaceContainerLow
                    onClicked: {
                        const m = Object.assign({}, editor.mods);
                        m[modelData] = !m[modelData];
                        editor.mods = m;
                    }
                }
            }

            StyledText {
                text: "+"
                color: Appearance.colors.colSubtext
            }

            // Key capture
            Rectangle {
                id: capture
                Layout.fillWidth: true
                implicitHeight: 40 * Appearance.effectiveScale
                radius: height / 2
                color: editor.capturing ? Appearance.m3colors.m3primaryContainer : Appearance.m3colors.m3surfaceContainerLow
                border.width: editor.capturing ? 2 : 0
                border.color: Appearance.colors.colPrimary
                focus: editor.capturing

                StyledText {
                    anchors.centerIn: parent
                    text: editor.capturing ? I18nService.tr("Press a key…")
                        : editor.key ? Keybinds.prettyKey(editor.key) : I18nService.tr("Click, then press a key")
                    font.weight: editor.key && !editor.capturing ? Font.Medium : Font.Normal
                    color: editor.capturing ? Appearance.m3colors.m3onPrimaryContainer
                        : editor.key ? Appearance.colors.colOnLayer1 : Appearance.colors.colSubtext
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { editor.capturing = true; capture.forceActiveFocus(); }
                }

                Keys.onPressed: event => {
                    if (!editor.capturing) return;
                    const name = Keybinds.keyName(event.key);
                    event.accepted = true;
                    if (!name) return; // a bare modifier: wait for the real key
                    // Pick up modifiers too when Hyprland let the combo through
                    const m = Object.assign({}, editor.mods);
                    if (event.modifiers & (Qt.MetaModifier | Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier)) {
                        m.SUPER = !!(event.modifiers & Qt.MetaModifier);
                        m.CTRL = !!(event.modifiers & Qt.ControlModifier);
                        m.ALT = !!(event.modifiers & Qt.AltModifier);
                        m.SHIFT = !!(event.modifiers & Qt.ShiftModifier);
                    }
                    editor.mods = m;
                    editor.key = name;
                    editor.capturing = false;
                }
                onActiveFocusChanged: if (!activeFocus) editor.capturing = false
            }
        }

        RowLayout {
            visible: editor.conflicts.length > 0
            Layout.fillWidth: true
            spacing: 6 * Appearance.effectiveScale
            MaterialSymbol { text: "warning"; iconSize: 16 * Appearance.effectiveScale; color: Appearance.m3colors.m3error }
            StyledText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: I18nService.tr("Also used by: %1. Both will run.").arg(editor.conflicts.join(", "))
                font.pixelSize: Appearance.font.pixelSize.smaller
                color: Appearance.m3colors.m3error
            }
        }
    }

    // "Super" "+" "T" chips
    component KeyCombo: RowLayout {
        id: keyCombo
        property string combo
        spacing: 4 * Appearance.effectiveScale
        Repeater {
            model: keyCombo.combo ? Keybinds.normalize(keyCombo.combo).split(" + ") : []
            delegate: Rectangle {
                required property string modelData
                implicitWidth: Math.max(implicitHeight, keyText.implicitWidth + 14 * Appearance.effectiveScale)
                implicitHeight: 26 * Appearance.effectiveScale
                radius: 8 * Appearance.effectiveScale
                color: Appearance.m3colors.m3surfaceContainerLow
                border.width: 1
                border.color: Appearance.m3colors.m3outlineVariant
                StyledText {
                    id: keyText
                    anchors.centerIn: parent
                    text: Keybinds.prettyKey(parent.modelData)
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    font.weight: Font.Medium
                    color: Appearance.colors.colOnLayer1
                }
            }
        }
    }

    component GroupTitle: RowLayout {
        id: groupTitle
        property string icon
        property string text
        Layout.fillWidth: true
        Layout.topMargin: 8 * Appearance.effectiveScale
        Layout.bottomMargin: 6 * Appearance.effectiveScale
        spacing: 8 * Appearance.effectiveScale
        MaterialSymbol { text: groupTitle.icon; iconSize: 20 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
        StyledText {
            text: groupTitle.text
            font.pixelSize: Appearance.font.pixelSize.normal
            font.weight: Font.Medium
            color: Appearance.colors.colOnLayer1
            Layout.fillWidth: true
        }
    }

    component CardRow: SegmentedWrapper {
        id: cardRow
        default property alias content: cardRowLayout.data
        Layout.fillWidth: true
        implicitHeight: Math.max(56 * Appearance.effectiveScale, cardRowLayout.implicitHeight + 16 * Appearance.effectiveScale)
        orientation: Qt.Vertical
        color: Appearance.m3colors.m3surfaceContainerHigh
        RowLayout {
            id: cardRowLayout
            anchors.fill: parent
            anchors.leftMargin: 16 * Appearance.effectiveScale
            anchors.rightMargin: 12 * Appearance.effectiveScale
            spacing: 10 * Appearance.effectiveScale
        }
    }

    component Card: Rectangle {
        id: card
        default property alias content: cardCol.data
        implicitHeight: cardCol.implicitHeight + 32 * Appearance.effectiveScale
        radius: 20 * Appearance.effectiveScale
        color: Appearance.m3colors.m3surfaceContainerHigh
        ColumnLayout {
            id: cardCol
            anchors.fill: parent
            anchors.margins: 16 * Appearance.effectiveScale
        }
    }

    component Notice: Rectangle {
        id: notice
        property string icon
        property string text
        Layout.fillWidth: true
        implicitHeight: noticeRow.implicitHeight + 28 * Appearance.effectiveScale
        radius: 20 * Appearance.effectiveScale
        color: Appearance.m3colors.m3surfaceContainerHigh
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 14 * Appearance.effectiveScale
            anchors.leftMargin: 16 * Appearance.effectiveScale
            spacing: 12 * Appearance.effectiveScale
            MaterialSymbol { text: notice.icon; iconSize: 22 * Appearance.effectiveScale; color: Appearance.colors.colPrimary }
            StyledText {
                text: notice.text
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Appearance.colors.colOnLayer1
            }
        }
    }

    component PillButton: RippleButton {
        id: pill
        property string label
        property string icon: ""
        property bool primary: false
        implicitHeight: 36 * Appearance.effectiveScale
        implicitWidth: pillRow.implicitWidth + 28 * Appearance.effectiveScale
        buttonRadius: 18 * Appearance.effectiveScale
        opacity: enabled ? 1 : 0.5
        colBackground: primary ? Appearance.colors.colPrimary : Appearance.m3colors.m3surfaceContainerLow
        RowLayout {
            id: pillRow
            anchors.centerIn: parent
            spacing: 6 * Appearance.effectiveScale
            MaterialSymbol {
                visible: pill.icon !== ""
                text: pill.icon
                iconSize: 18 * Appearance.effectiveScale
                color: pill.primary ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer1
            }
            StyledText {
                text: pill.label
                font.pixelSize: Appearance.font.pixelSize.small
                font.weight: Font.Medium
                color: pill.primary ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer1
            }
        }
    }

    component IconAction: RippleButton {
        id: iconAction
        property string icon
        property string tooltip: ""
        implicitWidth: 36 * Appearance.effectiveScale
        implicitHeight: 36 * Appearance.effectiveScale
        buttonRadius: 18 * Appearance.effectiveScale
        colBackground: "transparent"
        MaterialSymbol {
            anchors.centerIn: parent
            text: iconAction.icon
            iconSize: 20 * Appearance.effectiveScale
            color: Appearance.colors.colOnLayer1
        }
        StyledToolTip {
            extraVisibleCondition: iconAction.tooltip !== "" && (iconAction.hovered || iconAction.realHovered)
            text: iconAction.tooltip
        }
    }
}
