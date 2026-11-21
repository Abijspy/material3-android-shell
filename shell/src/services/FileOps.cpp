#include "services/FileOps.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3::fileops {

namespace {

std::string lowerExt(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool among(const std::string& ext, std::initializer_list<const char*> list) {
    return std::ranges::any_of(list, [&](const char* e) { return ext == e; });
}

std::string percentEncode(const std::string& s, bool keepSlash) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || (keepSlash && c == '/')) out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

std::string percentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string md5Hex(const std::string& s) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    EVP_Digest(s.data(), s.size(), digest, &n, EVP_md5(), nullptr);
    std::string out;
    for (unsigned i = 0; i < n; ++i) out += std::format("{:02x}", digest[i]);
    return out;
}

// ── Operations, one at a time, in a queue ──

std::string g_status;
Signal<> g_changed;

struct Job {
    std::string what;                       // "Copying 3 items"
    std::vector<std::vector<std::string>> commands;
    Done done;
};
std::deque<Job> g_jobs;
bool g_running = false;

void runNext();

void runCommands(std::shared_ptr<Job> job, size_t i, std::string errors) {
    if (i >= job->commands.size()) {
        if (job->done) job->done(errors);
        g_running = false;
        g_status.clear();
        g_changed.emit();
        return runNext();
    }
    run(job->commands[i], [job, i, errors](int code, const std::string& out) mutable {
        if (code != 0) {
            log::warn("files: {} failed ({}){}", job->commands[i].front(), code, out.empty() ? "" : ": " + out);
            if (errors.empty()) errors = std::format("{} couldn't finish", job->what);
        }
        runCommands(job, i + 1, errors);
    });
}

void runNext() {
    if (g_running || g_jobs.empty()) return;
    auto job = std::make_shared<Job>(std::move(g_jobs.front()));
    g_jobs.pop_front();
    g_running = true;
    g_status = job->what + "…";
    g_changed.emit();
    runCommands(job, 0, {});
}

void queue(std::string what, std::vector<std::vector<std::string>> commands, Done done) {
    if (commands.empty()) {
        if (done) done({});
        return;
    }
    g_jobs.push_back({std::move(what), std::move(commands), std::move(done)});
    runNext();
}

std::string items(size_t n) {
    return n == 1 ? "1 item" : std::format("{} items", n);
}

std::string isoNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

std::string trashRoot() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.local/share") + "/Trash";
}

// Video thumbnails waiting for ffmpeg, one at a time
std::deque<std::pair<std::string, std::string>> g_thumbQueue; // video, png
std::map<std::string, std::vector<std::function<void()>>> g_thumbWaiters;
std::set<std::string> g_thumbFailed;
bool g_thumbRunning = false;

void nextThumb() {
    if (g_thumbRunning || g_thumbQueue.empty()) return;
    const auto [video, png] = g_thumbQueue.front();
    g_thumbQueue.pop_front();
    g_thumbRunning = true;
    std::error_code ec;
    fs::create_directories(fs::path(png).parent_path(), ec);
    // A frame a few seconds in (past a black start), 320 px wide
    run({"timeout", "20", "ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-ss", "3", "-i", video, "-frames:v", "1", "-vf", "scale=320:-2", png},
        [video, png](int code, const std::string&) {
            g_thumbRunning = false;
            std::error_code ec2;
            if (code != 0 || !fs::exists(png, ec2)) g_thumbFailed.insert(video);
            auto waiters = std::move(g_thumbWaiters[video]);
            g_thumbWaiters.erase(video);
            for (auto& w : waiters)
                if (w) w();
            nextThumb();
        });
}

} // namespace

// ── Listing ──

std::vector<Entry> list(const std::string& dir, bool hidden, std::string* error) {
    std::vector<Entry> out;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        if (error) *error = ec.message();
        return out;
    }
    for (const auto& e : it) {
        Entry x;
        x.path = e.path().string();
        x.name = e.path().filename().string();
        x.hidden = x.name.starts_with(".");
        if (x.hidden && !hidden) continue;
        std::error_code ec2;
        x.link = e.is_symlink(ec2);
        x.dir = e.is_directory(ec2);
        if (!x.dir) x.size = e.file_size(ec2), ec2.clear();
        struct stat st {};
        if (::stat(x.path.c_str(), &st) == 0) x.modified = st.st_mtime;
        out.push_back(std::move(x));
    }
    if (error) error->clear();
    return out;
}

