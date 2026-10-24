#pragma once

#include "core/Signal.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace m3 {

// ~/.config/material3ui/config.json, shared with the QML shell during the
// port. Values are read by JSON pointer with a fallback; the file is watched
// and listeners run after every reload.
class Config {
public:
    static Config& get();

    template <typename T>
    T value(const std::string& pointer, T fallback) const {
        try {
            const auto ptr = nlohmann::json::json_pointer(pointer);
            if (m_data.contains(ptr)) return m_data.at(ptr).get<T>();
        } catch (const std::exception&) {
        }
        return fallback;
    }

    Signal<> changed;
    void reload();
    // Writes one value (creating parent objects) and saves config.json
    // atomically; listeners run when the file watcher sees the change.
    void set(const std::string& pointer, const nlohmann::json& value);

    // The whole of it as a file to keep or move to another computer, and
    // back: import replaces every setting (the caller backs up first). An
    // empty string on success, else why not.
    std::string exportTo(const std::string& path) const;
    std::string importFrom(const std::string& path);

private:
    void write();
    Config();

    nlohmann::json m_data = nlohmann::json::object();
};

} // namespace m3
