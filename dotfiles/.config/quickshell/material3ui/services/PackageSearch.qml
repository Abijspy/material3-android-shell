pragma Singleton
pragma ComponentBehavior: Bound

import QtQuick
import Quickshell
import Quickshell.Io
import "../core"

/**
 * Package search and install across the official Arch repositories
 * (`pacman -Ss`), the AUR (aurweb RPC v5, no helper needed to search) and
 * Flatpak (`flatpak search`). Installs run in the user's terminal so sudo and
 * confirmation prompts stay interactive.
 *
 * results: [{ id, name, version, description, source: "repo"|"aur"|"flatpak",
 *             repo, remote, installed, popularity }]
 */
Singleton {
    id: root

    property string query: ""
    property var repoResults: []
    property var aurResults: []
    property var flatpakResults: []
    property bool repoBusy: false
    property bool aurBusy: false
    property bool flatpakBusy: false
    property string aurError: ""
    readonly property bool busy: repoBusy || aurBusy || flatpakBusy

    property var installedNative: ({})   // pacman -Qq
    property var installedFlatpak: ({})  // flatpak list --app
    property bool flatpakAvailable: false
    property string installing: ""       // id of the package being installed

    readonly property int limit: 40
    readonly property var results: {
        const mark = r => Object.assign({}, r, {
            installed: r.source === "flatpak" ? !!root.installedFlatpak[r.id] : !!root.installedNative[r.name]
        });
        return root.repoResults.concat(root.aurResults, root.flatpakResults).map(mark);
    }

    function search(text) {
        const q = (text || "").trim();
        root.query = q;
        if (q.length < 2) {
            root.repoResults = [];
            root.aurResults = [];
            root.flatpakResults = [];
            return;
        }
        debounce.restart();
    }

    function install(result) {
        if (root.installing) return;
        let cmd;
        if (result.source === "repo") cmd = "sudo pacman -S " + result.name;
        else if (result.source === "aur") {
            if (!ArchUpdates.aurHelper) return;
            cmd = ArchUpdates.aurHelper + " -S " + result.name;
        } else cmd = "flatpak install " + (result.remote || "flathub") + " " + result.id;
        root.installing = result.id;
        installProc.command = [ArchUpdates.terminal, "-e", "sh", "-c",
            cmd + "; echo; printf 'Done. Press Enter to close…'; read _"];
        installProc.running = true;
    }

    function refreshInstalled() {
        installedProc.running = false;
        installedProc.running = true;
    }

    Component.onCompleted: refreshInstalled()

    Timer {
        id: debounce
        interval: 400
        onTriggered: {
            const q = root.query;
            root.repoBusy = true;
            repoProc.command = ["pacman", "-Ss", "--color", "never", "--", q];
            repoProc.running = false;
            repoProc.running = true;
            if (root.flatpakAvailable) {
                root.flatpakBusy = true;
                flatpakProc.command = ["flatpak", "search", "--columns=application,name,version,description,remotes", q];
                flatpakProc.running = false;
                flatpakProc.running = true;
            }
            root.searchAur(q);
        }
    }

    // ── Official repositories ──
    Process {
        id: repoProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [];
                const lines = this.text.split("\n");
                for (let i = 0; i < lines.length && out.length < root.limit; i++) {
                    // "extra/firefox 131.0-1 (group) [installed]" + indented description
                    const m = lines[i].match(/^(\S+)\/(\S+)\s+(\S+)/);
                    if (!m) continue;
                    const desc = (lines[i + 1] || "").trim();
                    out.push({ id: m[1] + "/" + m[2], name: m[2], version: m[3], description: desc, source: "repo", repo: m[1], remote: "", popularity: 0 });
                }
                // Exact and prefix matches first
                const q = root.query.toLowerCase();
                out.sort((a, b) => root.rank(a.name, q) - root.rank(b.name, q));
                root.repoResults = out;
                root.repoBusy = false;
            }
        }
        onExited: root.repoBusy = false
    }

    // ── AUR (aurweb RPC) ──
    function searchAur(q) {
        root.aurBusy = true;
        root.aurError = "";
        const xhr = new XMLHttpRequest();
        xhr.open("GET", "https://aur.archlinux.org/rpc/v5/search/" + encodeURIComponent(q) + "?by=name-desc");
        xhr.onreadystatechange = function () {
            if (xhr.readyState !== XMLHttpRequest.DONE) return;
            if (q !== root.query) return; // a newer search superseded this one
            root.aurBusy = false;
            if (xhr.status !== 200) {
                root.aurError = "AUR unreachable";
                root.aurResults = [];
                return;
            }
            try {
                const data = JSON.parse(xhr.responseText);
                if (data.type === "error") {
                    root.aurError = data.error || "AUR search failed";
                    root.aurResults = [];
                    return;
                }
                const ql = q.toLowerCase();
                const list = (data.results || [])
                    .sort((a, b) => (root.rank(a.Name, ql) - root.rank(b.Name, ql)) || (b.Popularity - a.Popularity))
                    .slice(0, root.limit)
                    .map(p => ({ id: "aur/" + p.Name, name: p.Name, version: p.Version, description: p.Description || "", source: "aur", repo: "aur", remote: "", popularity: p.Popularity, outOfDate: !!p.OutOfDate }));
                root.aurResults = list;
            } catch (e) {
                root.aurError = "AUR search failed";
                root.aurResults = [];
            }
        };
        xhr.send();
    }

    // ── Flatpak ──
    Process {
        id: flatpakProc
        stdout: StdioCollector {
            onStreamFinished: {
                const out = [];
                for (const line of this.text.split("\n")) {
                    const f = line.split("\t");
                    if (f.length < 2 || !f[0] || f[0] === "Application ID") continue;
                    out.push({ id: f[0], name: f[1] || f[0], version: f[2] || "", description: f[3] || "", source: "flatpak", repo: "flatpak", remote: (f[4] || "flathub").split(",")[0], popularity: 0 });
                    if (out.length >= root.limit) break;
                }
                root.flatpakResults = out;
                root.flatpakBusy = false;
            }
        }
        onExited: root.flatpakBusy = false
    }

    // ── Installed sets ──
    Process {
        id: installedProc
        command: ["sh", "-c", 'pacman -Qq; echo "#FLATPAK"; command -v flatpak >/dev/null 2>&1 && { echo "#HASFLATPAK"; flatpak list --app --columns=application; }; exit 0']
        stdout: StdioCollector {
            onStreamFinished: {
                const native = {};
                const flat = {};
                let section = "native";
                let hasFlatpak = false;
                for (const raw of this.text.split("\n")) {
                    const line = raw.trim();
                    if (!line) continue;
                    if (line === "#FLATPAK") { section = "flatpak"; continue; }
                    if (line === "#HASFLATPAK") { hasFlatpak = true; continue; }
                    if (section === "native") native[line] = true; else flat[line] = true;
                }
                root.installedNative = native;
                root.installedFlatpak = flat;
                root.flatpakAvailable = hasFlatpak;
            }
        }
    }

    // ── Install in a terminal ──
    Process {
        id: installProc
        onExited: {
            root.installing = "";
            root.refreshInstalled();
            ArchUpdates.check();
        }
    }

    // 0 exact, 1 prefix, 2 contains, 3 description-only
    function rank(name, q) {
        const n = (name || "").toLowerCase();
        if (n === q) return 0;
        if (n.startsWith(q)) return 1;
        if (n.includes(q)) return 2;
        return 3;
    }
}
