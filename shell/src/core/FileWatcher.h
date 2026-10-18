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

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    FileWatcher();

    struct Entry {
        std::string name;
        std::function<void()> fn;
        EventLoop::TimerId debounce = 0;
    };

    int m_fd = -1;
    std::map<int, std::string> m_dirs;       // wd → dir
    std::multimap<std::string, Entry> m_files; // dir → watched names
};

} // namespace m3
