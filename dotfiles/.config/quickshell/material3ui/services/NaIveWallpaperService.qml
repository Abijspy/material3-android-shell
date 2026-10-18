pragma Singleton
import QtQuick
import Quickshell
import Quickshell.Io
import "../core"
import "../core/functions" as Functions

/**
 * Service for interacting with na-ive wallpaper collection.
 */
Singleton {
    id: root

    property string wallpaperDir: Functions.FileUtils.trimFileProtocol(Directories.pictures) + "/Wallpapers"
    readonly property alias results: naiveModel
    property bool loading: false
    property string errorMessage: ""
    
    readonly property string baseUrl: "https://na-ive.github.io/wallpapers/"
    readonly property string rawBaseUrl: "https://raw.githubusercontent.com/na-ive/wallpapers/main/"
    readonly property string jsonUrl: "https://raw.githubusercontent.com/na-ive/wallpapers/gh-pages/wallpapers.json"

    ListModel {
        id: naiveModel
    }

    signal fetchFinished()

    function fetch() {
        if (naiveModel.count > 0 && !root.errorMessage) return; // Cache results
        
        root.loading = true;
        root.errorMessage = "";
        naiveModel.clear();

        const xhr = new XMLHttpRequest();
        xhr.open("GET", root.jsonUrl);
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                root.loading = false;
                if (xhr.status === 200) {
                    try {
                        const response = JSON.parse(xhr.responseText);
                        if (Array.isArray(response)) {
                            // Sort by mtime descending (newest first)
                            response.sort((a, b) => new Date(b.mtime) - new Date(a.mtime));
                            
                            const newItems = response.map(item => ({
                                "id": item.wallhaven_id || item.filename.split('.')[0],
                                "wallhaven_id": item.wallhaven_id || "",
                                "filename": item.filename,
                                "preview": root.baseUrl + item.thumbnail,
                                "full": root.rawBaseUrl + item.filename,
                                "color": item.color || "#000000",
                                "is_naive": true
                            }));
                            
                            for (let i = 0; i < newItems.length; i++) {
                                naiveModel.append(newItems[i]);
                            }
                        }
                    } catch (e) {
                        console.error("[Na-ive] Parse error:", e);
                        root.errorMessage = "Failed to parse wallpaper list";
                    }
                } else {
                    root.errorMessage = "Server error (" + xhr.status + ")";
                }
                root.fetchFinished();
            }
        };
        xhr.onerror = function() {
            root.loading = false;
            root.errorMessage = "Network error. Check connection.";
            root.fetchFinished();
        };
        xhr.send();
    }

    function download(url, filename, apply = false) {
        const fullPath = root.wallpaperDir + "/" + filename;

        const applyWallpaper = () => {
            if (GlobalStates.wallpaperSelectorTarget === "desktop") {
                Wallpapers.select("file://" + fullPath);
            } else {
                Wallpapers.selectForLockscreen("file://" + fullPath);
            }
        };

        Quickshell.execDetached(["mkdir", "-p", root.wallpaperDir]);

        // Check if file exists
        const checkProc = createProcess.createObject(null, {
            command: ["sh", "-c", 'if [ -f "$1" ]; then exit 0; else exit 1; fi', "sh", fullPath]
        });

        checkProc.exited.connect((exitCode) => {
            if (exitCode === 0) {
                if (apply) {
                    applyWallpaper();
                    SnackbarService.show("Already existed. Wallpaper applied!");
                } else {
                    SnackbarService.show("Already downloaded: " + filename);
                }
                checkProc.destroy();
            } else {
                checkProc.destroy();
                const p = createProcess.createObject(null, {
                    command: ["sh", "-c", 'curl -L "$1" -o "$2"', "sh", url, fullPath]
                });
                p.exited.connect((code) => {
                    if (code !== 0) {
                        SnackbarService.show("Download failed.");
                    } else if (apply) {
                        applyWallpaper();
                        SnackbarService.show("Wallpaper applied successfully!");
                    } else {
                        SnackbarService.show("Downloaded: " + filename, "Set as wallpaper", applyWallpaper);
                    }
                    p.destroy();
                });
                p.running = true;
            }
        });
        checkProc.running = true;
    }

    Component {
        id: createProcess
        Process {}
    }
}