Kind kindOf(const std::string& path, bool dir) {
    if (dir) return Kind::Folder;
    const std::string e = lowerExt(path);
    if (among(e, {".jpg", ".jpeg", ".png", ".webp", ".gif", ".bmp", ".svg", ".tif", ".tiff", ".avif", ".heic", ".ico", ".jxl"})) return Kind::Picture;
    if (among(e, {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v", ".wmv", ".flv", ".mpg", ".mpeg", ".3gp", ".ogv"})) return Kind::Video;
    if (among(e, {".mp3", ".flac", ".ogg", ".opus", ".m4a", ".wav", ".aac", ".wma", ".alac", ".aiff"})) return Kind::Audio;
    if (among(e, {".pdf", ".doc", ".docx", ".odt", ".rtf", ".xls", ".xlsx", ".ods", ".ppt", ".pptx", ".odp", ".epub"})) return Kind::Document;
    if (among(e, {".txt", ".md", ".log", ".csv", ".ini", ".conf", ".cfg", ".toml", ".yaml", ".yml", ".json", ".xml", ".kdl"})) return Kind::Text;
    if (among(e, {".deb", ".rpm", ".apk", ".appimage", ".flatpak"}) || path.ends_with(".pkg.tar.zst")) return Kind::Package;
    if (among(e, {".zip", ".tar", ".gz", ".tgz", ".xz", ".zst", ".bz2", ".7z", ".rar", ".iso"})) return Kind::Archive;
    if (among(e, {".c", ".cpp", ".h", ".hpp", ".py", ".js", ".ts", ".rs", ".go", ".java", ".sh", ".lua", ".rb", ".php", ".html", ".css"})) return Kind::Code;
    return Kind::Other;
}

bool isPicture(const std::string& path) {
    return kindOf(path) == Kind::Picture;
}

std::string kindIcon(Kind kind, const std::string& path) {
    switch (kind) {
    case Kind::Folder: return "folder";
    case Kind::Picture: return "image";
    case Kind::Video: return "movie";
    case Kind::Audio: return "music_note";
    case Kind::Document: return lowerExt(path) == ".pdf" ? "picture_as_pdf" : "article";
    case Kind::Text: return "description";
    case Kind::Archive: return "folder_zip";
    case Kind::Code: return "code";
    case Kind::Package: return lowerExt(path) == ".apk" ? "android" : "deployed_code";
    case Kind::Other: break;
    }
    return "draft";
}

std::string kindName(Kind kind, const std::string& path) {
    std::string ext = lowerExt(path);
    if (!ext.empty()) ext = ext.substr(1);
    for (auto& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (kind) {
    case Kind::Folder: return "Folder";
    case Kind::Picture: return ext + " picture";
    case Kind::Video: return ext + " video";
    case Kind::Audio: return ext + " audio";
    case Kind::Document: return ext + " document";
    case Kind::Text: return ext.empty() ? "Text" : ext + " text";
    case Kind::Archive: return ext + " archive";
    case Kind::Code: return ext + " source";
    case Kind::Package: return "Package";
    case Kind::Other: break;
    }
    return ext.empty() ? "File" : ext + " file";
}

std::string humanSize(std::uintmax_t bytes) {
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1000 && u < 4) v /= 1000, ++u;
    return u == 0 ? std::format("{} B", bytes) : std::format("{:.{}f} {}", v, v < 10 ? 1 : 0, units[u]);
}

std::string humanTime(std::time_t t) {
    if (t <= 0) return "";
    std::tm tm{}, now{};
    localtime_r(&t, &tm);
    const std::time_t n = std::time(nullptr);
    localtime_r(&n, &now);
    char buf[64];
    if (tm.tm_year == now.tm_year && tm.tm_yday == now.tm_yday) std::strftime(buf, sizeof buf, "Today, %H:%M", &tm);
    else if (tm.tm_year == now.tm_year && tm.tm_yday == now.tm_yday - 1) std::strftime(buf, sizeof buf, "Yesterday, %H:%M", &tm);
    else std::strftime(buf, sizeof buf, tm.tm_year == now.tm_year ? "%-d %b, %H:%M" : "%-d %b %Y", &tm);
    return buf;
}

std::string displayPath(const std::string& p) {
    const std::string home = paths::home();
    if (p == home) return "~";
    if (p.starts_with(home + "/")) return "~" + p.substr(home.size());
    return p;
}

std::string freeName(const std::string& dir, const std::string& name) {
    std::error_code ec;
    if (!fs::exists(fs::path(dir) / name, ec) && !fs::is_symlink(fs::path(dir) / name, ec)) return name;
    // "photo.tar.gz" → "photo (2).tar.gz"; a folder or ".hidden" keeps it whole
    std::string stem = name, ext;
    const size_t dot = name.find('.', name.starts_with(".") ? 1 : 0);
    if (dot != std::string::npos && dot > 0 && !fs::is_directory(fs::path(dir) / name, ec)) {
        stem = name.substr(0, dot);
        ext = name.substr(dot);
    }
    for (int i = 2;; ++i) {
        const std::string candidate = std::format("{} ({}){}", stem, i, ext);
        if (!fs::exists(fs::path(dir) / candidate, ec)) return candidate;
    }
}

std::string thumbnail(const std::string& path, bool make, std::function<void()> ready) {
    const std::string png = paths::home() + "/.cache/material3ui/thumbnails/" + md5Hex("file://" + percentEncode(path, true)) + ".png";
    std::error_code ec;
    // Fresh: newer than the video
    if (fs::exists(png, ec) && fs::last_write_time(png, ec) >= fs::last_write_time(path, ec)) return png;
    if (!make || g_thumbFailed.contains(path) || !haveCommand("ffmpeg")) return "";
    auto& waiters = g_thumbWaiters[path];
    const bool queued = !waiters.empty() || std::ranges::any_of(g_thumbQueue, [&](const auto& q) { return q.first == path; });
    waiters.push_back(std::move(ready));
    if (!queued) g_thumbQueue.emplace_back(path, png);
    nextThumb();
    return "";
}

std::vector<Place> places() {
    const std::string h = paths::home();
    // The user's own names for them (~/.config/user-dirs.dirs), else the usual
    std::map<std::string, std::string> xdg;
    std::ifstream in(h + "/.config/user-dirs.dirs");
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('=');
        if (!line.starts_with("XDG_") || eq == std::string::npos) continue;
        std::string v = line.substr(eq + 1);
        if (v.size() >= 2 && v.front() == '"') v = v.substr(1, v.size() - 2);
        if (v.starts_with("$HOME")) v = h + v.substr(5);
        xdg[line.substr(4, eq - 4)] = v;
    }
    auto dir = [&](const char* key, const char* fallback) { return xdg.contains(key) ? xdg[key] : h + "/" + fallback; };
    std::vector<Place> out = {{"home", "Home", h},
                              {"desktop_windows", "Desktop", dir("DESKTOP_DIR", "Desktop")},
                              {"description", "Documents", dir("DOCUMENTS_DIR", "Documents")},
                              {"download", "Downloads", dir("DOWNLOAD_DIR", "Downloads")},
                              {"music_note", "Music", dir("MUSIC_DIR", "Music")},
                              {"image", "Pictures", dir("PICTURES_DIR", "Pictures")},
                              {"movie", "Videos", dir("VIDEOS_DIR", "Videos")}};
    std::erase_if(out, [](const Place& p) {
        std::error_code ec;
        return p.path != paths::home() && (!fs::is_directory(p.path, ec) || fs::equivalent(p.path, paths::home(), ec));
    });
    return out;
}

std::vector<Place> devices() {
    std::vector<Place> out;
    std::ifstream in("/proc/mounts");
    for (std::string line; std::getline(in, line);) {
        std::istringstream ss(line);
        std::string dev, at, type;
        ss >> dev >> at >> type;
        // Mount points escape spaces as \040
        for (size_t p; (p = at.find("\\040")) != std::string::npos;) at.replace(p, 4, " ");
        if (!at.starts_with("/run/media/") && !at.starts_with("/media/") && !at.starts_with("/mnt/")) continue;
        out.push_back({type == "vfat" || type == "exfat" ? "usb" : "hard_drive", fs::path(at).filename().string(), at});
    }
    out.push_back({"computer", "This computer", "/"});
    return out;
}

// ── Operations ──

void copy(const std::vector<std::string>& sources, const std::string& dir, Done done) {
    std::vector<std::vector<std::string>> cmds;
    std::set<std::string> taken;
    for (const auto& s : sources) {
        std::string name = fs::path(s).filename().string();
        // Pasted where it is: a copy beside it, under a free name
        name = freeName(dir, name);
        while (taken.contains(name)) name = freeName(dir, "_" + name);
        taken.insert(name);
        cmds.push_back({"cp", "-a", "--", s, (fs::path(dir) / name).string()});
    }
    queue("Copying " + items(sources.size()), std::move(cmds), std::move(done));
}

void move(const std::vector<std::string>& sources, const std::string& dir, Done done) {
    std::vector<std::vector<std::string>> cmds;
    std::error_code ec;
    for (const auto& s : sources) {
        if (fs::path(s).parent_path() == fs::path(dir)) continue; // already there
        // Not into itself
        const std::string to = dir + "/";
        if (to.starts_with(s + "/")) continue;
        cmds.push_back({"mv", "-n", "--", s, (fs::path(dir) / freeName(dir, fs::path(s).filename().string())).string()});
    }
    queue("Moving " + items(cmds.size()), std::move(cmds), std::move(done));
}

std::string trashFiles() {
    return trashRoot() + "/files";
}

void trash(const std::vector<std::string>& paths, Done done) {
    const std::string files = trashFiles(), info = trashRoot() + "/info";
    std::error_code ec;
    fs::create_directories(files, ec);
    fs::create_directories(info, ec);
    struct stat trashStat {};
    ::stat(files.c_str(), &trashStat);
    std::vector<std::vector<std::string>> cmds;
    for (const auto& p : paths) {
        struct stat st {};
        if (::lstat(p.c_str(), &st) != 0) continue;
        // On another drive: the trash there, as gio does it
        if (st.st_dev != trashStat.st_dev && haveCommand("gio")) {
            cmds.push_back({"gio", "trash", "--", p});
            continue;
        }
        const std::string name = freeName(files, fs::path(p).filename().string());
        std::ofstream(info + "/" + name + ".trashinfo") << "[Trash Info]\nPath=" << percentEncode(p, true) << "\nDeletionDate=" << isoNow() << "\n";
        cmds.push_back({"mv", "-n", "--", p, files + "/" + name});
    }
    queue("Moving " + items(paths.size()) + " to the trash", std::move(cmds), std::move(done));
}

void remove(const std::vector<std::string>& paths, Done done) {
    if (paths.empty()) return done ? done({}) : void();
    std::vector<std::string> cmd = {"rm", "-rf", "--"};
    cmd.insert(cmd.end(), paths.begin(), paths.end());
    // Deleting from the trash: their .trashinfo goes too
    const std::string files = trashFiles() + "/";
    for (const auto& p : paths)
        if (p.starts_with(files)) cmd.push_back(trashRoot() + "/info/" + fs::path(p).filename().string() + ".trashinfo");
    queue("Deleting " + items(paths.size()), {cmd}, std::move(done));
}

std::string rename(const std::string& path, const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) return "A name can't be empty or have a / in it";
    const fs::path to = fs::path(path).parent_path() / name;
    std::error_code ec;
    if (to == fs::path(path)) return {};
    if (fs::exists(to, ec)) return "There's already something called " + name;
    fs::rename(path, to, ec);
    if (!ec) g_changed.emit();
    return ec ? ec.message() : "";
}

std::string makeFolder(const std::string& dir, const std::string& name, std::string* made) {
    if (name.empty() || name.find('/') != std::string::npos) return "A name can't be empty or have a / in it";
    const fs::path to = fs::path(dir) / name;
    std::error_code ec;
    if (fs::exists(to, ec)) return "There's already something called " + name;
    fs::create_directory(to, ec);
    if (made && !ec) *made = to.string();
    if (!ec) g_changed.emit();
    return ec ? ec.message() : "";
}

std::vector<Trashed> trashed() {
    std::vector<Trashed> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(trashFiles(), ec)) {
        Trashed t;
        t.path = e.path().string();
        t.name = e.path().filename().string();
        std::ifstream in(trashRoot() + "/info/" + t.name + ".trashinfo");
        for (std::string line; std::getline(in, line);) {
            if (line.starts_with("Path=")) t.original = percentDecode(line.substr(5));
            if (line.starts_with("DeletionDate=")) {
                std::tm tm{};
                std::istringstream ds(line.substr(13));
                ds >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
                if (!ds.fail()) t.deleted = std::mktime(&tm);
            }
        }
        if (!t.original.empty()) t.name = fs::path(t.original).filename().string();
        out.push_back(std::move(t));
    }
    std::ranges::sort(out, [](const Trashed& a, const Trashed& b) { return a.deleted > b.deleted; });
    return out;
}

