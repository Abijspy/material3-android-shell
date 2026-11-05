#include "services/Trash.h"

#include "core/FileWatcher.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Sounds.h"

#include <cstdlib>
#include <filesystem>

namespace m3 {

namespace fs = std::filesystem;

Trash& Trash::get() {
    static Trash t;
    return t;
}

std::string Trash::dir() const {
    const char* data = std::getenv("XDG_DATA_HOME");
    return (data && *data ? std::string(data) : paths::home() + "/.local/share") + "/Trash";
}

void Trash::start() {
    if (m_started) return;
    m_started = true;
    // Made as a file manager would on its first delete, so there's one to watch
    std::error_code ec;
    fs::create_directories(dir() + "/files", ec);
    fs::create_directories(dir() + "/info", ec);
    recount();
    FileWatcher::get().watchDir(dir() + "/files", [this] { recount(); });
}

void Trash::recount() {
    int n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir() + "/files", ec), end; !ec && it != end; it.increment(ec)) ++n;
    if (n == m_count) return;
    m_count = n;
    changed.emit();
}

void Trash::open() const {
    // gvfs's trash:/// lists where each came from and can put it back;
    // without it, the folder itself
    const std::string files = dir() + "/files";
    run({"gio", "open", "trash:///"}, [files](int code, const std::string&) {
        if (code != 0) run({"xdg-open", files}, nullptr);
    });
}

void Trash::empty() {
    const std::string d = dir();
    // Read-only folders inside (a Go module cache, say) made writable first,
    // so everything goes
    run({"sh", "-c", "chmod -R u+rwX -- \"$@\" 2>/dev/null; find \"$@\" -mindepth 1 -delete 2>/dev/null; true", "sh", d + "/files",
         d + "/info", d + "/expunged"},
        [this, d](int, const std::string&) {
            std::error_code ec;
            fs::remove(d + "/directorysizes", ec);
            recount();
            Sounds::get().play(Sounds::Event::Trash);
        });
}

} // namespace m3
