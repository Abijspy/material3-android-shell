#include "services/ThemedIcons.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "render/Images.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace m3::themedicons {

namespace {

std::string hex(const Color& c) {
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return std::format("#{:02x}{:02x}{:02x}", b(c.r), b(c.g), b(c.b));
}

std::string read(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The icon's own colours swapped for the palette's (each "from" exactly as
// the SVG writes it); written only when it differs from what's there
void theme(const std::string& name, const std::vector<std::pair<std::string, std::string>>& swaps) {
    const std::string source = paths::dataFile("icons/" + name + ".svg");
    if (source.empty()) return;
    std::string svg = read(source);
    for (const auto& [from, to] : swaps) {
        const std::string a = "fill=\"" + from + "\"", b = "fill=\"" + to + "\"";
        for (size_t at = svg.find(a); at != std::string::npos; at = svg.find(a, at + b.size())) svg.replace(at, a.size(), b);
    }
    const fs::path dir = paths::home() + "/.local/share/icons/hicolor/scalable/apps";
    const fs::path out = dir / (name + ".svg");
    if (read(out.string()) == svg) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path tmp = out.string() + ".tmp";
    {
        std::ofstream f(tmp);
        f << svg;
    }
    fs::rename(tmp, out, ec);
    if (ec) {
        log::warn("themed icons: {}: {}", out.string(), ec.message());
        return;
    }
    Images::get().reloadIcon(name);
}

} // namespace

void update() {
    // As Android's themed icons: dark, a deep tint behind a light glyph;
    // light, a pale tint behind a deep one. Contrast either way, whatever
    // the palette (some put light colours in primary even when dark)
    const bool dark = Config::get().value<bool>("/background/darkmode", true);
    const auto& t = Theme::get();
    const std::string back = hex(t.color(dark ? "on_primary" : "primary_container"));
    const std::string glyph = hex(t.color(dark ? "primary" : "on_primary_container"));
    theme("material3ui-settings", {{"#3367d6", back}, {"#fff", glyph}});
}

} // namespace m3::themedicons
