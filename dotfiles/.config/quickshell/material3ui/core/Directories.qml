pragma Singleton
pragma ComponentBehavior: Bound

import "functions" as Functions
import QtCore
import QtQuick
import Quickshell

Singleton {
    // XDG Dirs (with "file://")
    readonly property string home: StandardPaths.standardLocations(StandardPaths.HomeLocation)[0]
    readonly property string documents: StandardPaths.standardLocations(StandardPaths.DocumentsLocation)[0]
    readonly property string downloads: StandardPaths.standardLocations(StandardPaths.DownloadLocation)[0]
    readonly property string music: StandardPaths.standardLocations(StandardPaths.MusicLocation)[0]
    readonly property string pictures: StandardPaths.standardLocations(StandardPaths.PicturesLocation)[0]
    readonly property string videos: StandardPaths.standardLocations(StandardPaths.MoviesLocation)[0]
    readonly property string config: StandardPaths.standardLocations(StandardPaths.ConfigLocation)[0]
    readonly property string state: StandardPaths.standardLocations(StandardPaths.StateLocation)[0]
    readonly property string cache: StandardPaths.standardLocations(StandardPaths.CacheLocation)[0]
    readonly property string genericCache: Functions.FileUtils.trimFileProtocol(`${home}/.cache`)

    // Material3UI paths (without "file://")
    property string assetsPath: Quickshell.shellPath("assets")
    property string shellConfig: Functions.FileUtils.trimFileProtocol(`${home}/.config/material3ui`)
    property string shellConfigName: "config.json"
    property string shellConfigPath: `${shellConfig}/${shellConfigName}`

    // Presets
    property string presetsPath: `${shellConfig}/presets`
    property string presetsScriptPath: `${Quickshell.shellPath("scripts")}/presets.sh`

    // Matugen colors path
    property string generatedMaterialThemePath: Functions.FileUtils.trimFileProtocol(`${state}/user/generated/colors.json`)
    property string generatedLockColorsPath: Functions.FileUtils.trimFileProtocol(`${state}/user/generated/lockscreencolors.json`)

    // Notifications cache
    property string notificationsPath: Functions.FileUtils.trimFileProtocol(`${cache}/notifications/notifications.json`)

    // Favorites cache
    property string favoritesPathRaw: genericCache + "/material3ui/favorites.json"
    property string favoritesPath: "file://" + favoritesPathRaw

    // Reminders cache
    property string remindersPath: genericCache + "/material3ui/reminders.json"

    // Screenshots
    property string screenshotTemp: "/tmp/material3ui/screenshots"
    property string screenshotDir: Functions.FileUtils.trimFileProtocol(`${pictures}/Screenshots`)

    // Ensure dirs and temp files exist (silences FileView warnings)
    Component.onCompleted: {
        Quickshell.execDetached(["mkdir", "-p", `${shellConfig}`])
        Quickshell.execDetached(["mkdir", "-p", `${presetsPath}`])
        Quickshell.execDetached(["mkdir", "-p", `${screenshotTemp}`])
        Quickshell.execDetached(["mkdir", "-p", `${Functions.FileUtils.trimFileProtocol(cache)}/material3ui`])
        
        // Ensure matugen output dir exists
        const matugenFile = generatedMaterialThemePath;
        const matugenDir = matugenFile.substring(0, matugenFile.lastIndexOf('/'));
        Quickshell.execDetached(["mkdir", "-p", matugenDir])

        // Pre-create state files for FileView watchers
        Quickshell.execDetached(["mkdir", "-p", `${Functions.FileUtils.trimFileProtocol(state)}/quickshell`])
        Quickshell.execDetached(["touch", `${Functions.FileUtils.trimFileProtocol(state)}/quickshell/material3ui_states.json`])
        Quickshell.execDetached(["touch", "/tmp/material3ui_cava.conf"])
        Quickshell.execDetached(["touch", `${Functions.FileUtils.trimFileProtocol(cache)}/material3ui/todo.json`])
        Quickshell.execDetached(["touch", `${Functions.FileUtils.trimFileProtocol(cache)}/material3ui/reminders.json`])
    }
}
