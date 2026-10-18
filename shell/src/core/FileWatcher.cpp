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

void FileWatcher::watch(const std::string& path, std::function<void()> onChange) {
    if (m_fd < 0) return;
    const std::filesystem::path p(path);
    const std::string dir = p.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int wd = inotify_add_watch(m_fd, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (wd < 0) return;
    m_dirs[wd] = dir;
    m_files.emplace(dir, Entry{p.filename(), std::move(onChange)});
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
                if (it->second.name != ev->name) continue;
                // Writers save in bursts; act once they settle
                auto& entry = it->second;
                EventLoop::get().debounce(entry.debounce, 120, entry.fn);
            }
        }
    }
}

} // namespace m3
