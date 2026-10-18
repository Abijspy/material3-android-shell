#pragma once

#include <string>

// XDG locations: config, Matugen colours, state and runtime files.
namespace m3::paths {

std::string home();
std::string configDir();   // ~/.config/material3ui
std::string configFile();  // ~/.config/material3ui/config.json
std::string colorsFile();  // Matugen output: ~/.local/state/quickshell/user/generated/colors.json
std::string runtimeDir();  // $XDG_RUNTIME_DIR/material3ui
std::string ipcSocket();   // $XDG_RUNTIME_DIR/material3ui/ipc.sock
std::string stateDir();    // ~/.local/state/material3ui
// A file shipped in the data directory (emojis.txt, icons, wallpapers):
// the installed copy, or shell/data when running from shell/build
std::string dataFile(const std::string& name);

} // namespace m3::paths
