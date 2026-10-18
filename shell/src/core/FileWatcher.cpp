#include "core/FileWatcher.h"

#include <filesystem>
#include <sys/inotify.h>
#include <unistd.h>

namespace m3 {

FileWatcher& FileWatcher::get() {
    static FileWatcher instance;
    return instance;
}

FileWatcher::FileWatcher() : m_fd(inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {
    if (m_fd >= 0) EventLoop::get().add(this);
}

FileWatcher::~FileWatcher() {
    if (m_fd >= 0) close(m_fd);
}

namespace {
constexpr uint32_t kWritten = IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE;
constexpr uint32_t kRemoved = IN_DELETE | IN_MOVED_FROM;
} // namespace

void FileWatcher::watch(const std::string& path, std::function<void()> onChange) {
    const std::filesystem::path p(path);
    add(p.parent_path(), p.filename(), std::move(onChange));
}

void FileWatcher::watchDir(const std::string& dir, std::function<void()> onChange) {
    add(dir, "", std::move(onChange));
}

void FileWatcher::add(const std::string& dir, std::string name, std::function<void()> onChange) {
    if (m_fd < 0) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // One mask per directory (re-adding replaces it), so always ask for both
    const int wd = inotify_add_watch(m_fd, dir.c_str(), kWritten | kRemoved);
    if (wd < 0) return;
    m_dirs[wd] = dir;
    m_files.emplace(dir, Entry{std::move(name), std::move(onChange)});
}

void FileWatcher::dispatch(short) {
    alignas(inotify_event) char buf[4096];
    for (;;) {
        const ssize_t n = read(m_fd, buf, sizeof buf);
        if (n <= 0) break;
        for (char* ptr = buf; ptr < buf + n;) {
            auto* ev = reinterpret_cast<inotify_event*>(ptr);
            ptr += sizeof(inotify_event) + ev->len;
            auto dir = m_dirs.find(ev->wd);
            if (dir == m_dirs.end() || !ev->len) continue;
            const auto range = m_files.equal_range(dir->second);
            for (auto it = range.first; it != range.second; ++it) {
                // Single files only care about new contents; directories about any change
                if (!it->second.name.empty() && (it->second.name != ev->name || !(ev->mask & kWritten))) continue;
                // Writers save in bursts; act once they settle
                auto& entry = it->second;
                EventLoop::get().debounce(entry.debounce, 120, entry.fn);
            }
        }
    }
}

} // namespace m3
