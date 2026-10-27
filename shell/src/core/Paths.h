#pragma once

#include <string>
#include <vector>

// XDG locations: config, Matugen colours, state and runtime files.
namespace m3::paths {

std::string home();
std::string configDir();   // ~/.config/material3ui
std::string configFile();  // ~/.config/material3ui/config.json
std::string colorsFile();  // Matugen output: ~/.local/state/quickshell/user/generated/colors.json
std::string runtimeDir();  // $XDG_RUNTIME_DIR/material3ui (-<instance>)
std::string ipcSocket();   // $XDG_RUNTIME_DIR/material3ui/ipc.sock
// M3_INSTANCE: a second shell beside the one running (on a nested
// compositor, for testing), with its own runtime folder and socket, and
// none of what there's one of in a session (Quick Share, the polkit agent,
// hooks, the clipboard's watchers). "" for the shell itself; `material3ui
// msg` with it set talks to that one
std::string instance();
std::string stateDir();    // ~/.local/state/material3ui
// A file shipped in the data directory (emojis.txt, icons, wallpapers):
// the installed copy, or shell/data when running from shell/build
std::string dataFile(const std::string& name);
// $XDG_DATA_DIRS in order, with Flatpak's exports in front when the session
// was started without its profile script adding them
std::vector<std::string> dataDirs();

} // namespace m3::paths
