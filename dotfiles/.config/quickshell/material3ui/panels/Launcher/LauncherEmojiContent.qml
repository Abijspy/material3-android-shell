import "../../core"
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import Quickshell

ColumnLayout {
    id: root

    property bool isSpotlight: false
    property var launcherContent: null
    property int selectedIndex: 0
    property bool isKeyboardNavigation: false
    property string jumpSectionLabel: ""
    property bool jumpPending: false
    readonly property real emojiCellSize: 54 * Appearance.effectiveScale
    readonly property real emojiGridSpacing: 2 * Appearance.effectiveScale
    readonly property real emojiGridWidth: launcherContent ? (launcherContent.width - (isSpotlight ? 32 : 48) * Appearance.effectiveScale) : width
    readonly property int emojiColumns: Math.max(4, Math.floor((root.emojiGridWidth + root.emojiGridSpacing) / (root.emojiCellSize + root.emojiGridSpacing)))
    readonly property real emojiCellW: (root.emojiGridWidth - (root.emojiColumns - 1) * root.emojiGridSpacing) / root.emojiColumns
    readonly property real emojiHeaderHeight: 30 * Appearance.effectiveScale
    readonly property var emojiView: root.buildEmojiView()

    function emojiTabIcon(category) {
        const icons = {
            "Recent": "history",
            "Smileys & Emotion": "mood",
            "People & Body": "face",
            "Animals & Nature": "pets",
            "Food & Drink": "restaurant",
            "Travel & Places": "explore",
            "Activities": "sports_soccer",
            "Objects": "lightbulb",
            "Symbols": "tag",
            "Flags": "flag"
        };
        return icons[category] || "mood";
    }

    function pushEmojiRows(model, flat, rowMap, emojis, cols) {
        for (let i = 0; i < emojis.length; i += cols) {
            const items = emojis.slice(i, i + cols);
            const row = {
                "type": "emoji",
                "items": items,
                "startIndex": flat.length
            };
            model.push(row);
            const rowIdx = model.length - 1;
            for (let c = 0; c < items.length; c++) rowMap.push(rowIdx)
            for (const it of items) flat.push(it)
        }
    }

    function buildEmojiView() {
        const cols = root.emojiColumns;
        const model = [];
        const flat = [];
        const rowMap = [];
        const sections = [];
        if (LauncherSearch.emojiQuery !== "") {
            const secStart = flat.length;
            root.pushEmojiRows(model, flat, rowMap, LauncherSearch.emojiSearchResults, cols);
            if (flat.length > secStart)
                sections.push({
                "label": "",
                "start": secStart,
                "end": flat.length
            });

        } else {
            for (const section of LauncherSearch.emojiSections) {
                model.push({
                    "type": "header",
                    "label": section.label
                });
                const secStart = flat.length;
                root.pushEmojiRows(model, flat, rowMap, section.emojis, cols);
                sections.push({
                    "label": section.label,
                    "start": secStart,
                    "end": flat.length
                });
            }
        }
        return {
            "model": model,
            "flat": flat,
            "rowMap": rowMap,
            "sections": sections
        };
    }

    function emojiNavigate(dx, dy) {
        const flat = root.emojiView.flat;
        if (!flat || flat.length === 0)
            return 0;

        const fromIdx = Math.min(Math.max(0, root.selectedIndex), flat.length - 1);
        if (dx !== 0)
            return Math.min(flat.length - 1, Math.max(0, fromIdx + dx));

        const sections = root.emojiView.sections;
        let secIdx = -1;
        for (let i = 0; i < sections.length; i++) {
            if (fromIdx >= sections[i].start && fromIdx < sections[i].end) {
                secIdx = i;
                break;
            }
        }
        if (secIdx < 0)
            return fromIdx;

        const sec = sections[secIdx];
        const col = (fromIdx - sec.start) % root.emojiColumns;
        const target = fromIdx + dy * root.emojiColumns;
        if (dy > 0) {
            if (target < sec.end)
                return target;

            const next = sections[secIdx + 1];
            if (!next)
                return flat.length - 1;

            return Math.min(next.end - 1, next.start + col);
        }
        if (dy < 0) {
            if (target >= sec.start)
                return target;

            const prev = sections[secIdx - 1];
            if (!prev)
                return 0;

            const lastRowLen = (prev.end - prev.start - 1) % root.emojiColumns + 1;
            const lastRowStart = prev.end - lastRowLen;
            return col < lastRowLen ? lastRowStart + col : prev.end - 1;
        }
        return fromIdx;
    }

    function jumpToSection(label) {
        LauncherSearch.selectedEmojiCategory = label;
        root.isKeyboardNavigation = false;
        const target = label === "Recent" ? "Recent Emoji" : label;
        const model = root.emojiView.model;
        for (let i = 0; i < model.length; i++) {
            if (model[i].type === "header" && model[i].label === target) {
                root.jumpSectionLabel = target;
                root.jumpPending = true;
                if (i + 1 < model.length && model[i + 1].type === "emoji")
                    root.selectedIndex = model[i + 1].startIndex;

                emojiListView.positionViewAtIndex(i, ListView.Beginning);
                break;
            }
        }
    }

    function updateActiveSection() {
        if (!LauncherSearch.isEmojiMode)
            return ;

        if (root.isKeyboardNavigation)
            return ;

        const model = root.emojiView.model;
        const spacing = 4 * Appearance.effectiveScale;
        const y = emojiListView.contentY;
        let acc = 0;
        let label = "";
        let activeRow = -1;
        for (let i = 0; i < model.length; i++) {
            if (model[i].type === "header")
                label = model[i].label;

            const h = model[i].type === "header" ? root.emojiHeaderHeight : root.emojiCellSize;
            if (acc >= y) {
                activeRow = i;
                break;
            }
            acc += h + spacing;
        }
        if (activeRow < 0)
            activeRow = model.length - 1;

        if (model[activeRow] && model[activeRow].type === "header")
            activeRow++;

        const reachedJump = !root.jumpPending || label === root.jumpSectionLabel;
        if (!reachedJump)
            return ;

        root.jumpPending = false;
        if (model[activeRow] && model[activeRow].type === "emoji")
            root.selectedIndex = model[activeRow].startIndex;

        const tabLabel = label === "Recent Emoji" ? "Recent" : label;
        if (tabLabel && LauncherSearch.selectedEmojiCategory !== tabLabel)
            LauncherSearch.selectedEmojiCategory = tabLabel;

    }

    function syncEmojiTab(index) {
        if (LauncherSearch.emojiQuery !== "")
            return ;

        const sections = root.emojiView.sections;
        for (let i = 0; i < sections.length; i++) {
            if (index >= sections[i].start && index < sections[i].end) {
                const tabLabel = sections[i].label === "Recent Emoji" ? "Recent" : sections[i].label;
                if (tabLabel && LauncherSearch.selectedEmojiCategory !== tabLabel)
                    LauncherSearch.selectedEmojiCategory = tabLabel;

                break;
            }
        }
    }

    spacing: isSpotlight ? 12 * Appearance.effectiveScale : 16 * Appearance.effectiveScale
    onSelectedIndexChanged: {
        if (!visible)
            return ;

        if (GlobalStates.spotlightOpen || GlobalStates.launcherOpen) {
            if (root.isKeyboardNavigation) {
                if (root.emojiView.rowMap) {
                    const rowIdx = root.emojiView.rowMap[selectedIndex];
                    if (rowIdx !== undefined && rowIdx >= 0)
                        emojiListView.positionViewAtIndex(rowIdx, ListView.Contain);

                }
                root.syncEmojiTab(selectedIndex);
            }
        }
    }

    Connections {
        function onQueryChanged() {
            if (LauncherSearch.isEmojiMode)
                Qt.callLater(() => {
                    if (emojiListView.model && emojiListView.model.length > 0)
                        emojiListView.positionViewAtIndex(0, ListView.Beginning);

                });

        }

        target: LauncherSearch
    }

    // ── Emoji Category Tabs (icon only) ──
    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 36 * Appearance.effectiveScale
        visible: LauncherSearch.emojiQuery === ""

        ListView {
            id: emojiTabList

            anchors.fill: parent
            orientation: ListView.Horizontal
            spacing: 8 * Appearance.effectiveScale
            model: LauncherSearch.emojiTabs
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            delegate: RippleButton {
                width: 36 * Appearance.effectiveScale
                height: 36 * Appearance.effectiveScale
                buttonRadius: 18 * Appearance.effectiveScale
                colBackground: LauncherSearch.selectedEmojiCategory === modelData ? Appearance.m3colors.m3primary : Appearance.m3colors.m3surfaceContainerHigh
                colRipple: Appearance.m3colors.m3onPrimary
                onClicked: root.jumpToSection(modelData)

                MaterialSymbol {
                    anchors.centerIn: parent
                    text: root.emojiTabIcon(modelData)
                    iconSize: 20 * Appearance.effectiveScale
                    color: LauncherSearch.selectedEmojiCategory === modelData ? Appearance.m3colors.m3onPrimary : Appearance.m3colors.m3onSurfaceVariant
                }

            }

        }

    }

    // ── Main Content Container ──
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        ListView {
            id: emojiListView

            anchors.fill: parent
            interactive: true
            clip: true
            spacing: 4 * Appearance.effectiveScale
            onContentYChanged: Qt.callLater(root.updateActiveSection)
            model: root.visible ? root.emojiView.model : []

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: false
                onWheel: (event) => {
                    root.isKeyboardNavigation = false;
                    root.jumpPending = false;
                    event.accepted = false;
                }
            }

            delegate: Item {
                id: rowDelegate

                property var rowData: modelData

                width: emojiListView.width
                height: rowData.type === "header" ? root.emojiHeaderHeight : root.emojiCellSize

                StyledText {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: rowData.type === "header"
                    text: rowData.type === "header" ? rowData.label : ""
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    font.weight: Font.DemiBold
                    color: Appearance.m3colors.m3primary
                    opacity: 0.9
                    elide: Text.ElideRight
                }

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: rowData.type === "emoji"
                    spacing: root.emojiGridSpacing

                    Repeater {
                        model: rowData.items

                        delegate: EmojiCell {
                            width: root.emojiCellW
                            height: root.emojiCellSize
                            emoji: modelData
                            selected: rowData.startIndex + index === root.selectedIndex
                            onHoveredChanged: {
                                if (hovered && (GlobalStates.launcherOpen || GlobalStates.spotlightOpen)) {
                                    root.isKeyboardNavigation = false;
                                    root.jumpPending = false;
                                    root.selectedIndex = rowData.startIndex + index;
                                    root.syncEmojiTab(rowData.startIndex + index);
                                }
                            }
                        }

                    }

                }

            }

        }

    }

}
