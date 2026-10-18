#include "core/Config.h"

#include "core/FileWatcher.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <fstream>

namespace m3 {

Config& Config::get() {
    static Config instance;
    return instance;
}

Config::Config() {
    reload();
    FileWatcher::get().watch(paths::configFile(), [this] {
        reload();
        changed.emit();
    });
}

void Config::reload() {
    std::ifstream in(paths::configFile());
    if (!in) {
        m_data = nlohmann::json::object();
        return;
    }
    try {
        m_data = nlohmann::json::parse(in, nullptr, true, true);
    } catch (const std::exception& e) {
        log::warn("config.json: {}", e.what());
    }
}

} // namespace m3
