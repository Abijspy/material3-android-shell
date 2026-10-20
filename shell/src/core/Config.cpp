#include "core/Config.h"

#include "core/FileWatcher.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <filesystem>
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

void Config::set(const std::string& pointer, const nlohmann::json& value) {
    reload(); // never write over edits made since we last read
    try {
        const auto ptr = nlohmann::json::json_pointer(pointer);
        // Unchanged: nothing written, so nothing reloads (every write has
        // the whole shell restyle)
        if (m_data.contains(ptr) && m_data.at(ptr) == value) return;
        m_data[ptr] = value;
    } catch (const std::exception& e) {
        log::warn("config: cannot set {}: {}", pointer, e.what());
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(paths::configDir(), ec);
    const std::string tmp = paths::configFile() + ".tmp";
    {
        std::ofstream out(tmp);
        out << m_data.dump(2);
    }
    std::filesystem::rename(tmp, paths::configFile(), ec);
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
