#include "services/NiriConfig.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Effects.h"
#include "services/Niri.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string niriDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.config") + "/niri";
}

std::string readAll(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string hex(const Color& c) {
    auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return std::format("#{:02x}{:02x}{:02x}", b(c.r), b(c.g), b(c.b));
}

const char* kInclude = "include optional=true \"material3ui-settings.kdl\"";

} // namespace

NiriConfig& NiriConfig::get() {
    static NiriConfig instance;
    return instance;
}

NiriConfig::NiriConfig() {
    // The focus ring follows the wallpaper's colours
    Theme::get().changed.connectForever([this] { apply(); });
    // niri's answer: it reads the file again by itself
    Niri::get().configLoaded.connectForever([this](bool failed) {
        const std::string was = m_error;
        m_error = failed ? "niri couldn't read its config: run niri validate to see why" : "";
        if (m_error != was) changed.emit();
    });
}

std::string NiriConfig::file() {
    return niriDir() + "/material3ui-settings.kdl";
}

std::string NiriConfig::includeFile() {
    return niriDir() + "/material3ui.kdl";
}

bool NiriConfig::included() const {
    return readAll(includeFile()).find(kInclude) != std::string::npos;
}

bool NiriConfig::userSetsOutput(const std::string& output) const {
    // A block of its own for it in config.kdl, not commented out
    const std::string text = readAll(niriDir() + "/config.kdl");
    std::stringstream ss(text);
    const std::string want = "output \"" + output + "\"";
    for (std::string l; std::getline(ss, l);) {
        const size_t a = l.find_first_not_of(" \t");
        if (a != std::string::npos && l.compare(a, want.size(), want) == 0) return true;
    }
    return false;
}

void NiriConfig::ensureIncluded() {
    // The shell's own file (from 6.0-beta-1, before this line was in it)
    const std::string path = includeFile();
    const std::string text = readAll(path);
    if (text.empty() || text.find(kInclude) != std::string::npos || text.find("Material3UI") == std::string::npos) return;
    std::ofstream(path, std::ios::app) << "\n// The shell's Settings (cursor, touchpad, keyboard, monitors…), last so they win\n" << kInclude << "\n";
    log::info("niri: material3ui.kdl now includes the shell's settings");
}

niri::Settings NiriConfig::collect() const {
    const auto& c = Config::get();
    niri::Settings s;
    s.cursorTheme = Effects::cursorTheme();
    const int size = c.value<int>("/accessibility/cursorSize", 0);
    s.cursorSize = size > 0 ? size : 24;
    auto flag = [&](const char* key, bool fallback) { return c.value<bool>(std::string("/input/") + key, fallback); };
    s.tap = flag("tapToClick", true);
    s.naturalScroll = flag("naturalScroll", true);
    s.dwt = flag("disableWhileTyping", true);
    s.clickFinger = flag("clickFinger", true);
    s.tapDrag = flag("tapAndDrag", true);
    s.dragLock = flag("dragLock", false);
    s.mouseNaturalScroll = flag("mouseNaturalScroll", false);
    s.scrollFactor = std::clamp(c.value<double>("/input/scrollFactor", 0.7), 0.1, 3.0);
    s.speed = std::clamp(c.value<double>("/input/sensitivity", 0.0), -1.0, 1.0);
    s.flat = c.value<std::string>("/input/accelProfile", "adaptive") == "flat";
    s.layouts = c.value<std::string>("/keyboard/layouts", "");
    s.layoutOptions = c.value<std::string>("/keyboard/options", "");
    s.repeatDelay = c.value<int>("/keyboard/repeatDelay", 0);
    s.repeatRate = c.value<int>("/keyboard/repeatRate", 0);
    if (c.value<bool>("/niri/focusRingColors", true)) {
        const auto& t = Theme::get();
        s.ringActive = s.borderActive = hex(t.color("primary"));
        s.ringInactive = s.borderInactive = hex(t.color("outline_variant"));
    }
    // Niri Settings: only the options set there
    const auto look = c.value<nlohmann::json>("/niri/look", nlohmann::json::object());
    auto num = [&](const char* k) -> std::optional<double> {
        if (look.is_object() && look.contains(k) && look[k].is_number()) return look[k].get<double>();
        return std::nullopt;
    };
    auto whole = [&](const char* k, int lo, int hi) -> std::optional<int> {
        if (auto v = num(k)) return std::clamp(static_cast<int>(std::lround(*v)), lo, hi);
        return std::nullopt;
    };
    auto yes = [&](const char* k) -> std::optional<bool> {
        if (look.is_object() && look.contains(k) && look[k].is_boolean()) return look[k].get<bool>();
        return std::nullopt;
    };
    auto& l = s.look;
    l.gaps = whole("gaps", 0, 200);
    if (look.is_object() && look.contains("centerColumn") && look["centerColumn"].is_string()) {
        const std::string v = look["centerColumn"];
        if (v == "never" || v == "always" || v == "on-overflow") l.centerColumn = v;
    }
    l.centerSingle = yes("centerSingle");
    if (auto v = num("columnWidth")) l.columnWidth = std::clamp(*v, 0.1, 1.0);
    l.strutLeft = whole("strutLeft", 0, 1000);
    l.strutRight = whole("strutRight", 0, 1000);
    l.strutTop = whole("strutTop", 0, 1000);
    l.strutBottom = whole("strutBottom", 0, 1000);
    l.ringOn = yes("ringOn");
    l.ringWidth = whole("ringWidth", 1, 64);
    l.borderOn = yes("borderOn");
    l.borderWidth = whole("borderWidth", 1, 64);
    l.shadowOn = yes("shadowOn");
    l.shadowSoftness = whole("shadowSoftness", 0, 200);
    l.shadowSpread = whole("shadowSpread", 0, 100);
    l.shadowOffset = whole("shadowOffset", -100, 100);
    l.cornerRadius = whole("cornerRadius", 0, 64);
    l.animations = yes("animations");
    if (auto v = num("slowdown")) l.slowdown = std::clamp(*v, 0.1, 10.0);
    l.preferNoCsd = yes("preferNoCsd");
    l.backAndForth = yes("backAndForth");
    if (auto help = yes("hotkeyHelp")) l.skipHotkeys = !*help;
    l.focusFollowsMouse = yes("focusFollowsMouse").value_or(false);
    l.warpMouse = yes("warpMouse").value_or(false);
    l.hotCornersOff = !yes("hotCorner").value_or(true);
    if (auto v = num("overviewZoom")) l.overviewZoom = std::clamp(*v, 0.05, 0.75);
    // Kept here: items() of a temporary would outlive it
    const auto outputs = c.value<nlohmann::json>("/niri/outputs", nlohmann::json::object());
    for (const auto& [name, o] : outputs.items()) {
        if (!o.is_object()) continue;
        niri::Settings::Output out;
        out.name = name;
        out.off = o.value("off", false);
        out.mode = o.value("mode", "");
        out.scale = o.value("scale", 0.0);
        out.transform = o.value("transform", "");
        if (o.contains("x") && o.contains("y")) out.position = std::pair{o.value("x", 0), o.value("y", 0)};
        out.vrr = o.value("vrr", false);
        s.outputs.push_back(out);
    }
    const auto binds = c.value<nlohmann::json>("/niri/binds", nlohmann::json::array());
    for (const auto& b : binds)
        if (b.is_object()) s.binds.push_back({b.value("keys", ""), b.value("command", ""), b.value("title", "")});
    return s;
}

