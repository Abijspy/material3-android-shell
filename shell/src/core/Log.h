#pragma once

#include <cstdio>
#include <format>
#include <string_view>

namespace m3::log {

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    std::fprintf(stderr, "material3ui: %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    std::fprintf(stderr, "material3ui: warning: %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    std::fprintf(stderr, "material3ui: error: %s\n", std::format(fmt, std::forward<Args>(args)...).c_str());
}

} // namespace m3::log
