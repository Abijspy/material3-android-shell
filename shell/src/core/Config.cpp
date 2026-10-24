#include "core/Config.h"

#include "core/FileWatcher.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
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
    write();
}

void Config::write() {
    std::error_code ec;
    std::filesystem::create_directories(paths::configDir(), ec);
    const std::string tmp = paths::configFile() + ".tmp";
    {
        std::ofstream out(tmp);
        out << m_data.dump(2);
    }
    std::filesystem::rename(tmp, paths::configFile(), ec);
}

std::string Config::exportTo(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return "Couldn't write " + path;
    out << m_data.dump(2) << '\n';
    return out.good() ? "" : "Couldn't write " + path;
}

std::string Config::importFrom(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "Couldn't open that file";
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(in, nullptr, true, true);
    } catch (const std::exception&) {
        return "That file isn't valid JSON";
    }
    // Shell settings are one object of sections ("bar", "dock"…)
    if (!data.is_object() || data.empty()) return "That file doesn't hold Material3UI settings";
    const bool ours = std::ranges::any_of(data.items(), [](const auto& kv) {
        static const std::vector<std::string> known = {"bar", "statusBar", "dock", "appearance", "background", "lockscreen",
                                                       "notifications", "quickSettings", "launcher", "desktop", "weather",
                                                       "clock", "idle", "screenshot", "quickshare", "calendar", "hyprland"};
        return std::ranges::find(known, kv.key()) != known.end();
    });
    if (!ours) return "That file doesn't hold Material3UI settings";
    m_data = std::move(data);
    write(); // the file watcher reloads everyone
    return "";
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