void restore(const std::vector<std::string>& trashedPaths, Done done) {
    const auto all = trashed();
    std::vector<std::vector<std::string>> cmds;
    std::error_code ec;
    for (const auto& p : trashedPaths) {
        const auto it = std::ranges::find(all, p, &Trashed::path);
        if (it == all.end() || it->original.empty()) continue;
        const fs::path where = fs::path(it->original).parent_path();
        fs::create_directories(where, ec);
        cmds.push_back({"mv", "-n", "--", p, (where / freeName(where.string(), fs::path(it->original).filename().string())).string()});
        cmds.push_back({"rm", "-f", "--", trashRoot() + "/info/" + fs::path(p).filename().string() + ".trashinfo"});
    }
    queue("Restoring " + items(cmds.size() / 2), std::move(cmds), std::move(done));
}

void emptyTrash(Done done) {
    std::vector<std::string> cmd = {"rm", "-rf", "--"};
    std::error_code ec;
    for (const char* sub : {"/files", "/info"})
        for (const auto& e : fs::directory_iterator(trashRoot() + sub, ec)) cmd.push_back(e.path().string());
    if (cmd.size() == 3) return done ? done({}) : void();
    queue("Emptying the trash", {cmd}, std::move(done));
}

// ── Archives ──

namespace {
std::string readCommand(const std::string& cmd) {
    std::string out;
    if (FILE* f = popen(cmd.c_str(), "r")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        pclose(f);
    }
    return out;
}
std::string shq(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
} // namespace

bool canExtract(const std::string& path) {
    return kindOf(path) == Kind::Archive || among(lowerExt(path), {".jar", ".cbz", ".cbr", ".apk", ".deb", ".rpm", ".cpio", ".lzma", ".lz4"});
}

std::string archiveStem(const std::string& path) {
    std::string name = fs::path(path).filename().string();
    std::string lower = name;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* ext : {".tar.gz", ".tar.xz", ".tar.zst", ".tar.bz2", ".tar.lz4", ".tar.lzma", ".tgz", ".txz", ".tbz2", ".zip", ".7z", ".rar",
                            ".tar", ".iso", ".gz", ".xz", ".zst", ".bz2", ".jar", ".cbz", ".cbr", ".apk", ".deb", ".rpm", ".cpio", ".lzma", ".lz4"})
        if (lower.size() > std::strlen(ext) && lower.ends_with(ext)) return name.substr(0, name.size() - std::strlen(ext));
    return name;
}

