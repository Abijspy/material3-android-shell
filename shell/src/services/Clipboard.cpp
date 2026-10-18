#include "services/Clipboard.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string cacheDir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.cache") + "/material3ui/clipboard";
}

// "[[ binary data 12 KiB png 800x600 ]]" → "png"
std::string imageType(const std::string& preview) {
    for (const char* t : {"png", "jpeg", "jpg", "webp", "bmp", "gif"})
        if (preview.find(std::string(" ") + t + " ") != std::string::npos) return t;
    return "";
}

} // namespace

Clipboard& Clipboard::get() {
    static Clipboard instance;
    return instance;
}

void Clipboard::refresh() {
    run({"cliphist", "list"}, [this](int code, const std::string& out) {
        m_available = code == 0;
        std::vector<Entry> entries;
        std::stringstream ss(out);
        std::string line;
        while (std::getline(ss, line) && entries.size() < 200) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            Entry e{line, line.substr(0, tab), line.substr(tab + 1)};
            e.image = e.preview.starts_with("[[ binary") && !imageType(e.preview).empty();
            entries.push_back(std::move(e));
        }
        m_entries = std::move(entries);
        for (auto& e : m_entries)
            if (e.image) decodeThumb(e);
        changed.emit();
    });
}

void Clipboard::decodeThumb(Entry& e) {
    const std::string file = cacheDir() + "/" + e.id + "." + imageType(e.preview);
    std::error_code ec;
    if (fs::exists(file, ec)) {
        e.thumb = file;
        return;
    }
    fs::create_directories(cacheDir(), ec);
    const std::string id = e.id;
    run({"sh", "-c", "printf '%s' \"$1\" | cliphist decode > \"$2\"", "sh", e.line, file}, [this, id, file](int code, const std::string&) {
        if (code != 0) return;
        for (auto& x : m_entries)
            if (x.id == id) x.thumb = file;
        changed.emit();
    });
}

void Clipboard::copy(const Entry& e) {
    // Through a pipe: images are larger than run()'s input buffer
    run({"sh", "-c", "printf '%s' \"$1\" | cliphist decode | wl-copy", "sh", e.line}, nullptr);
}

void Clipboard::remove(const Entry& e) {
    run({"cliphist", "delete"}, [this](int, const std::string&) { refresh(); }, e.line);
}

void Clipboard::wipe() {
    run({"cliphist", "wipe"}, [this](int, const std::string&) {
        std::error_code ec;
        fs::remove_all(cacheDir(), ec);
        refresh();
    });
}

void Clipboard::ensureWatchers() {
    if (!Config::get().value<bool>("/clipboard/history", true)) return;
    run({"pgrep", "-f", "wl-paste.*cliphist"}, [](int code, const std::string&) {
        if (code == 0) return; // Hyprland's autostart already feeds cliphist
        log::info("clipboard: starting the cliphist watchers");
        spawn("wl-paste --type text --watch cliphist store");
        spawn("wl-paste --type image --watch cliphist store");
    });
}

} // namespace m3
