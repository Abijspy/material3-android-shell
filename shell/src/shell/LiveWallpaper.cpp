#include "shell/LiveWallpaper.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Network.h"
#include "services/Wallpaper.h"
#include "shell/LockScreen.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace m3::shell {

// ── LiveWallpaper ──

LiveWallpaper& LiveWallpaper::get() {
    static LiveWallpaper instance;
    return instance;
}

std::string LiveWallpaper::video() const {
    return Config::get().value<std::string>("/background/live/path", "");
}

bool LiveWallpaper::on() const {
    if (!Config::get().value<bool>("/background/live/enabled", false)) return false;
    std::error_code ec;
    const std::string v = video();
    return !v.empty() && std::filesystem::exists(v, ec);
}

bool LiveWallpaper::paused() const {
    return m_paused;
}

void LiveWallpaper::start() {
    // Paused while locked, and on battery with the switch
    auto check = [this] {
        const auto& p = Power::get();
        const bool battery = p.hasBattery() && !p.pluggedIn() && Config::get().value<bool>("/background/live/pauseOnBattery", true);
        const bool paused = LockScreen::get().locked() || battery;
        if (paused == m_paused) return;
        m_paused = paused;
        changed.emit();
    };
    Power::get().changed.connectForever(check);
    LockScreen::get().lockedChanged.connectForever(check);
    // Switched, or another video: the wallpapers start again
    auto shape = std::make_shared<std::string>();
    Config::get().changed.connectForever([this, shape, check] {
        const std::string now = std::format("{}|{}|{}", on(), video(), Config::get().value<bool>("/background/live/pauseOnBattery", true));
        check();
        if (now == *shape) return;
        *shape = now;
        changed.emit();
    });
    check();
}

void LiveWallpaper::takeFrame(const std::string& path) {
    // A frame a second in: the still wallpaper, for the colours and the lock screen
    const std::string frame = paths::stateDir() + "/live-wallpaper.jpg";
    std::error_code ec;
    std::filesystem::create_directories(paths::stateDir(), ec);
    run({"ffmpeg", "-loglevel", "error", "-y", "-ss", "1", "-i", path, "-frames:v", "1", "-q:v", "2", frame}, [frame](int code, const std::string&) {
        if (code != 0) return;
        Images::get().forget(frame); // the same file, a new picture
        Wallpaper::get().set(frame);
    });
}

void LiveWallpaper::setVideo(const std::string& path) {
    auto& c = Config::get();
    // The still wallpaper from before, to go back to when it's off
    const std::string still = Wallpaper::get().path();
    if (!still.ends_with("/live-wallpaper.jpg")) c.set("/background/live/previous", still);
    c.set("/background/live/path", path);
    c.set("/background/live/enabled", true);
    takeFrame(path);
}

void LiveWallpaper::setOn(bool on) {
    auto& c = Config::get();
    c.set("/background/live/enabled", on);
    if (on && !video().empty()) {
        const std::string still = Wallpaper::get().path();
        if (!still.ends_with("/live-wallpaper.jpg")) c.set("/background/live/previous", still);
        takeFrame(video());
        return;
    }
    const std::string before = c.value<std::string>("/background/live/previous", "");
    std::error_code ec;
    if (!on && !before.empty() && std::filesystem::exists(before, ec)) Wallpaper::get().set(before);
}

// ── VideoStream ──

VideoStream::VideoStream(std::string path, int width, int height)
    : m_path(std::move(path)), m_w(width), m_h(height), m_bytes(static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
    m_buf.resize(m_bytes);
    if (!launch(true)) launch(false);
}

VideoStream::~VideoStream() {
    stop();
}

bool VideoStream::launch(bool gpu) {
    m_gpu = gpu;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return false;
    // Filled to the screen (cropped, not stretched), looping, at its own pace
    const std::string size = std::format("{}:{}", m_w, m_h);
    const std::string filter = gpu ? std::format("scale_vaapi=w={}:h={}:force_original_aspect_ratio=increase:format=bgra,hwdownload,format=bgra,"
                                                 "crop={},fps=30",
                                                 m_w, m_h, size)
                                   : std::format("scale={}:force_original_aspect_ratio=increase,crop={},fps=30,format=bgra", size, size);
    // Through setpriv: the kernel ends it with the shell, however that ends
    // (a paused one would otherwise wait, stopped, for ever)
    std::vector<std::string> args;
    if (haveCommand("setpriv")) args = {"setpriv", "--pdeathsig", "KILL", "--"};
    args.insert(args.end(), {"ffmpeg", "-loglevel", "error", "-nostdin"});
    if (gpu) args.insert(args.end(), {"-hwaccel", "vaapi", "-hwaccel_output_format", "vaapi"});
    args.insert(args.end(), {"-re", "-stream_loop", "-1", "-i", m_path, "-an", "-vf", filter, "-f", "rawvideo", "-"});
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    const int err = posix_spawnp(&m_pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (err != 0) {
        close(fds[0]);
        m_pid = -1;
        return false;
    }
    m_fd = fds[0];
    fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
    m_have = 0;
    EventLoop::get().add(this);
    if (m_paused) kill(m_pid, SIGSTOP);
    return true;
}

void VideoStream::stop() {
    if (m_fd >= 0) {
        EventLoop::get().remove(this);
        close(m_fd);
        m_fd = -1;
    }
    if (m_pid > 0) {
        kill(m_pid, SIGKILL);
        waitpid(m_pid, nullptr, 0); // ours: the shell's reaper leaves it alone
        m_pid = -1;
    }
}

void VideoStream::pause(bool paused) {
    if (paused == m_paused) return;
    m_paused = paused;
    if (m_pid > 0) kill(m_pid, paused ? SIGSTOP : SIGCONT);
}

void VideoStream::dispatch(short) {
    for (;;) {
        const ssize_t n = read(m_fd, m_buf.data() + m_have, m_bytes - m_have);
        if (n > 0) {
            m_have += static_cast<size_t>(n);
            if (m_have == m_bytes) {
                // A whole frame: the newest one, kept (older ones dropped)
                m_frame.swap(m_buf);
                m_buf.resize(m_bytes);
                m_have = 0;
                m_fresh = m_any = true;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) break;
        // It ended: on the GPU before a frame, the CPU instead; else it failed
        const bool retry = m_gpu && !m_any;
        stop();
        if (retry) {
            log::info("live wallpaper: decoding on the CPU");
            launch(false);
        } else {
            log::warn("live wallpaper: ffmpeg stopped ({})", m_path);
        }
        return;
    }
    if (m_fresh && onFrame) onFrame();
}

} // namespace m3::shell