void extract(const std::string& archive, std::function<void(const std::string&, const std::string&)> done) {
    const std::string dir = fs::path(archive).parent_path().string();
    const std::string folder = (fs::path(dir) / freeName(dir, archiveStem(archive))).string();
    queue("Extracting " + fs::path(archive).filename().string(), {{"mkdir", "-p", "--", folder}, {"bsdtar", "-xf", archive, "-C", folder}},
          [done, folder](const std::string& err) {
              // One folder inside named as the archive was: that's the folder
              std::error_code ec;
              std::vector<fs::path> inside;
              for (const auto& e : fs::directory_iterator(folder, ec)) inside.push_back(e.path());
              std::string result = folder;
              if (err.empty() && inside.size() == 1 && fs::is_directory(inside[0], ec)) {
                  const fs::path parent = fs::path(folder).parent_path(), tmp = fs::path(folder).string() + ".m3unwrap";
                  fs::rename(inside[0], tmp, ec);
                  if (!ec) fs::remove(folder, ec);
                  const std::string name = freeName(parent.string(), inside[0].filename().string());
                  if (!ec) fs::rename(tmp, parent / name, ec);
                  if (!ec) result = (parent / name).string();
              }
              if (done) done(err, result);
          });
}

void compress(const std::vector<std::string>& paths, const std::string& name, std::function<void(const std::string&, const std::string&)> done) {
    if (paths.empty()) return;
    const std::string dir = fs::path(paths.front()).parent_path().string();
    const std::string zip = (fs::path(dir) / freeName(dir, name + ".zip")).string();
    std::vector<std::string> cmd = {"bsdtar", "-a", "-cf", zip, "-C", dir, "--"};
    for (const auto& p : paths) cmd.push_back(fs::path(p).filename().string());
    queue("Compressing " + items(paths.size()), {cmd}, [done, zip](const std::string& err) {
        if (done) done(err, zip);
    });
}

