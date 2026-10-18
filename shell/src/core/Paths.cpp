#include "core/Paths.h"

#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <vector>

namespace m3::paths {

namespace {

std::string env(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

} // namespace

std::string home() {
    return env("HOME", "/tmp");
}

std::string configDir() {
    return env("XDG_CONFIG_HOME", home() + "/.config") + "/material3ui";
}

std::string configFile() {
    // M3_CONFIG_FILE: the greeter reads the lock screen settings synced for it
    const std::string forced = env("M3_CONFIG_FILE", "");
    return forced.empty() ? configDir() + "/config.json" : forced;
}

std::string colorsFile() {
    // M3_COLORS_FILE: the greeter reads the palette synced for it. The state
    // directory keeps the name Matugen's templates have always written to.
    const std::string forced = env("M3_COLORS_FILE", "");
    if (!forced.empty()) return forced;
    return env("XDG_STATE_HOME", home() + "/.local/state") + "/quickshell/user/generated/colors.json";
}

std::string runtimeDir() {
    return env("XDG_RUNTIME_DIR", "/tmp/material3ui-" + std::to_string(getuid())) + "/material3ui";
}

std::string ipcSocket() {
    return runtimeDir() + "/ipc.sock";
}

std::string stateDir() {
    return env("XDG_STATE_HOME", home() + "/.local/state") + "/material3ui";
}

std::string dataFile(const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec).parent_path();
    const std::vector<fs::path> candidates = {
        env("M3_DATA_DIR", ""),
        exe / "../share/material3ui",
        exe / "../data", // shell/data, running from shell/build
        "/usr/share/material3ui",
    };
    for (const auto& dir : candidates) {
        if (dir.empty()) continue;
        if (fs::exists(dir / name, ec)) return (dir / name).string();
    }
    return {};
}

} // namespace m3::paths
