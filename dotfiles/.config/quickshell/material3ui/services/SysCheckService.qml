pragma Singleton

import "../core"
import QtQuick
import Quickshell
import Quickshell.Io

/**
 * SysCheckService.qml
 * Centralized dependency management service.
 * Driven by data/dependencies.json.
 */
Singleton {
    id: root

    property int missingCount: 0
    property bool isReady: missingCount === 0
    property bool isChecking: false
    
    // The master list of dependencies with their current status
    // [{name, description, installed, category}]
    property var dependencyData: []

    function check() {
        if (isChecking) return;
        isChecking = true;
        checkProcess.running = true;
    }

    function cancel() {
        checkProcess.running = false;
        isChecking = false;
    }

    Component.onCompleted: check()

    Process {
        id: checkProcess
        command: [
            "bash", "-c", 
            "HOME_PATH=\"" + Directories.home.toString().replace('file://', '') + "\"; " +
            "JSON_FILE=\"$HOME_PATH/.config/quickshell/material3ui/data/dependencies.json\"; " +
            "if ! command -v jq >/dev/null 2>&1; then echo 'ERROR: jq not found'; exit 1; fi; " +
            "if [ ! -f \"$JSON_FILE\" ]; then echo 'ERROR: File not found'; exit 1; fi; " +
            "categories=(\"core\" \"services\" \"utilities\" \"theming\" \"fonts\" \"livewallpaper\" \"optional\"); " +
            "for cat in \"${categories[@]}\"; do " +
            "  jq -c \".$cat[]\" \"$JSON_FILE\" | while read -r item; do " +
            "    name=$(echo \"$item\" | jq -r '.name'); " +
            "    cmd=$(echo \"$item\" | jq -r '.command'); " +
            "    desc=$(echo \"$item\" | jq -r '.description // \"\"'); " +
            "    dep=$(echo \"$item\" | jq -r '.deprecated // false'); " +
            "    replacement=$(echo \"$item\" | jq -r '.replacement // \"\"'); " +
            "    installed=false; " +
            "    if command -v \"$cmd\" >/dev/null 2>&1 || [ -f \"$cmd\" ]; then installed=true; fi; " +
            "    echo \"$name|$desc|$installed|$cat|$dep|$replacement\"; " +
            "  done; " +
            "done"
        ]

        stdout: StdioCollector {
            onStreamFinished: {
                if (this.text.includes("ERROR")) {
                    console.error("[SysCheck] Error: " + this.text.trim());
                    root.isChecking = false;
                    return;
                }
                
                const lines = this.text.split("\n").filter(line => line.trim() !== "");
                let newData = [];
                let missing = 0;
                
                lines.forEach(line => {
                    const parts = line.split("|");
                    if (parts.length < 4) return;
                    
                    const isInstalled = parts[2] === "true";
                    const category = parts[3];
                    const isDeprecated = parts.length > 4 && parts[4] === "true";
                    const replacement = parts.length > 5 ? parts[5] : "";
                    
                    // Only count missing in core, services, and fonts as critical
                    if (!isInstalled && (category === "core" || category === "services" || category === "fonts")) {
                        missing++;
                    }
                    
                    newData.push({
                        name: parts[0],
                        description: parts[1],
                        installed: isInstalled,
                        category: category,
                        deprecated: isDeprecated,
                        replacement: replacement
                    });
                });
                
                root.dependencyData = newData;
                root.missingCount = missing;
                root.isChecking = false;
            }
        }
    }
}