// ── Drives ──

std::vector<Drive> drives() {
    std::vector<Drive> out;
    const auto j = nlohmann::json::parse(readCommand("lsblk -J -o NAME,PATH,PKNAME,LABEL,MOUNTPOINT,RM,HOTPLUG,TYPE,FSTYPE,SIZE 2>/dev/null"), nullptr, false);
    if (!j.is_object() || !j.contains("blockdevices")) return out;
    auto str = [](const nlohmann::json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
    auto flag = [](const nlohmann::json& o, const char* k) { return o.contains(k) && o[k].is_boolean() && o[k].get<bool>(); };
    std::function<void(const nlohmann::json&, const nlohmann::json*)> walk = [&](const nlohmann::json& d, const nlohmann::json* parent) {
        const bool removable = flag(d, "rm") || flag(d, "hotplug") || (parent && (flag(*parent, "rm") || flag(*parent, "hotplug")));
        const std::string fstype = str(d, "fstype"), at = str(d, "mountpoint");
        const bool userMount = at.starts_with("/run/media/") || at.starts_with("/media/") || at.starts_with("/mnt/");
        if (!fstype.empty() && fstype != "swap" && fstype != "crypto_LUKS" && (removable || userMount) && !(d.contains("children") && d["children"].is_array())) {
            Drive x;
            x.device = str(d, "path");
            x.disk = parent ? str(*parent, "path") : x.device;
            x.label = str(d, "label");
            x.mountpoint = at;
            x.size = str(d, "size");
            x.fstype = fstype;
            x.removable = removable;
            out.push_back(std::move(x));
        }
        if (d.contains("children") && d["children"].is_array())
            for (const auto& c : d["children"]) walk(c, &d);
    };
    for (const auto& d : j["blockdevices"]) walk(d, nullptr);
    return out;
}

void mount(const std::string& device, std::function<void(const std::string&, const std::string&)> done) {
    run({"udisksctl", "mount", "-b", device}, [done, device](int code, const std::string& out) {
        // "Mounted /dev/sdb1 at /run/media/you/LABEL"
        std::string path;
        if (const size_t at = out.find(" at "); at != std::string::npos) {
            path = out.substr(at + 4);
            while (!path.empty() && (std::isspace(static_cast<unsigned char>(path.back())) || path.back() == '.')) path.pop_back();
        }
        if (code != 0 || path.empty()) return done(code == 0 ? "Mounted, but where wasn't said" : "Couldn't open " + device, "");
        g_changed.emit();
        done("", path);
    });
}

void eject(const Drive& drive, Done done) {
    std::vector<std::vector<std::string>> cmds;
    if (!drive.mountpoint.empty()) cmds.push_back({"udisksctl", "unmount", "-b", drive.device});
    if (drive.removable) cmds.push_back({"udisksctl", "power-off", "-b", drive.disk});
    queue("Ejecting " + (drive.label.empty() ? drive.device : drive.label), std::move(cmds), [done, drive](const std::string& err) {
        // Powering off can fail where unmounting worked (a card reader): still safe
        std::ifstream mounts("/proc/mounts");
        bool still = false;
        for (std::string line; std::getline(mounts, line);)
            if (line.starts_with(drive.device + " ")) still = true;
        if (done) done(still ? "It's still in use: close what has files open on it" : (err.empty() ? "" : std::string()));
    });
}

// ── Servers ──

std::string gvfsDir() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    return (rt && *rt ? std::string(rt) : "/run/user/" + std::to_string(getuid())) + "/gvfs";
}

