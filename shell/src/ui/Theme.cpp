#include "ui/Theme.h"

#include "core/Config.h"
#include "core/FileWatcher.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "ui/Animation.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace m3 {

namespace {

// Material 3 baseline, dark
const std::map<std::string, std::string> kBaseline = {
    {"primary", "#d0bcff"}, {"on_primary", "#381e72"}, {"primary_container", "#4f378b"},
    {"on_primary_container", "#eaddff"}, {"secondary", "#ccc2dc"}, {"on_secondary", "#332d41"},
    {"secondary_container", "#4a4458"}, {"on_secondary_container", "#e8def8"}, {"tertiary", "#efb8c8"},
    {"on_tertiary", "#492532"}, {"tertiary_container", "#633b48"}, {"on_tertiary_container", "#ffd8e4"},
    {"error", "#f2b8b5"}, {"on_error", "#601410"}, {"error_container", "#8c1d18"},
    {"on_error_container", "#f9dedc"}, {"background", "#141218"}, {"on_background", "#e6e0e9"},
    {"surface", "#141218"}, {"on_surface", "#e6e0e9"}, {"surface_variant", "#49454f"},
    {"on_surface_variant", "#cac4d0"}, {"surface_container_lowest", "#0f0d13"},
    {"surface_container_low", "#1d1b20"}, {"surface_container", "#211f26"},
    {"surface_container_high", "#2b2930"}, {"surface_container_highest", "#36343b"},
    {"surface_bright", "#3b383e"}, {"outline", "#938f99"}, {"outline_variant", "#49454f"},
    {"inverse_surface", "#e6e0e9"}, {"inverse_on_surface", "#322f35"}, {"inverse_primary", "#6750a4"},
    {"shadow", "#000000"}, {"scrim", "#000000"},
};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

} // namespace

std::optional<Color> Color::fromHex(const std::string& hex) {
    if ((hex.size() != 7 && hex.size() != 9) || hex[0] != '#') return std::nullopt;
    float out[4] = {0, 0, 0, 1};
    for (size_t i = 0; i < (hex.size() - 1) / 2; ++i) {
        const int hi = hexDigit(hex[1 + i * 2]), lo = hexDigit(hex[2 + i * 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    return Color{out[0], out[1], out[2], out[3]};
}

Color ColorRef::resolve() const {
    if (fixed) return *fixed;
    return Theme::get().color(role).withAlpha(alpha);
}

Theme& Theme::get() {
    static Theme instance;
    return instance;
}

Theme::Theme() {
    load();
    loadEffects();
    FileWatcher::get().watch(paths::colorsFile(), [this] {
        log::info("palette changed, recolouring");
        load();
        notify();
    });
    Config::get().changed.connectForever([this] {
        loadEffects();
        notify();
    });
}

void Theme::loadEffects() {
    auto& c = Config::get();
    m_blur = c.value<bool>("/appearance/blur/enabled", true);
    m_barOpacity = std::clamp(static_cast<float>(c.value<double>("/appearance/transparency/bar", 0.8)), 0.3f, 1.0f);
    m_panelOpacity = std::clamp(static_cast<float>(c.value<double>("/appearance/transparency/panels", 0.9)), 0.3f, 1.0f);
    m_roundness = std::clamp(static_cast<float>(c.value<double>("/appearance/roundness", 1.0)), 0.0f, 1.5f);
    ui::motion::scale = std::clamp(static_cast<float>(c.value<double>("/appearance/animationSpeed", 1.0)), 0.0f, 2.0f);
}

void Theme::load() {
    m_palette.clear();
    for (const auto& [role, hex] : kBaseline) m_palette[role] = *Color::fromHex(hex);
    std::ifstream in(paths::colorsFile());
    if (!in) return;
    try {
        // Keep the document alive: items() only references it
        const auto doc = nlohmann::json::parse(in);
        for (const auto& [role, val] : doc.items())
            if (val.is_string())
                if (auto c = Color::fromHex(val.get<std::string>())) m_palette[role] = *c;
    } catch (const std::exception& e) {
        log::warn("colors.json: {}", e.what());
    }
}

void Theme::notify() {
    changed.emit();
}

Color Theme::color(const std::string& role) const {
    auto it = m_palette.find(role);
    return it != m_palette.end() ? it->second : Color{1, 0, 1, 1};
}

std::string Theme::fontFamily() const {
    return Config::get().value<std::string>("/appearance/fonts/main", "Google Sans Flex") + ", Noto Sans, sans-serif";
}

std::string Theme::monoFamily() const {
    return Config::get().value<std::string>("/appearance/fonts/monospace", "JetBrains Mono NF") + ", monospace";
}

} // namespace m3
