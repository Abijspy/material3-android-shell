#include "services/Backups.h"

#include "core/Config.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Hyprland.h"

#include <algorithm>
#include <filesystem>

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

std::uintmax_t folderSize(const fs::path& dir) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
        if (e.is_regular_file(ec)) total += e.file_size(ec);
    return total;
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
        out.push_back({Kind::Settings, e.path().string(), reason, mtime(e.path()), e.file_size(ec)});
    }
    // The installer's: YYYYmmdd-HHMMSS folders with a restore.sh
    for (const auto& e : fs::directory_iterator(root(), ec))
        if (e.is_directory(ec) && fs::exists(e.path() / "restore.sh", ec))
            out.push_back({Kind::Install, e.path().string(), "install", mtime(e.path() / "restore.sh"), folderSize(e.path())});
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
    if (entry.kind == Kind::Install) {
        // The installer's own undo, in a terminal: it says what it puts back
        const char* t = std::getenv("TERMINAL");
        run({t && *t ? t : "alacritty", "-e", "sh", "-c", "sh '" + entry.path + "/restore.sh'; echo; read -p 'Done. Press Enter to close…' _"},
            [done](int code, const std::string&) {
                if (done) done(code == 0);
            });
        return;
    }
    const std::string path = entry.path;
    create("before-restore", [this, path, done](bool) {
        m_busy = true;
        changed.emit();
        run({"tar", "-xzf", path, "-C", paths::home()}, [this, done](int code, const std::string&) {
            m_busy = false;
            // The shell and Hyprland pick the restored settings up
            Config::get().reload();
            Hyprland::get().request("reload");
            changed.emit();
            if (done) done(code == 0);
        });
    });
}

void Backups::remove(const Entry& entry) {
    if (entry.kind != Kind::Settings) return; // the installer's stay its own
    std::error_code ec;
    fs::remove(entry.path, ec);
    changed.emit();
}

void Backups::prune() {
    // Keep every manual backup, and the last ten automatic ones
    int automatic = 0;
    std::error_code ec;
    for (const auto& e : list())
        if (e.kind == Kind::Settings && e.reason != "manual" && ++automatic > 10) fs::remove(e.path, ec);
}

} // namespace m3
