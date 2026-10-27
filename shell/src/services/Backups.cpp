#include "services/Backups.h"

#include "core/Config.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Compositor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string root() {
    return paths::configDir() + "/backups";
}

std::string settingsDir() {
    return root() + "/settings";
}

// What a settings backup holds, relative to the home folder: the shell's
// settings, and the Hyprland settings Settings writes (not shell.lua and the
// others, which updates own)
std::vector<std::string> members() {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path home = paths::home();
    for (const char* f : {".config/material3ui/config.json", ".config/material3ui/keybinds.json", ".config/material3ui/hyprland-profiles"})
        if (fs::exists(home / f, ec)) out.push_back(f);
    for (const auto& e : fs::directory_iterator(home / ".config/hypr/material3ui", ec)) {
        const std::string name = e.path().filename().string();
        if (name.ends_with("_custom.lua") || name == "user.lua" || name == "user_persistence.lua")
            out.push_back(".config/hypr/material3ui/" + name);
    }
    return out;
}

std::time_t mtime(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return 0;
    return std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(t));
}

} // namespace

Backups& Backups::get() {
    static Backups instance;
    return instance;
}

std::vector<Backups::Entry> Backups::list() const {
    std::vector<Entry> out;
    std::error_code ec;
    // 2026-10-20_10-15-30_before-update.tar.gz
    for (const auto& e : fs::directory_iterator(settingsDir(), ec)) {
        const std::string name = e.path().filename().string();
        if (!name.ends_with(".tar.gz")) continue;
        std::string reason = name.substr(0, name.size() - 7);
        reason = reason.size() > 20 ? reason.substr(20) : "manual";
        out.push_back({e.path().string(), reason, mtime(e.path()), e.file_size(ec)});
    }
    std::ranges::sort(out, [](const Entry& a, const Entry& b) { return a.time > b.time; });
    return out;
}

void Backups::create(const std::string& reason, std::function<void(bool)> done) {
    const auto files = members();
    if (files.empty()) {
        if (done) done(false);
        return;
    }
    std::error_code ec;
    fs::create_directories(settingsDir(), ec);
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &tm);
    std::vector<std::string> argv = {"tar", "-czf", settingsDir() + "/" + stamp + "_" + reason + ".tar.gz", "-C", paths::home()};
    argv.insert(argv.end(), files.begin(), files.end());
    m_busy = true;
    changed.emit();
    run(argv, [this, done](int code, const std::string&) {
        m_busy = false;
        prune();
        changed.emit();
        if (done) done(code == 0);
    });
}

void Backups::restore(const Entry& entry, std::function<void(bool)> done) {
    const std::string path = entry.path;
    // Only settings files come out of it: anything else (a damaged or
    // foreign archive) and nothing is touched
    run({"tar", "-tzf", path}, [this, path, done](int code, const std::string& out) {
        std::stringstream ss(out);
        bool safe = code == 0 && !out.empty();
        for (std::string l; safe && std::getline(ss, l);)
            safe = l.find("..") == std::string::npos && !l.starts_with("/") &&
                   (l.starts_with(".config/material3ui/") || l.starts_with(".config/hypr/material3ui/"));
        if (!safe) {
            if (done) done(false);
            return;
        }
        restoreChecked(path, done);
    });
}

void Backups::restoreChecked(const std::string& path, std::function<void(bool)> done) {
    create("before-restore", [this, path, done](bool) {
        m_busy = true;
        changed.emit();
        run({"tar", "-xzf", path, "-C", paths::home()}, [this, done](int code, const std::string&) {
            m_busy = false;
            // The shell and Hyprland pick the restored settings up
            Config::get().reload();
            Compositor::get().reload();
            changed.emit();
            if (done) done(code == 0);
        });
    });
}

void Backups::remove(const Entry& entry) {
    std::error_code ec;
    fs::remove(entry.path, ec);
    changed.emit();
}

void Backups::prune() {
    // Keep every manual backup, and the last ten automatic ones
    int automatic = 0;
    std::error_code ec;
    for (const auto& e : list())
        if (e.reason != "manual" && ++automatic > 10) fs::remove(e.path, ec);
}

} // namespace m3
