#pragma once

#include <string>

// XDG locations shared with the QML shell, so both read the same config,
// Matugen colours and state while the port is in progress.
namespace m3::paths {

std::string home();
std::string configDir();   // ~/.config/material3ui
std::string configFile();  // ~/.config/material3ui/config.json
std::string colorsFile();  // Matugen output: ~/.local/state/quickshell/user/generated/colors.json
std::string runtimeDir();  // $XDG_RUNTIME_DIR/material3ui
std::string ipcSocket();   // $XDG_RUNTIME_DIR/material3ui/ipc.sock
std::string stateDir();    // ~/.local/state/material3ui
// A file shipped in the data directory (emojis.txt…): installed copy, the
// source tree next to the build, or the QML shell's copy
std::string dataFile(const std::string& name);

} // namespace m3::paths
