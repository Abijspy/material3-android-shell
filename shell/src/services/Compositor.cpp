#include "services/Compositor.h"

#include "services/Niri.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace m3 {

std::string Compositor::session() {
    static const std::string kind = [] {
        const char* niri = std::getenv("NIRI_SOCKET");
        return std::string(niri && *niri ? "niri" : "");
    }();
    return kind;
}

Compositor& Compositor::get() {
    // niri's backend, which outside niri stands in with nothing in it
    return Niri::get();
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