std::vector<Server> servers() {
    std::vector<Server> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(gvfsDir(), ec)) {
        // "sftp:host=192.168.1.5,user=pi", "ftp:host=x,port=2121"
        const std::string dir = e.path().filename().string();
        const size_t colon = dir.find(':');
        if (colon == std::string::npos) continue;
        const std::string scheme = dir.substr(0, colon);
        std::map<std::string, std::string> kv;
        std::stringstream ss(dir.substr(colon + 1));
        for (std::string part; std::getline(ss, part, ',');)
            if (const size_t eq = part.find('='); eq != std::string::npos) kv[part.substr(0, eq)] = percentDecode(part.substr(eq + 1));
        if (!kv.contains("host")) continue;
        std::string kind = scheme;
        for (auto& c : kind) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const std::string urlScheme = scheme == "dav" && kv["ssl"] == "true" ? "davs" : scheme;
        Server s;
        s.path = e.path().string();
        s.name = kv["host"] + " (" + kind + ")";
        s.url = urlScheme + "://" + (kv.contains("user") ? kv["user"] + "@" : "") + kv["host"] + (kv.contains("port") ? ":" + kv["port"] : "") +
                (kv.contains("prefix") ? kv["prefix"] : "/");
        out.push_back(std::move(s));
    }
    return out;
}

