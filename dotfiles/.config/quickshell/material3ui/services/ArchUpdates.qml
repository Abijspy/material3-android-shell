pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

/**
 * Arch Linux package updates.
 *
 * Official repositories are checked with `checkupdates` (pacman-contrib),
 * which syncs a temporary copy of the databases and never touches pacman's
 * real ones. AUR updates come from `paru -Qua` / `yay -Qua`. Upgrading opens
 * the user's terminal so pacman/sudo prompts stay interactive, then rechecks.
 * Flatpak app updates come from `flatpak remote-ls --updates`.
 */
Singleton {
    id: root

    // [{ name, from, to, source: "repo" | "aur" | "flatpak" }]
    property var packages: []
    readonly property int count: packages.length
    readonly property int repoCount: packages.filter(p => p.source === "repo").length
    readonly property int aurCount: packages.filter(p => p.source === "aur").length
    readonly property int flatpakCount: packages.filter(p => p.source === "flatpak").length
    property bool flatpakAvailable: false

    property bool checking: false
    property bool upgrading: false
    property bool checkupdatesAvailable: true
    property string aurHelper: ""
    property string error: ""
    property real lastChecked: 0

    readonly property var cfg: Config.ready ? Config.options.archUpdates : null
    readonly property bool autoCheck: cfg ? cfg.autoCheck : true
    readonly property int intervalMinutes: cfg ? Math.max(15, cfg.intervalMinutes) : 60
    readonly property bool includeAur: cfg ? cfg.includeAur : true
    readonly property bool includeFlatpak: cfg ? cfg.includeFlatpak : true
    readonly property bool notify: cfg ? cfg.notify : true
    readonly property string terminal: Quickshell.env("TERMINAL") || "alacritty"

    readonly property string statusText: {
        if (!checkupdatesAvailable) return "Install pacman-contrib to check for updates";
        if (checking) return "Checking for updates…";
        if (error) return error;
        if (lastChecked === 0) return "Not checked yet";
        if (count === 0) return "Your system is up to date";
        const parts = [];
        if (repoCount > 0) parts.push(repoCount + " official");
        if (aurCount > 0) parts.push(aurCount + " AUR");
        if (flatpakCount > 0) parts.push(flatpakCount + " Flatpak");
        return count + (count === 1 ? " update available" : " updates available") + " (" + parts.join(", ") + ")";
    }

    function check() {
        if (root.checking || root.upgrading) return;
        root.checking = true;
        root.error = "";
        checkProc.running = true;
    }

    function upgrade() {
        if (root.upgrading) return;
        root.upgrading = true;
        upgradeProc.running = true;
    }

    // ── Check ──
    Process {
        id: checkProc
        command: ["sh", "-c", `
            if command -v checkupdates >/dev/null 2>&1; then
                checkupdates --nocolor 2>/dev/null
                [ $? -eq 1 ] && echo "#ERROR"
            else
                echo "#MISSING"
            fi
            echo "#AUR"
            for h in paru yay; do
                if command -v "$h" >/dev/null 2>&1 && "$h" --version >/dev/null 2>&1; then
                    echo "#HELPER $h"
                    [ "$1" = 1 ] && "$h" -Qua --color never 2>/dev/null
                    break
                fi
            done
            echo "#FLATPAK"
            if command -v flatpak >/dev/null 2>&1; then
                echo "#HASFLATPAK"
                [ "$2" = 1 ] && flatpak remote-ls --updates --app --columns=application,version 2>/dev/null
            fi
            exit 0`, "sh", root.includeAur ? "1" : "0", root.includeFlatpak ? "1" : "0"]
        stdout: StdioCollector {
            onStreamFinished: {
                const list = [];
                let source = "repo";
                let failed = false;
                root.checkupdatesAvailable = true;
                root.flatpakAvailable = false;
                for (const raw of this.text.split("\n")) {
                    const line = raw.trim();
                    if (!line) continue;
                    if (line === "#MISSING") { root.checkupdatesAvailable = false; continue; }
                    if (line === "#ERROR") { failed = true; continue; }
                    if (line === "#AUR") { source = "aur"; continue; }
                    if (line === "#FLATPAK") { source = "flatpak"; continue; }
                    if (line === "#HASFLATPAK") { root.flatpakAvailable = true; continue; }
                    if (source === "flatpak") {
                        const f = raw.split("\t");
                        list.push({ name: f[0].trim(), from: "", to: (f[1] || "").trim() || "latest", source: "flatpak", ignored: false });
                        continue;
                    }
                    if (line.startsWith("#HELPER ")) { root.aurHelper = line.slice(8); continue; }
                    // "name old -> new" (paru may append "[ignored]")
                    const m = line.match(/^(\S+)\s+(\S+)\s+->\s+(\S+)/);
                    if (m) list.push({ name: m[1], from: m[2], to: m[3], source: source, ignored: /\[ignored\]/.test(line) });
                }
                const previous = root.count;
                root.packages = list.filter(p => !p.ignored);
                root.error = failed && list.length === 0 ? "Could not reach the package mirrors" : "";
                root.lastChecked = Date.now();
                root.checking = false;
                if (root.notify && root.count > previous && root.count > 0) root.sendNotification();
            }
        }
        onExited: if (root.checking) root.checking = false
    }

    // ── Upgrade in a terminal ──
    Process {
        id: upgradeProc
        command: [root.terminal, "-e", "sh", "-c",
            (root.aurHelper ? root.aurHelper + " -Syu" : "sudo pacman -Syu")
            + (root.flatpakAvailable && root.includeFlatpak ? "; echo; flatpak update" : "")
            + "; echo; printf 'Done. Press Enter to close…'; read _"]
        onExited: {
            root.upgrading = false;
            root.check();
        }
    }

    function sendNotification() {
        Quickshell.execDetached(["notify-send", "-a", "Material3UI",
            "-i", Quickshell.shellPath("assets/icons/Material3UI.svg"),
            root.count === 1 ? "1 system update available" : root.count + " system updates available",
            root.statusText]);
    }

    // ── Scheduling ──
    Timer {
        // First check shortly after login, once the network is up
        interval: 90 * 1000
        running: root.autoCheck
        onTriggered: root.check()
    }

    Timer {
        interval: root.intervalMinutes * 60 * 1000
        running: root.autoCheck
        repeat: true
        onTriggered: root.check()
    }
}
