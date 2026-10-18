import "../../core"
import "../../core/functions" as Functions
import "../../services"
import "../../widgets"
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

/**
 * Quick Settings toggle button — supports size 1 (icon-only) and size 2 (expanded with label).
 *
 * Normal mode:
 *   - Left-click: toggle action (or open detail panel if expanded + hasDetails)
 *   - Right-click: open detail panel (for icon-only toggles with details)
 *
 * Edit mode (card previews as "off", icon keeps the real state; blocking MouseArea on top):
 *   - Left-click: enable/disable toggle (add/remove from list)
 *   - Right-click or drag the right-edge handle: resize (1 ↔ 2)
 *   - Scroll: reorder position
 */
RippleButton {
    id: root
    focusPolicy: Qt.NoFocus

    // Data from repeater
    required property int buttonIndex
    required property var buttonData
    required property var allToggles
    required property bool editMode
    required property real baseCellWidth
    required property real baseCellHeight
    required property real cellSpacing

    // Signals
    signal openDetails()



    // Keyboard navigation support (host registers this delegate for focus ring)
    // Keyed by the toggle's stable `type`, so navigation stays correct for any
    // toggle count/size/order and survives delegate recreation.
    property var keyboardHost: null
    property string _keyboardType: ""

    function _registerKey() {
        if (!root.keyboardHost) return;
        const t = root.buttonData?.type ?? "";
        if (t === "" || t === root._keyboardType) return;
        if (root._keyboardType !== "") {
            root.keyboardHost.unregisterToggleDelegate(root._keyboardType);
        }
        root.keyboardHost.registerToggleDelegate(t, root);
        root._keyboardType = t;
    }

    Component.onCompleted: {
        root._registerKey();
    }
    Component.onDestruction: {
        if (root.rowCoordinator && root.rowCoordinator.pressedIndex === root.rowIndex) {
            root.rowCoordinator.pressedIndex = -1;
        }
        if (root.keyboardHost && root._keyboardType !== "") {
            root.keyboardHost.unregisterToggleDelegate(root._keyboardType, root);
        }
    }
    onButtonDataChanged: root._registerKey()

    // Resolved toggle info
    property var toggleData: allToggles ? (allToggles[buttonData.type] ?? null) : null
    property bool isToggled: toggleData?.toggled ?? false
    // Edit mode previews the card in its "off" state while the icon keeps
    // reflecting the toggle's real state
    readonly property bool visualToggled: root.editMode ? false : isToggled
    property bool expandedSize: (buttonData?.size ?? 1) > 1
    property bool hasMenu: !editMode && expandedSize && (toggleData?.hasDetails ?? false)

    // Sizing
    property int cellSize: buttonData?.size ?? 1

    // ── Press squeeze effect ──
    // While a toggle is pressed it widens slightly and its horizontal neighbors
    // in the same row shrink to compensate (half each, or the full amount if the
    // pressed toggle only has one neighbor). The shared press state lives on the
    // row (RowLayout) which acts as coordinator for its toggles.
    property real squeezeAmount: 10 * Appearance.effectiveScale
    property var rowCoordinator: null
    property int rowIndex: 0
    readonly property real baseCellTotalWidth: Math.floor(baseCellWidth * cellSize + cellSpacing * (cellSize - 1))

    Layout.preferredWidth: {
        if (!rowCoordinator || rowCoordinator.pressedIndex < 0)
            return baseCellTotalWidth;
        const p = rowCoordinator.pressedIndex;
        const count = rowCoordinator.rowToggleCount;
        if (p === rowIndex)
            return baseCellTotalWidth + squeezeAmount;
        const isRightNeighbor = p === rowIndex - 1 && p >= 0;
        const isLeftNeighbor = p === rowIndex + 1;
        if (!isRightNeighbor && !isLeftNeighbor)
            return baseCellTotalWidth;
        // Existing neighbors split the bill evenly: both sides -> half each,
        // only one side -> that single neighbor pays the full amount, so the
        // pressed toggle visually expands only toward the occupied side(s).
        const donors = (p > 0 ? 1 : 0) + (p < count - 1 ? 1 : 0);
        return baseCellTotalWidth - squeezeAmount / Math.max(1, donors);
    }

    Behavior on Layout.preferredWidth {
        NumberAnimation {
            duration: Appearance.animation ? Appearance.animation.elementMoveFast.duration : 200
            easing.type: Easing.OutCubic
        }
    }

    // Shared by the card press and the inner icon press so both trigger the
    // same row-level squeeze on the parent card (the icon itself never grows).
    function _setSqueezed(active) {
        if (!root.rowCoordinator || root.editMode) return;
        if (active) {
            root.rowCoordinator.pressedIndex = root.rowIndex;
        } else if (root.rowCoordinator.pressedIndex === root.rowIndex) {
            root.rowCoordinator.pressedIndex = -1;
        }
    }

    // Keyboard activations have no press/release cycle, so pulse instead:
    // expand now, revert automatically after a beat.
    Timer {
        id: squeezeResetTimer
        interval: 200
        onTriggered: root._setSqueezed(false)
    }

    function squeezePulse() {
        root._setSqueezed(true);
        squeezeResetTimer.restart();
    }

    // Menu cards (x2 with details): only the inner icon toggle squeezes the
    // parent card — a plain card click opens the details panel and shouldn't
    // deform the row.
    onDownChanged: {
        if (!root.hasMenu) root._setSqueezed(down);
    }

    Layout.preferredHeight: baseCellHeight

    visible: toggleData !== null && (editMode || (toggleData?.available ?? true))
    enabled: (toggleData?.available ?? true) || editMode
    padding: 6 * Appearance.effectiveScale
    leftPadding: padding
    rightPadding: padding
    topPadding: padding
    bottomPadding: padding

    // Styling
    toggled: hasMenu ? false : visualToggled
    colBackground: Appearance.colors.colLayer2
    colBackgroundHover: Appearance.colors.colLayer2Hover
    colBackgroundToggled: (hasMenu) ? Appearance.colors.colLayer2 : Appearance.colors.colPrimary
    colBackgroundToggledHover: (hasMenu) ? Appearance.colors.colLayer2Hover : Appearance.colors.colPrimary
    
    // In Android 16 / Material 3, squircle radii are usually standardized tokens.
    // 20px is the standard button rounding in this theme (Appearance.rounding.button), 
    // which gives a 14px inner radius (20 - 6 = 14).
    buttonRadius: visualToggled ? Appearance.rounding.button : height / 2

    property color colText: (visualToggled && !hasMenu && enabled) ? Appearance.colors.colOnPrimary : Functions.ColorUtils.transparentize(Appearance.colors.colOnLayer2, enabled ? 0 : 0.7)
    property color colIcon: expandedSize ? (visualToggled ? Appearance.colors.colOnPrimary : Appearance.colors.colOnLayer3) : colText

    // ── Normal mode click handling ──
    function triggerAction() {
        if (toggleData?.action) toggleData.action();
    }

    // Edit-mode helper: write a specific size for this toggle (by config index)
    function editSetSize(size) {
        const toggleList = Config.options.quickSettings?.toggles;
        if (!toggleList) return;
        const idx = root.buttonIndex;
        if (idx < 0 || idx >= toggleList.length) return;
        if ((toggleList[idx].size || 1) === size) return;
        toggleList[idx].size = size;
        Config.options.quickSettings.toggles = toggleList;
    }

    onClicked: {
        if (hasMenu) {
            root.openDetails();
        } else {
            triggerAction();
        }
    }

    altAction: {
        if (!editMode) {
            if (!expandedSize && (toggleData?.hasDetails ?? false)) return (() => root.openDetails());
            if (toggleData?.altAction) return toggleData.altAction;
        }
        return null;
    }

    // Content
    contentItem: Item {
        RowLayout {
            anchors.fill: parent
            spacing: (root.hasMenu ? 8 : 6) * Appearance.effectiveScale
            
            // Spacers for 1x centering
            Item { Layout.fillWidth: true; visible: !root.expandedSize }

            // Icon area (clickable toggle zone for expanded+hasDetails buttons)
            MouseArea {
                id: iconMouseArea
                hoverEnabled: root.hasMenu
                propagateComposedEvents: true
                acceptedButtons: (root.hasMenu) ? Qt.LeftButton : Qt.NoButton
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredHeight: root.hasMenu ? 44 * Appearance.effectiveScale : 44 * Appearance.effectiveScale
                Layout.preferredWidth: root.hasMenu ? 44 * Appearance.effectiveScale : 44 * Appearance.effectiveScale
                cursorShape: Qt.PointingHandCursor

                // Pressing the inner icon squeezes the parent card, not the icon
                onPressed: (event) => root._setSqueezed(true)
                onReleased: (event) => root._setSqueezed(false)
                onCanceled: (event) => root._setSqueezed(false)

                onClicked: {
                    if (root.toggleData?.action) root.toggleData.action();
                }

                Rectangle {
                    id: iconBackground
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: (root.hasMenu && root.visualToggled) ? Math.max(0, root.buttonRadius - root.padding) : height / 2
                    color: {
                        if (root.hasMenu) {
                            return root.visualToggled ? Appearance.colors.colPrimary : Functions.ColorUtils.applyAlpha(Appearance.colors.colOnLayer2, 0.08)
                        } else {
                            return "transparent"
                        }
                    }

                    Behavior on color { ColorAnimation { duration: 150 } }

                    MaterialSymbol {
                        anchors.centerIn: parent
                        fill: root.isToggled ? 1 : 0
                        iconSize: root.expandedSize ? 24 * Appearance.effectiveScale : 22 * Appearance.effectiveScale
                        color: root.colIcon
                        text: root.isToggled
                            ? (root.toggleData?.icon ?? "check")
                            : (root.toggleData?.iconOff ?? root.toggleData?.icon ?? "circle")
                    }

                    // Hover state layer for icon area when it acts as a button
                    Rectangle {
                        anchors.fill: parent
                        radius: iconBackground.radius
                        visible: root.hasMenu
                        color: Functions.ColorUtils.transparentize(
                            root.colIcon, 
                            iconMouseArea.containsPress ? 0.88 : iconMouseArea.containsMouse ? 0.95 : 1
                        )
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                }
            }

            // Text column — only shown when expanded
            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                Layout.fillWidth: true
                visible: root.expandedSize
                spacing: -2 * Appearance.effectiveScale

                StyledText {
                    Layout.fillWidth: true
                    font.pixelSize: Appearance.font.pixelSize.small
                    font.weight: Font.DemiBold
                    color: root.colText
                    elide: Text.ElideRight
                    text: root.toggleData?.name ?? ""
                }

                StyledText {
                    visible: (root.toggleData?.statusText ?? "") !== ""
                    Layout.fillWidth: true
                    font.pixelSize: Appearance.font.pixelSize.smaller
                    color: root.colText
                    elide: Text.ElideRight
                    text: root.toggleData?.statusText ?? ""
                }
            }

            // Spacers for 1x centering
            Item { Layout.fillWidth: true; visible: !root.expandedSize }
        }
    }

    // ── Edit mode: blocking MouseArea ──
    // Sits on top of everything and handles all edit interactions via direct mutation
    MouseArea {
        id: editModeInteraction
        visible: root.editMode
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons

        function toggleEnabled() {
            var toggleList = Config.options.quickSettings?.toggles;
            
            if (!toggleList) return;
            var buttonType = root.buttonData.type;
            var found = false;
            var foundIndex = -1;
            
            for (var i = 0; i < toggleList.length; i++) {
                if (toggleList[i].type === buttonType) { 
                    found = true; 
                    foundIndex = i;
                    break; 
                }
            }

            if (found) {
                toggleList.splice(foundIndex, 1);
            } else {
                toggleList.push({ type: buttonType, size: 1 });
            }
        }

        function toggleSize() {
            var toggleList = Config.options.quickSettings?.toggles;
            if (!toggleList) return;
            var idx = root.buttonIndex;
            if (idx < 0 || idx >= toggleList.length) return;
            root.editSetSize((toggleList[idx].size || 1) === 1 ? 2 : 1);
        }

        function movePositionBy(offset) {
            var toggleList = Config.options.quickSettings?.toggles;
            if (!toggleList) return;
            var idx = root.buttonIndex;
            if (idx < 0) return;
            var targetIndex = idx + offset;
            if (targetIndex < 0 || targetIndex >= toggleList.length) return;
            var temp = toggleList[idx];
            toggleList[idx] = toggleList[targetIndex];
            toggleList[targetIndex] = temp;
        }

        onReleased: (event) => {
            if (event.button === Qt.LeftButton)
                toggleEnabled();
        }
        onPressed: (event) => {
            if (event.button === Qt.RightButton) toggleSize();
        }
        onWheel: (event) => {
            if (event.angleDelta.y < 0) {
                movePositionBy(1);
            } else if (event.angleDelta.y > 0) {
                movePositionBy(-1);
            }
            event.accepted = true;
        }
    }

    // Edit mode outline (hover brightens the border)
    Rectangle {
        visible: root.editMode
        anchors.fill: parent
        radius: root.buttonRadius
        color: "transparent"
        border.width: Math.max(1, 2 * Appearance.effectiveScale)
        readonly property bool isActive: root.buttonIndex >= 0
        readonly property bool editHovered: editModeInteraction.containsMouse
        border.color: {
            if (!isActive)
                return editHovered ? Appearance.colors.colPrimary : "transparent";
            return editHovered ? Appearance.colors.colPrimary
                : Functions.ColorUtils.transparentize(Appearance.colors.colPrimary, 0.7);
        }

        Behavior on border.color { ColorAnimation { duration: 150 } }
    }

    // Resize handle — always visible in edit mode (active toggles)
    Rectangle {
        visible: root.editMode && root.buttonIndex >= 0
        z: 10
        width: 8 * Appearance.effectiveScale
        height: 24 * Appearance.effectiveScale
        radius: width / 2
        color: Appearance.colors.colPrimary
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: -width / 2

        MouseArea {
            id: rightResizeArea
            anchors.fill: parent
            anchors.margins: -12 * Appearance.effectiveScale
            cursorShape: Qt.SizeHorCursor
            preventStealing: true

            // Pointer is tracked in the host's coordinate frame because the
            // handle itself jumps when the size change relayouts the row
            property real pressRefX: 0
            property int startSize: 1
            property int appliedSize: 1

            function pointerRefX(x) {
                if (!root.keyboardHost) return x;
                return rightResizeArea.mapToItem(root.keyboardHost, x, 0).x;
            }

            onPressed: (event) => {
                pressRefX = pointerRefX(event.x);
                startSize = root.cellSize;
                appliedSize = root.cellSize;
            }
            onPositionChanged: (event) => {
                if (!pressed) return;
                const dx = pointerRefX(event.x) - pressRefX;
                const threshold = root.baseCellWidth * 0.5;
                const target = dx > threshold ? 2 : (dx < -threshold ? 1 : startSize);
                if (target !== appliedSize) {
                    root.editSetSize(target);
                    appliedSize = target;
                }
            }
        }
    }

    // Tooltip
    StyledToolTip {
        id: toggleTooltip
        extraVisibleCondition: !root.editMode && (toggleTooltip.text !== "")
        text: {
            const data = root.toggleData;
            if (!data) return "";
            if (data.tooltipText) return data.tooltipText;
            if (data.name) {
                return (data.statusText && data.statusText !== "") 
                    ? data.name + ": " + data.statusText
                    : data.name;
            }
            return "";
        }
    }
}
