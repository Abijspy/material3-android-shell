#pragma once

#include "core/Signal.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace m3::fileops {

// What the built-in Files app and image viewer need from the filesystem:
// listing a folder, telling kinds of files apart, the places in the side
// bar, and the operations (copy, move, trash, delete, restore), which run
// as child processes (cp, mv, rm) so a big copy never holds up the shell.
// The trash is the freedesktop one other file managers share
// (~/.local/share/Trash, with an .trashinfo for each file).

struct Entry {
    std::string path, name;
    bool dir = false, link = false, hidden = false;
    std::uintmax_t size = 0; // files; folders count 0
    std::time_t modified = 0;
};

// A folder's entries (hidden ones too when `hidden`); "" error when it reads
std::vector<Entry> list(const std::string& dir, bool hidden, std::string* error = nullptr);

enum class Kind { Folder, Picture, Video, Audio, Document, Text, Archive, Code, Package, Other };
Kind kindOf(const std::string& path, bool dir = false);
std::string kindIcon(Kind kind, const std::string& path = {}); // a Material Symbol
std::string kindName(Kind kind, const std::string& path);     // "PNG picture", "Folder"
bool isPicture(const std::string& path);
// gdk-pixbuf can read it as it is (PNG, JPEG, WebP, AVIF, HEIC, JXL, TIFF…)
bool nativePicture(const std::string& path);
// A file the shell can draw for a picture of any format: the picture itself,
// or (PSD, XCF, camera RAW, JPEG 2000…) a PNG ImageMagick made of it, kept
// in ~/.cache/material3ui/converted. "" while it's made (`ready` runs then),
// and with `failed` set when it can't be
std::string renderable(const std::string& path, std::function<void()> ready = {}, bool* failed = nullptr);

std::string humanSize(std::uintmax_t bytes);   // "4.2 MB"
std::string humanTime(std::time_t t);          // "Today, 14:05", "3 Nov 2026"
std::string displayPath(const std::string& p); // ~ for the home folder

// A name free in `dir`: `name`, else "name (2).ext", "name (3).ext"…
std::string freeName(const std::string& dir, const std::string& name);

// The thumbnail for a video, from the shell's cache ("" while there's none;
// `make` asks ffmpeg for one, and `ready` runs when it's there)
std::string thumbnail(const std::string& path, bool make, std::function<void()> ready = {});

struct Place {
    std::string icon, name, path;
};
std::vector<Place> places();  // Home, Desktop, Documents… (those that exist)
std::vector<Place> devices(); // drives mounted under /run/media and /media, and the system

// Operations: `done` runs on the event loop with an error ("" when all went)
using Done = std::function<void(const std::string& error)>;
void copy(const std::vector<std::string>& sources, const std::string& dir, Done done = {});
void move(const std::vector<std::string>& sources, const std::string& dir, Done done = {});
void trash(const std::vector<std::string>& paths, Done done = {});
void remove(const std::vector<std::string>& paths, Done done = {}); // for good
std::string rename(const std::string& path, const std::string& name); // the error, "" when renamed
std::string makeFolder(const std::string& dir, const std::string& name, std::string* made = nullptr);

// The trash
std::string trashFiles(); // ~/.local/share/Trash/files
struct Trashed {
    std::string path, name, original; // path: in the trash; original: where it was
    std::time_t deleted = 0;
};
std::vector<Trashed> trashed();
void restore(const std::vector<std::string>& trashedPaths, Done done = {});
void emptyTrash(Done done = {});

// Archives, through bsdtar (libarchive, which pacman itself needs): zip,
// tar (gz, xz, zst, bz2), 7z, rar, iso and more come apart; ZIPs are made
bool canExtract(const std::string& path);
std::string archiveStem(const std::string& path); // "photos.tar.gz" → "photos"
// Into a new folder beside it named for it; `done` gets the error and the folder
void extract(const std::string& archive, std::function<void(const std::string& error, const std::string& folder)> done = {});
// `paths` (all in one folder) into `<dir>/<name>.zip`
void compress(const std::vector<std::string>& paths, const std::string& name, std::function<void(const std::string& error, const std::string& zip)> done = {});

// Drives (udisks2): USB sticks, cards, other disks, mounted or not
struct Drive {
    std::string device, disk, label, mountpoint, size, fstype; // disk: the whole device it's on
    bool removable = false;
};
std::vector<Drive> drives();
void mount(const std::string& device, std::function<void(const std::string& error, const std::string& path)> done);
// Unmounted and, when it's removable, powered off: safe to pull out
void eject(const Drive& drive, Done done);

// Servers (gvfs): ftp://, sftp://, dav(s):// mounted under
// $XDG_RUNTIME_DIR/gvfs, and the folders they show as there
struct Server {
    std::string name, url, path; // name: "example.com (FTP)"
};
std::vector<Server> servers();
std::string gvfsDir();
void connect(const std::string& url, const std::string& user, const std::string& password,
             std::function<void(const std::string& error, const std::string& path)> done);
void disconnect(const Server& server, Done done);

// Properties: what stat and statx say, put in words
struct Info {
    std::string path, name, kind, owner, group, permissions, linkTarget, mime;
    std::uintmax_t size = 0;
    std::time_t modified = 0, accessed = 0, created = 0; // created: 0 when the filesystem doesn't say
    unsigned mode = 0;
    bool dir = false, link = false, executable = false, writable = false;
    std::uintmax_t freeSpace = 0, totalSpace = 0; // the drive it's on
    int children = -1; // a folder's entries
};
Info info(const std::string& path);
// A folder's whole size and file count, from du (`done` on the loop)
void folderSize(const std::string& path, std::function<void(std::uintmax_t bytes, std::uintmax_t files)> done);
std::string setExecutable(const std::string& path, bool on); // the error

// What's under way ("Copying 3 items…"), for the windows' status lines
const std::string& status();
Signal<>& changed(); // an operation began or ended (folders may have changed)

} // namespace m3::fileops