void NiriConfig::apply() {
    if (!Niri::get().available()) return;
    const auto s = collect();
    // GTK's cursor, for the apps that read it there
    const std::string cursor = std::format("{} {}", s.cursorTheme, s.cursorSize);
    if (cursor != m_cursor) {
        m_cursor = cursor;
        if (!s.cursorTheme.empty())
            run({"sh", "-c", "gsettings set org.gnome.desktop.interface cursor-theme \"$1\"; gsettings set org.gnome.desktop.interface cursor-size \"$2\"",
                 "sh", s.cursorTheme, std::to_string(s.cursorSize)},
                nullptr);
    }
    ensureIncluded();
    const std::string text = niri::render(s);
    if (text == m_written || text == readAll(file())) {
        m_written = text;
        return;
    }
    std::error_code ec;
    fs::create_directories(niriDir(), ec);
    const std::string path = file();
    std::ofstream(path + ".tmp") << text;
    fs::rename(path + ".tmp", path, ec);
    m_written = text;
    log::info("niri: settings written to {}", path);
    changed.emit();
}

std::vector<kdl::Node> NiriConfig::yours() const {
    std::vector<kdl::Node> out;
    // Includes are read where they stand, as niri does; ours is left out
    std::error_code none;
    const std::string skip = fs::weakly_canonical(file(), none).string();
    std::function<void(const fs::path&, int)> read = [&](const fs::path& path, int depth) {
        std::error_code ec;
        if (depth > 8 || fs::weakly_canonical(path, ec).string() == skip) return;
        for (auto& n : kdl::parse(readAll(path.string()))) {
            if (n.name != "include") {
                out.push_back(std::move(n));
                continue;
            }
            if (n.args.empty() || n.args[0].type != kdl::Value::Type::String) continue;
            std::string target = n.args[0].text;
            if (target.starts_with("~/")) target = paths::home() + target.substr(1);
            const fs::path p(target);
            read(p.is_absolute() ? p : path.parent_path() / p, depth + 1);
        }
    };
    read(fs::path(niriDir()) / "config.kdl", 0);
    return out;
}

void NiriConfig::resetLook(const std::vector<std::string>& keys) {
    auto look = Config::get().value<nlohmann::json>("/niri/look", nlohmann::json::object());
    if (!look.is_object() || keys.empty()) look = nlohmann::json::object();
    else
        for (const auto& k : keys) look.erase(k);
    Config::get().set("/niri/look", look);
}

void NiriConfig::saveOutput(const niri::Settings::Output& o) {
    nlohmann::json j = {{"off", o.off}};
    if (!o.mode.empty()) j["mode"] = o.mode;
    if (o.scale > 0) j["scale"] = o.scale;
    if (!o.transform.empty()) j["transform"] = o.transform;
    if (o.position) {
        j["x"] = o.position->first;
        j["y"] = o.position->second;
    }
    if (o.vrr) j["vrr"] = true;
    Config::get().set("/niri/outputs/" + o.name, j);
}

} // namespace m3
