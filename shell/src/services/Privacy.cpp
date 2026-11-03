#include "services/Privacy.h"

#include "services/Audio.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sys/inotify.h>
#include <unistd.h>

namespace m3 {

namespace fs = std::filesystem;

namespace {
// "firefox" → "Firefox"
std::string pretty(std::string name) {
    if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}
} // namespace

Privacy& Privacy::get() {
    static Privacy instance;
    return instance;
}

Privacy::~Privacy() {
    if (m_fd >= 0) close(m_fd);
}

void Privacy::start() {
    if (m_fd >= 0) return;
    // The microphone follows the audio streams
    std::vector<std::string> mic;
    m_audio = Audio::get().changed.connect([this, mic]() mutable {
        auto now = micApps();
        if (now == mic) return;
        mic = std::move(now);
        changed.emit();
    });
    // The cameras: told of each open and close
    m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0) return;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/dev", ec))
        if (e.path().filename().string().starts_with("video"))
            inotify_add_watch(m_fd, e.path().c_str(), IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
    EventLoop::get().add(this);
    scanCameras();
}

std::vector<std::string> Privacy::micApps() const {
    std::vector<std::string> out;
    for (const auto& s : Audio::get().recording())
        if (!s.paused && std::ranges::find(out, s.app) == out.end()) out.push_back(s.app);
    return out;
}

void Privacy::dispatch(short) {
    alignas(inotify_event) char buf[4096];
    while (read(m_fd, buf, sizeof buf) > 0) {
    }
    // Probing (udev, PipeWire listing devices) opens and closes at once:
    // look once things settle
    EventLoop::get().debounce(m_scan, 400, [this] { scanCameras(); });
}

void Privacy::scanCameras() {
    std::vector<std::string> apps;
    std::error_code ec;
    for (const auto& proc : fs::directory_iterator("/proc", ec)) {
        const std::string pid = proc.path().filename().string();
        if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
        std::error_code fec;
        for (const auto& fd : fs::directory_iterator(proc.path() / "fd", fec)) {
            std::error_code lec;
            const std::string target = fs::read_symlink(fd.path(), lec).string();
            if (!target.starts_with("/dev/video")) continue;
            std::string comm;
            std::getline(std::ifstream(proc.path() / "comm"), comm);
            comm = pretty(comm);
            if (!comm.empty() && std::ranges::find(apps, comm) == apps.end()) apps.push_back(comm);
            break;
        }
    }
    if (apps == m_camera) return;
    m_camera = std::move(apps);
    changed.emit();
}

} // namespace m3
