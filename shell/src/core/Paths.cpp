#include "core/Paths.h"

#include <cstdlib>
#include <unistd.h>

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
    return configDir() + "/config.json";
}

std::string colorsFile() {
    return env("XDG_STATE_HOME", home() + "/.local/state") + "/quickshell/user/generated/colors.json";
}

std::string runtimeDir() {
    return env("XDG_RUNTIME_DIR", "/tmp/material3ui-" + std::to_string(getuid())) + "/material3ui";
}

std::string ipcSocket() {
    return runtimeDir() + "/ipc.sock";
}

} // namespace m3::paths
