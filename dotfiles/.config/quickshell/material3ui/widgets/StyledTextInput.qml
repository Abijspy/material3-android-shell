pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import "../core"

Item {
    id: root

    property alias text: input.text
    property string placeholder: ""
    property color placeholderColor: Appearance.m3colors.m3onSurfaceVariant
    property alias font: input.font
    property alias color: input.color
    property alias echoMode: input.echoMode
    property alias input: input
    property alias readOnly: input.readOnly
    property alias selectByMouse: input.selectByMouse
    property alias horizontalAlignment: input.horizontalAlignment
    property alias inputMask: input.inputMask

    property real inputRadius: 12
    property color backgroundColor: Appearance.m3colors.m3surfaceContainerLow
    property real borderInactiveWidth: 0
    property bool showActiveBorder: true
    property color borderInactiveColor: "transparent"
    property real leftMargin: 16
    property real rightMargin: 16
    property bool yieldArrows: false

    signal editingFinished()
    signal accepted()
    signal arrowPressed(int key)

    implicitWidth: 200 * Appearance.effectiveScale
    implicitHeight: 48 * Appearance.effectiveScale

    onFocusChanged: {
        if (focus)
            input.forceActiveFocus()
    }

    Component.onCompleted: {
        if (focus)
            input.forceActiveFocus()
    }

    Rectangle {
        id: bg
        anchors.fill: parent
        radius: root.inputRadius * Appearance.effectiveScale
        color: root.backgroundColor
        border.width: input.activeFocus && root.showActiveBorder ? Math.max(1, 2 * Appearance.effectiveScale) : root.borderInactiveWidth * Appearance.effectiveScale
        border.color: input.activeFocus && root.showActiveBorder ? Appearance.colors.colPrimary : root.borderInactiveColor
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: root.leftMargin * Appearance.effectiveScale
        anchors.rightMargin: root.rightMargin * Appearance.effectiveScale
        verticalAlignment: TextInput.AlignVCenter
        font.family: Appearance.font.family.main
        font.pixelSize: Appearance.font.pixelSize.normal
        color: Appearance.colors.colOnLayer1
        selectionColor: Appearance.colors.colPrimaryContainer
        selectedTextColor: Appearance.colors.colOnPrimaryContainer
        clip: true

        HoverHandler {
            cursorShape: Qt.IBeamCursor
        }

        onEditingFinished: root.editingFinished()
        onAccepted: root.accepted()

        Keys.onPressed: (event) => {
            if (root.yieldArrows && (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
                root.arrowPressed(event.key);
                event.accepted = true;
            }
        }

        StyledText {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: input.horizontalAlignment
            text: root.placeholder
            color: root.placeholderColor
            font: input.font
            opacity: 0.6
            visible: input.text === "" && !input.activeFocus
        }

        onActiveFocusChanged: {
            if (activeFocus)
                focusGuard.open()
            else
                focusGuard.close()
        }
    }

    Popup {
        id: focusGuard
        x: 0
        y: 0
        width: root.width
        height: root.height
        padding: 0
        margins: 0
        modal: false
        closePolicy: Popup.CloseOnPressOutside

        background: Item {}

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            property int dragStart: -1
            onPressed: (event) => {
                const pos = input.mapFromItem(focusGuard.contentItem, event.x, event.y)
                input.cursorPosition = input.positionAt(pos.x, pos.y)
                dragStart = input.cursorPosition
                input.select(dragStart, dragStart)
                input.forceActiveFocus()
            }
            onPositionChanged: (event) => {
                if (!pressed || dragStart < 0) return
                const pos = input.mapFromItem(focusGuard.contentItem, event.x, event.y)
                input.cursorPosition = input.positionAt(pos.x, pos.y)
                input.select(dragStart, input.cursorPosition)
            }
            onReleased: dragStart = -1
        }

        onClosed: input.focus = false
    }
}
