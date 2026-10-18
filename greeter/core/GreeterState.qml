pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import Quickshell.Services.Greetd

/**
 * Users, sessions and the greetd authentication flow.
 *
 * Flow: submit() → Greetd.createSession(user) → authMessage (respond with the
 * password; info prompts such as fingerprint get an empty response) →
 * readyToLaunch → Greetd.launch(session). Without $GREETD_SOCK the greeter
 * runs in preview mode and only simulates authentication.
 */
Singleton {
    id: root

    readonly property bool preview: !Quickshell.env("GREETD_SOCK")
    readonly property string statePath: Theme.dataDir + "/state.json"

    // Users: [{ name, displayName }]
    property var users: []
    property int userIndex: 0
    readonly property var user: users[userIndex] ?? ({ name: "", displayName: "" })

    // Sessions: [{ path, name, exec, desktopNames }]
    property var sessions: []
    property int sessionIndex: 0
    readonly property var session: sessions[sessionIndex] ?? ({ path: "", name: "Hyprland", exec: "Hyprland", desktopNames: "Hyprland" })

    // UI-facing auth state
    property string password: ""
    property bool busy: false
    property bool launching: false
    property string message: ""        // prompt or error shown in the field
    property bool messageIsError: false
    property bool awaitingPrompt: false  // a second required prompt (e.g. OTP)
    property bool sentPassword: false
    signal failed()

    property string lastUser: ""
    property string lastSession: ""

    // ── Actions ──
    function submit() {
        if (root.busy || root.launching || !root.user.name) return;
        root.messageIsError = false;

        if (root.awaitingPrompt) {
            root.awaitingPrompt = false;
            root.busy = true;
            Greetd.respond(root.password);
            root.password = "";
            return;
        }

        root.busy = true;
        root.message = "";
        if (root.preview) {
            previewTimer.restart();
            return;
        }
        if (Greetd.state !== GreetdState.Inactive) Greetd.cancelSession();
        root.sentPassword = false;
        Greetd.createSession(root.user.name);
    }

    function cancel() {
        if (!root.preview && Greetd.state !== GreetdState.Inactive) Greetd.cancelSession();
        root.busy = false;
        root.awaitingPrompt = false;
        root.password = "";
        root.message = "";
    }

    function selectUser(i) {
        if (i < 0 || i >= root.users.length || i === root.userIndex) return;
        root.cancel();
        root.userIndex = i;
    }

    function fail(text) {
        root.busy = false;
        root.awaitingPrompt = false;
        root.password = "";
        root.message = text;
        root.messageIsError = true;
        root.failed();
        if (!root.preview && Greetd.state !== GreetdState.Inactive) Greetd.cancelSession();
    }

    function launchSession() {
        root.launching = true;
        root.saveState();
        const s = root.session;
        const desktop = s.desktopNames || s.name;
        const env = [
            "XDG_SESSION_TYPE=wayland",
            "XDG_CURRENT_DESKTOP=" + desktop,
            "XDG_SESSION_DESKTOP=" + desktop.split(":")[0],
        ];
        // exec keeps the session as the direct child of greetd
        Greetd.launch(["sh", "-c", "exec " + s.exec], env);
    }

    function power(action) {
        Quickshell.execDetached(["systemctl", action]);
    }

    function saveState() {
        const json = JSON.stringify({ lastUser: root.user.name, lastSession: root.session.path });
        Quickshell.execDetached(["sh", "-c", 'printf "%s\\n" "$1" > "$2"', "sh", json, root.statePath]);
    }

    // ── greetd ──
    Connections {
        target: Greetd
        enabled: !root.preview

        function onAuthMessage(message, error, responseRequired, echoResponse) {
            if (!responseRequired) {
                // Informational prompt (fingerprint, "Place your finger"...)
                if (message) root.message = message;
                Greetd.respond("");
                return;
            }
            if (!root.sentPassword) {
                root.sentPassword = true;
                Greetd.respond(root.password);
                root.password = "";
                return;
            }
            // Another secret is needed: hand the field back to the user
            root.busy = false;
            root.awaitingPrompt = true;
            root.message = message || "Additional verification required";
            root.messageIsError = error;
        }

        function onAuthFailure(message) {
            root.fail(message && !/authentication failure/i.test(message) ? message : "Incorrect password");
        }

        function onError(error) {
            root.fail(error || "Login failed");
        }

        function onReadyToLaunch() {
            root.busy = false;
            root.message = "";
            root.launchSession();
        }
    }

    Timer {
        id: previewTimer
        interval: 700
        onTriggered: {
            if (root.password === "fail") {
                root.fail("Incorrect password");
            } else {
                root.busy = false;
                root.launching = true;
                root.saveState();
                quitTimer.restart();
            }
        }
    }

    Timer {
        id: quitTimer
        interval: 600
        onTriggered: Qt.quit()
    }

    // ── Discovery ──
    FileView {
        path: "/etc/passwd"
        onLoaded: {
            const list = [];
            for (const line of text().split("\n")) {
                const f = line.split(":");
                if (f.length < 7) continue;
                const uid = parseInt(f[2]);
                if (uid < 1000 || uid >= 60000 || /(nologin|false)$/.test(f[6])) continue;
                const gecos = (f[4] || "").split(",")[0].trim();
                list.push({ name: f[0], displayName: gecos || f[0] });
            }
            root.users = list;
            root.restoreSelection();
        }
    }

    Process {
        running: true
        command: ["sh", "-c", `
            for d in /usr/local/share/wayland-sessions /usr/share/wayland-sessions; do
                for f in "$d"/*.desktop; do
                    [ -f "$f" ] || continue
                    grep -qiE '^(Hidden|NoDisplay)=true' "$f" && continue
                    n=$(sed -n 's/^Name=//p' "$f" | head -n1)
                    e=$(sed -n 's/^Exec=//p' "$f" | head -n1 | sed 's/ *%[a-zA-Z]//g')
                    k=$(sed -n 's/^DesktopNames=//p' "$f" | head -n1 | tr ';' ':' | sed 's/:$//')
                    [ -n "$e" ] && printf '%s\\t%s\\t%s\\t%s\\n' "$f" "\${n:-$e}" "$e" "$k"
                done
            done`]
        stdout: StdioCollector {
            onStreamFinished: {
                const list = [];
                const seen = {};
                for (const line of this.text.split("\n")) {
                    const p = line.split("\t");
                    if (p.length < 3 || seen[p[1]]) continue;
                    seen[p[1]] = true;
                    list.push({ path: p[0], name: p[1], exec: p[2], desktopNames: p[3] || p[1] });
                }
                if (list.length > 0) root.sessions = list;
                root.restoreSelection();
            }
        }
    }

    FileView {
        path: root.statePath
        onLoaded: {
            try {
                const json = JSON.parse(text());
                root.lastUser = json.lastUser || "";
                root.lastSession = json.lastSession || "";
            } catch (e) {}
            root.restoreSelection();
        }
    }

    function restoreSelection() {
        const u = root.users.findIndex(x => x.name === root.lastUser);
        if (u >= 0) root.userIndex = u;
        let s = root.sessions.findIndex(x => x.path === root.lastSession);
        if (s < 0) s = root.sessions.findIndex(x => /hyprland/i.test(x.name) && !/uwsm|debug/i.test(x.name));
        if (s >= 0) root.sessionIndex = s;
    }
}
