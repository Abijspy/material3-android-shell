pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quickshell
import "core"
import "components"

/**
 * One screen of the greeter, laid out exactly like the Material3UI lock
 * screen (panels/Lock/LockSurface.qml): wallpaper, centred digital clock and
 * date, and the bottom password island (leading icon · field · arrow button)
 * with the same sizes, colours, placeholder text, shake and entrance motion.
 * Greeter-only additions sit where the lock screen shows its media card
 * (user chip above the island) and status bar (power pill).
 */
Item {
    id: root

    property bool primary: true

    // ── Entrance motion: identical to LockSurface ──
    property real islandOpacity: 0
    property real islandScale: 0.95
    property real islandYOffset: 30 * Theme.s
    Behavior on islandOpacity { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
    Behavior on islandScale { NumberAnimation { duration: 400; easing.type: Easing.OutBack; easing.overshoot: 0.8 } }
    Behavior on islandYOffset { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
    Component.onCompleted: {
        islandOpacity = 1;
        islandScale = 1;
        islandYOffset = 0;
    }

    // ── Wallpaper ──
    Rectangle {
        anchors.fill: parent
        color: Theme.surfaceContainerLowest
    }

    Image {
        anchors.fill: parent
        source: Theme.wallpaper ? "file://" + Theme.wallpaper : ""
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(root.width, root.height)
        asynchronous: true
        opacity: GreeterState.launching ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 400 } }
    }

    // ── Clock (DigitalClock with isLockscreen: true) ──
    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 0
        opacity: GreeterState.launching ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 400 } }

        readonly property string timeString: {
            let t = Qt.formatTime(clock.date, Theme.timeFormat);
            if (Theme.clock.hideAmPm) t = t.replace(/ ?[ap]m$/i, "");
            return t.trim();
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: Theme.clock.isVertical ? Qt.formatTime(clock.date, Theme.timeFormat.startsWith("H") ? "HH" : "hh") : parent.timeString
            color: Theme.clockColor
            font.pixelSize: Math.round(Theme.clock.fontSize)
            font.weight: Font.Bold
            font.family: Theme.clock.timeFont
            renderType: Text.NativeRendering
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: -24 * Theme.s
            visible: Theme.clock.isVertical
            text: Qt.formatTime(clock.date, "mm")
            color: Theme.clockColor
            font.pixelSize: Math.round(Theme.clock.fontSize)
            font.weight: Font.Bold
            font.family: Theme.clock.timeFont
            renderType: Text.NativeRendering
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.clock.dateGap * Theme.s
            visible: Theme.clock.showDate
            text: Qt.formatDate(clock.date, Theme.dateFormat)
            color: Theme.dateColor
            font.pixelSize: Math.round(Theme.clock.dateFontSize * Theme.s)
            font.weight: Font.Medium
            font.family: Theme.clock.dateFont
            renderType: Text.NativeRendering
        }
    }

    // ── Power (lock screen PowerBtn style) ──
    Pill {
        visible: root.primary
        anchors { top: parent.top; right: parent.right; margins: 16 * Theme.s }
        opacity: root.islandOpacity
        scale: root.islandScale

        PowerButton { icon: "bedtime"; onClicked: GreeterState.power("suspend") }
        PowerButton { icon: "restart_alt"; onClicked: GreeterState.power("reboot") }
        PowerButton { icon: "power_settings_new"; onClicked: GreeterState.power("poweroff") }
    }

    // ── Preview banner ──
    Pill {
        visible: root.primary && GreeterState.preview
        anchors { top: parent.top; left: parent.left; margins: 16 * Theme.s }
        padding: 14 * Theme.s
        opacity: root.islandOpacity
        Icon { text: "visibility"; color: Theme.tertiary }
        Label {
            text: "Preview: greetd is not running. Type \"fail\" to test an error, anything else to close."
            font.pixelSize: 13 * Theme.s
            color: Theme.onSurfaceVariant
        }
    }

    // ── User chip (where the lock screen shows its media card) ──
    Pill {
        id: userCard
        visible: root.primary
        anchors.bottom: bottomIsland.top
        anchors.bottomMargin: 24 * Theme.s
        anchors.horizontalCenter: parent.horizontalCenter
        padding: 8 * Theme.s
        spacing: 12 * Theme.s
        scale: root.islandScale
        opacity: GreeterState.launching ? 0 : root.islandOpacity
        transform: Translate { y: root.islandYOffset }

        Avatar {
            username: GreeterState.user.name
            displayName: GreeterState.user.displayName
            size: 40 * Theme.s
        }
        Label {
            Layout.rightMargin: GreeterState.users.length > 1 ? 0 : 12 * Theme.s
            text: GreeterState.user.displayName || "No users found"
            font.pixelSize: 16 * Theme.s
            font.weight: Font.Medium
        }
        PowerButton {
            visible: GreeterState.users.length > 1
            icon: menus.which === "user" ? "expand_less" : "expand_more"
            onClicked: menus.toggle("user")
        }
    }

    // ── Bottom island: identical to LockSurface's password pill ──
    Pill {
        id: bottomIsland
        visible: root.primary
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: 32 * Theme.s
        }
        implicitWidth: Math.min(400 * Theme.s, parent.width * 0.9)
        scale: root.islandScale
        opacity: GreeterState.launching ? 0 : root.islandOpacity
        transform: Translate { y: root.islandYOffset }

        // Leading icon: the session picker (the lock screen puts its fingerprint here)
        PowerButton {
            Layout.leftMargin: 2 * Theme.s
            icon: "desktop_windows"
            active: menus.which === "session"
            onClicked: menus.toggle("session")
        }

        Rectangle {
            id: inputWrapper
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceContainerLow
            radius: height / 2

            TextInput {
                id: passwordInput
                anchors.fill: parent
                verticalAlignment: TextInput.AlignVCenter
                font.pixelSize: 15 * Theme.s
                color: "transparent"
                selectionColor: "transparent"
                cursorVisible: false
                cursorDelegate: Item {}
                inputMethodHints: Qt.ImhSensitiveData
                echoMode: TextInput.Normal
                clip: true
                padding: 12 * Theme.s
                focus: root.primary
                enabled: !GreeterState.busy && !GreeterState.launching
                text: GreeterState.password
                onTextChanged: {
                    if (GreeterState.password !== text) GreeterState.password = text;
                    if (text.length > 0 && GreeterState.messageIsError) GreeterState.message = "";
                }
                onAccepted: GreeterState.submit()
                Keys.onEscapePressed: { menus.which = ""; GreeterState.cancel(); }
                Keys.onUpPressed: GreeterState.selectUser(GreeterState.userIndex - 1)
                Keys.onDownPressed: GreeterState.selectUser(GreeterState.userIndex + 1)

                PasswordDots {
                    anchors.fill: parent
                    visible: !GreeterState.awaitingPrompt
                    active: passwordInput.activeFocus && !GreeterState.busy
                    length: GreeterState.password.length
                    charSize: 18 * Theme.s
                }

                // A non-secret follow-up prompt (e.g. OTP) shows its text
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 14 * Theme.s
                    verticalAlignment: Text.AlignVCenter
                    visible: GreeterState.awaitingPrompt && GreeterState.password.length > 0
                    text: GreeterState.password
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 24 * Theme.s
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    visible: passwordInput.text.length === 0
                    text: GreeterState.message || (GreeterState.busy ? "Signing in…" : "Enter password")
                    font.pixelSize: 15 * Theme.s
                    font.family: Theme.fontMain
                    color: GreeterState.messageIsError ? Theme.error : Theme.onSurfaceVariant
                }
            }

            // Shake (same keyframes as the lock screen)
            SequentialAnimation {
                id: shakeAnim
                NumberAnimation { target: inputWrapper; property: "Layout.leftMargin"; to: -10 * Theme.s; duration: 50 }
                NumberAnimation { target: inputWrapper; property: "Layout.leftMargin"; to: 10 * Theme.s; duration: 50 }
                NumberAnimation { target: inputWrapper; property: "Layout.leftMargin"; to: -5 * Theme.s; duration: 50 }
                NumberAnimation { target: inputWrapper; property: "Layout.leftMargin"; to: 5 * Theme.s; duration: 50 }
                NumberAnimation { target: inputWrapper; property: "Layout.leftMargin"; to: 0; duration: 50 }
            }
            Connections {
                target: GreeterState
                function onFailed() { shakeAnim.restart(); passwordInput.forceActiveFocus(); }
            }
        }

        // Main action (unlock arrow)
        IconButton {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 64 * Theme.s
            implicitHeight: 40 * Theme.s
            iconSize: 22 * Theme.s
            icon: GreeterState.busy || GreeterState.launching ? "progress_activity" : "arrow_right_alt"
            spinning: GreeterState.busy || GreeterState.launching
            background: GreeterState.busy ? Theme.surfaceContainerHigh : Theme.primary
            foreground: GreeterState.busy ? Theme.onSurfaceVariant : Theme.onPrimary
            enabled: !GreeterState.busy
            onClicked: GreeterState.submit()
        }
    }

    // ── Menus (open above the user chip) ──
    QtObject {
        id: menus
        property string which: ""
        function toggle(name) { which = which === name ? "" : name; }
    }

    Menu {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: userCard.top
        anchors.bottomMargin: 12 * Theme.s
        open: menus.which === "user"
        model: GreeterState.users.map(u => ({ label: u.displayName, sublabel: u.name !== u.displayName ? u.name : "", icon: "account_circle" }))
        currentIndex: GreeterState.userIndex
        onPicked: index => { GreeterState.selectUser(index); menus.which = ""; passwordInput.forceActiveFocus(); }
    }

    Menu {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: userCard.top
        anchors.bottomMargin: 12 * Theme.s
        open: menus.which === "session"
        model: GreeterState.sessions.map(s => ({ label: s.name, sublabel: s.exec, icon: "desktop_windows" }))
        currentIndex: GreeterState.sessionIndex
        onPicked: index => { GreeterState.sessionIndex = index; menus.which = ""; passwordInput.forceActiveFocus(); }
    }

    // Lock screen PowerBtn: 40px round, primary when active
    component PowerButton: IconButton {
        property bool active: false
        Layout.alignment: Qt.AlignVCenter
        background: active ? Theme.primary : "transparent"
        foreground: active ? Theme.onPrimary : Theme.onSurfaceVariant
    }
}
