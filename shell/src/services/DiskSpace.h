#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace m3 {

// Tells you when a disk runs low: your home folder's and the system's (one
// look when they're the same), every five minutes. Low is under a twentieth
// of the disk (but never more than 20 GiB) or under 1 GiB, whichever is
// more. Once each time it gets low, and again if what's left halves; the
// notification offers to empty the trash and opens Settings → Storage.
// Also the disk_space_low hook. Settings → Storage switches it off
class DiskSpace {
public:
    static DiskSpace& get();
    void start();                      // once
    std::function<void()> openStorage; // set by the shell: Settings → Storage

private:
    DiskSpace() = default;
    void check();
    struct Seen {
        bool low = false;
        uint64_t warnedFree = 0; // what was left when it last said so
    };
    std::map<std::string, Seen> m_seen; // by mount point
    bool m_started = false;
};

} // namespace m3
