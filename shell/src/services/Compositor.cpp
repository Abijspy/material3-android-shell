#include "services/Compositor.h"

#include "services/Hyprland.h"
#include "services/Niri.h"
#include "services/Umbriel.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace m3 {

std::string Compositor::session() {
    static const std::string kind = [] {
        auto env = [](const char* name) {
            const char* v = std::getenv(name);
            return std::string(v ? v : "");
        };
        const std::string display = env("WAYLAND_DISPLAY"), umbriel = env("UMBRIEL_SOCKET"), niri = env("NIRI_SOCKET");
        // Its socket says whose display this is: umbriel-wayland-1.sock,
        // niri.wayland-1.1234.sock
        if (!display.empty() && umbriel.ends_with("/umbriel-" + display + ".sock")) return std::string("umbriel");
        if (!display.empty() && niri.find("/niri." + display + ".") != std::string::npos) return std::string("niri");
        if (!umbriel.empty()) return std::string("umbriel");
        if (!niri.empty()) return std::string("niri");
        if (!env("HYPRLAND_INSTANCE_SIGNATURE").empty()) return std::string("hyprland");
        return std::string();
    }();
    return kind;
}

Compositor& Compositor::get() {
    // Hyprland's backend outside the other two, which outside Hyprland too
    // stands in with nothing in it
    static Compositor& chosen = []() -> Compositor& {
        if (session() == "umbriel") return Umbriel::get();
        if (session() == "niri") return Niri::get();
        return Hyprland::get();
    }();
    return chosen;
}

std::string Compositor::keyboardLayoutShort() const {
    const auto layouts = keyboardLayouts();
    const int i = keyboardLayout();
    if (i < 0 || i >= static_cast<int>(layouts.size())) return {};
    return layoutShortName(layouts[static_cast<size_t>(i)]);
}

std::string Compositor::layoutShortName(const std::string& name) {
    // A code in brackets ("English (US)", "English (UK)") says it
    if (const auto open = name.rfind('('), close = name.rfind(')'); open != std::string::npos && (close == open + 3 || close == open + 4)) {
        std::string code = name.substr(open + 1, close - open - 1);
        if (std::ranges::all_of(code, [](char c) { return std::isupper(static_cast<unsigned char>(c)); })) return code;
    }
    static const std::pair<const char*, const char*> languages[] = {
        {"English", "EN"},   {"German", "DE"},  {"French", "FR"},  {"Spanish", "ES"},  {"Italian", "IT"},  {"Portuguese", "PT"},
        {"Russian", "RU"},   {"Ukrainian", "UK"}, {"Polish", "PL"}, {"Czech", "CS"},   {"Dutch", "NL"},    {"Swedish", "SV"},
        {"Norwegian", "NO"}, {"Danish", "DA"},  {"Finnish", "FI"}, {"Turkish", "TR"},  {"Greek", "EL"},    {"Hebrew", "HE"},
        {"Arabic", "AR"},    {"Hindi", "HI"},   {"Tamil", "TA"},   {"Japanese", "JA"}, {"Korean", "KO"},   {"Chinese", "ZH"},
        {"Hungarian", "HU"}, {"Romanian", "RO"}, {"Vietnamese", "VI"}, {"Thai", "TH"}, {"Persian", "FA"}, {"Bengali", "BN"},
    };
    for (const auto& [lang, code] : languages)
        if (name.starts_with(lang)) return code;
    std::string out;
    for (char c : name) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        if (out.size() < 2) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out.empty() ? "??" : out;
}

int Compositor::activeWorkspace(const std::string& output) const {
    for (const auto& o : outputs())
        if (o.name == output || (output.empty() && o.focused)) return o.activeWorkspace;
    return 0;
}

std::string Compositor::focusedOutput() const {
    for (const auto& o : outputs())
        if (o.focused) return o.name;
    return {};
}

} // namespace m3
