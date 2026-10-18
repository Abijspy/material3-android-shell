#pragma once

#include "core/EventLoop.h"

#include <functional>
#include <map>
#include <string>

namespace m3 {

// inotify on parent directories, so files replaced by rename (how editors,
// Matugen and the settings app save) are still seen.
class FileWatcher : public EventSource {
public:
    static FileWatcher& get();
    ~FileWatcher() override;

    void watch(const std::string& path, std::function<void()> onChange);
    // Any file created, changed, moved or removed directly in `dir`
    void watchDir(const std::string& dir, std::function<void()> onChange);

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    FileWatcher();
    void add(const std::string& dir, std::string name, std::function<void()> onChange);

    struct Entry {
        std::string name; // empty: the whole directory
        std::function<void()> fn;
        EventLoop::TimerId debounce = 0;
    };

    int m_fd = -1;
    std::map<int, std::string> m_dirs;       // wd → dir
    std::multimap<std::string, Entry> m_files; // dir → watched names
};

} // namespace m3
