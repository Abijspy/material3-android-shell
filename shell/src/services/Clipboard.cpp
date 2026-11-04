#include "services/Clipboard.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
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

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isPicture(const fs::path& p) {
    const auto ext = lower(p.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif" || ext == ".bmp";
}

bool isVideo(const fs::path& p) {
    const auto ext = lower(p.extension().string());
    return ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov" || ext == ".avi" || ext == ".m4v";
}

// "file:///home/me/My%20clip.mp4" (or a bare absolute path) → the path, if
// the entry is one existing file; else empty
std::string localFile(const std::string& preview) {
    std::string p = preview;
    if (p.starts_with("file://")) {
        p = p.substr(7);
        std::string out;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '%' && i + 2 < p.size() && std::isxdigit(static_cast<unsigned char>(p[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(p[i + 2]))) {
                out += static_cast<char>(std::stoi(p.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                out += p[i];
            }
        }
        p = out;
    }
    while (!p.empty() && (p.back() == '\r' || p.back() == ' ')) p.pop_back();
    if (!p.starts_with('/') || p.find('\n') != std::string::npos) return {};
    std::error_code ec;
    return fs::is_regular_file(p, ec) ? p : std::string{};
}

} // namespace

Clipboard& Clipboard::get() {
    static Clipboard instance;
    return instance;
}

void Clipboard::refresh(std::function<void()> done) {
    run({"cliphist", "list"}, [this, done](int code, const std::string& out) {
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
        for (auto& e : m_entries) {
            if (e.image) decodeThumb(e);
            // A copied file: cliphist cuts long previews short, so the
            // whole entry tells which file
            else if (e.preview.starts_with("file://") || e.preview.starts_with('/')) fileThumb(e);
        }
        if (done) done();
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

void Clipboard::fileThumb(Entry& e) {
    const std::string id = e.id;
    run({"cliphist", "decode"}, [this, id](int code, const std::string& text) {
        if (code != 0) return;
        // A URI list: the first file
        const std::string file = localFile(text.substr(0, text.find('\n')));
        if (file.empty() || (!isPicture(file) && !isVideo(file))) return;
        for (auto& x : m_entries) {
            if (x.id != id) continue;
            x.file = file;
            x.video = isVideo(file);
            if (x.video) {
                videoThumb(x);
            } else {
                x.thumb = file;
            }
        }
        changed.emit();
    }, e.line);
}

// A frame from the video (the most typical of its first ones), small,
// cached by the file's path and change time
void Clipboard::videoThumb(Entry& e) {
    std::error_code ec;
    const auto stamp = fs::last_write_time(e.file, ec).time_since_epoch().count();
    const std::string file = std::format("{}/video-{:016x}.jpg", cacheDir(), std::hash<std::string>{}(e.file + std::to_string(stamp)));
    if (fs::exists(file, ec)) {
        e.thumb = file;
        return;
    }
    fs::create_directories(cacheDir(), ec);
    const std::string id = e.id;
    run({"ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", e.file, "-vf", "thumbnail,scale=320:-2", "-frames:v", "1", file},
        [this, id, file](int code, const std::string&) {
            std::error_code ec;
            if (code != 0 || !fs::exists(file, ec)) return;
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

std::vector<std::string> Clipboard::pinned() const {
    std::vector<std::string> out;
    for (const auto& p : Config::get().value<nlohmann::json>("/clipboard/pinned", nlohmann::json::array()))
        if (p.is_string()) out.push_back(p.get<std::string>());
    return out;
}

void Clipboard::pin(const Entry& e) {
    if (e.image) return;
    run({"cliphist", "decode"}, [](int code, const std::string& text) {
        if (code != 0 || text.empty()) return;
        auto now = Clipboard::get().pinned();
        std::erase(now, text);
        now.insert(now.begin(), text);
        if (now.size() > 20) now.resize(20);
        Config::get().set("/clipboard/pinned", now);
    }, e.line);
}

void Clipboard::unpin(const std::string& text) {
    auto now = pinned();
    if (std::erase(now, text) == 0) return;
    Config::get().set("/clipboard/pinned", now);
}

bool Clipboard::isPinned(const Entry& e) const {
    if (e.image || e.preview.empty()) return false;
    // cliphist's preview: the text's start, its whitespace runs made single spaces
    auto squash = [](const std::string& t) {
        std::string out;
        for (char c : t) {
            const bool space = std::isspace(static_cast<unsigned char>(c));
            if (space && (out.empty() || out.back() == ' ')) continue;
            out += space ? ' ' : c;
        }
        return out;
    };
    const std::string p = squash(e.preview);
    for (const auto& t : pinned())
        if (squash(t).starts_with(p.ends_with("…") ? p.substr(0, p.size() - 3) : p)) return true;
    return false;
}

void Clipboard::copyText(const std::string& text) {
    run({"wl-copy", "--", text}, nullptr);
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
