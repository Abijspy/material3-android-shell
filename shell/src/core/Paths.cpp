#include "core/Paths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

std::string instance() {
    // A name for a folder: letters, digits, dashes
    std::string name;
    for (char c : env("M3_INSTANCE", ""))
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') name += c;
    return name;
}

std::string runtimeDir() {
    const std::string name = instance();
    return env("XDG_RUNTIME_DIR", "/tmp/material3ui-" + std::to_string(getuid())) + "/material3ui" + (name.empty() ? "" : "-" + name);
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
        if (fs::exists(dir / name, ec)) return (dir / name).lexically_normal().string();
    }
    return {};
}

std::vector<std::string> dataDirs() {
    std::vector<std::string> dirs;
    std::stringstream ss(env("XDG_DATA_DIRS", ""));
    std::string d;
    while (std::getline(ss, d, ':'))
        if (!d.empty()) dirs.push_back(d);
    if (dirs.empty()) dirs = {"/usr/local/share", "/usr/share"};
    // User installs before system ones, as flatpak's profile.d script orders them
    const std::string flatpaks[] = {env("XDG_DATA_HOME", home() + "/.local/share") + "/flatpak/exports/share",
                                    "/var/lib/flatpak/exports/share"};
    for (auto it = std::rbegin(flatpaks); it != std::rend(flatpaks); ++it)
        if (std::ranges::find(dirs, *it) == dirs.end()) dirs.insert(dirs.begin(), *it);
    return dirs;
}

bool writeFile(const std::string& path, const std::string& text) {
    namespace fs = std::filesystem;
    const fs::path p(path);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    // Hidden, so nothing that lists the folder takes it for one of its own
    const fs::path tmp = p.parent_path() / ("." + p.filename().string() + ".m3new");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!(out << text) || !out.flush()) return false;
    }
    fs::rename(tmp, p, ec);
    if (!ec) return true;
    std::error_code ignored;
    fs::remove(tmp, ignored);
    return false;
}

} // namespace m3::paths