void connect(const std::string& urlIn, const std::string& user, const std::string& password,
             std::function<void(const std::string&, const std::string&)> done) {
    std::string url = urlIn;
    if (url.find("://") == std::string::npos) url = "ftp://" + url; // a bare host: FTP
    // The host, to find its folder afterwards
    std::string host = url.substr(url.find("://") + 3);
    host = host.substr(0, host.find('/'));
    if (const size_t at = host.rfind('@'); at != std::string::npos) host = host.substr(at + 1);
    if (const size_t colon = host.find(':'); colon != std::string::npos) host = host.substr(0, colon);
    std::vector<std::string> cmd = {"gio", "mount"};
    if (user.empty() && url.starts_with("ftp")) cmd.push_back("-a"); // anonymous FTP
    cmd.push_back(url);
    run(cmd, [done, host](int code, const std::string& out) {
        for (const auto& s : servers())
            if (s.path.find("host=" + host) != std::string::npos) {
                g_changed.emit();
                return done("", s.path);
            }
        std::string why = out;
        if (const size_t e = why.rfind("gio: "); e != std::string::npos) why = why.substr(e + 5);
        while (!why.empty() && std::isspace(static_cast<unsigned char>(why.back()))) why.pop_back();
        done(code == 0 ? "Connected, but its folder didn't appear" : why.empty() ? "Couldn't connect" : why, "");
    }, user.empty() ? std::string() : user + "\n" + password + "\n");
}

void disconnect(const Server& server, Done done) {
    queue("Disconnecting " + server.name, {{"gio", "mount", "-u", server.url}}, std::move(done));
}

// ── Properties ──

Info info(const std::string& path) {
    Info x;
    x.path = path;
    x.name = fs::path(path).filename().string();
    if (x.name.empty()) x.name = path;
    struct statx st {};
    if (::statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW, STATX_BASIC_STATS | STATX_BTIME, &st) == 0) {
        x.link = S_ISLNK(st.stx_mode);
        x.modified = st.stx_mtime.tv_sec;
        x.accessed = st.stx_atime.tv_sec;
        if (st.stx_mask & STATX_BTIME) x.created = st.stx_btime.tv_sec;
        x.mode = st.stx_mode & 07777;
        if (const passwd* pw = getpwuid(st.stx_uid)) x.owner = pw->pw_name;
        if (const group* gr = getgrgid(st.stx_gid)) x.group = gr->gr_name;
    }
    std::error_code ec;
    if (x.link) x.linkTarget = fs::read_symlink(path, ec).string();
    x.dir = fs::is_directory(path, ec);
    if (!x.dir) x.size = fs::file_size(path, ec);
    struct stat target {};
    if (::stat(path.c_str(), &target) == 0) x.mode = target.st_mode & 07777;
    x.executable = !x.dir && (x.mode & S_IXUSR);
    x.writable = ::access(path.c_str(), W_OK) == 0;
    x.kind = kindName(kindOf(path, x.dir), path);
    // rwxr-xr-x (755)
    std::string rwx;
    for (int shift = 6; shift >= 0; shift -= 3)
        for (int bit = 2; bit >= 0; --bit) rwx += (x.mode >> (shift + bit) & 1) ? "rwx"[2 - bit] : '-';
    x.permissions = std::format("{} ({:03o})", rwx, x.mode & 0777);
    if (x.dir) {
        int n = 0;
        for (auto it = fs::directory_iterator(path, fs::directory_options::skip_permission_denied, ec); it != fs::directory_iterator() && n < 100000; it.increment(ec)) ++n;
        x.children = n;
    } else {
        x.mime = readCommand("file -b --mime-type -- " + shq(path) + " 2>/dev/null");
        while (!x.mime.empty() && std::isspace(static_cast<unsigned char>(x.mime.back()))) x.mime.pop_back();
    }
    struct statvfs vfs {};
    if (::statvfs(path.c_str(), &vfs) == 0) {
        x.freeSpace = static_cast<std::uintmax_t>(vfs.f_bavail) * vfs.f_frsize;
        x.totalSpace = static_cast<std::uintmax_t>(vfs.f_blocks) * vfs.f_frsize;
    }
    return x;
}

void folderSize(const std::string& path, std::function<void(std::uintmax_t, std::uintmax_t)> done) {
    run({"du", "-sb", "--", path}, [path, done](int, const std::string& bytes) {
        run({"du", "-s", "--inodes", "--", path}, [done, bytes](int, const std::string& inodes) {
            std::uintmax_t b = 0, n = 0;
            try {
                b = std::stoull(bytes);
                n = std::stoull(inodes);
            } catch (...) {
            }
            done(b, n > 0 ? n - 1 : 0); // less the folder itself
        });
    });
}

std::string setExecutable(const std::string& path, bool on) {
    std::error_code ec;
    const auto perms = fs::status(path, ec).permissions();
    // Runnable by whoever may read it
    fs::perms x = fs::perms::none;
    if ((perms & fs::perms::owner_read) != fs::perms::none) x |= fs::perms::owner_exec;
    if ((perms & fs::perms::group_read) != fs::perms::none) x |= fs::perms::group_exec;
    if ((perms & fs::perms::others_read) != fs::perms::none) x |= fs::perms::others_exec;
    fs::permissions(path, x, on ? fs::perm_options::add : fs::perm_options::remove, ec);
    return ec ? ec.message() : "";
}

const std::string& status() {
    return g_status;
}

Signal<>& changed() {
    return g_changed;
}

} // namespace m3::fileops
