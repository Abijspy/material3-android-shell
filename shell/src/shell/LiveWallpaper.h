#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <cstdint>
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace m3::shell {

// A video as the wallpaper (Wallpaper & style → Live wallpaper), drawn by
// the shell's own wallpaper surface, so desktop widgets and their blur work
// over it as ever. ffmpeg decodes it at the screen's size and 30 frames a
// second (on the GPU through VA-API where it can), looping, and pauses
// (SIGSTOP: nothing spent) while the screen is locked and, with the switch,
// on battery. A frame of it becomes the still wallpaper: the colours, the
// lock screen. /background/live/enabled, /path, /pauseOnBattery
class LiveWallpaper {
public:
    static LiveWallpaper& get();
    void start();
    bool on() const;            // switched on, with a video that's there
    bool paused() const;
    std::string video() const;
    void setVideo(const std::string& path); // and on, its frame the still wallpaper
    void setOn(bool on);                    // off: the still wallpaper from before
    Signal<> changed;

private:
    LiveWallpaper() = default;
    void takeFrame(const std::string& path);
    bool m_paused = false;
};

// One screen's decoder: ffmpeg's raw BGRA frames from a pipe, the newest kept
class VideoStream : public EventSource {
public:
    std::function<void()> onFrame;
    VideoStream(std::string path, int width, int height);
    ~VideoStream() override;
    void pause(bool paused);
    int width() const { return m_w; }
    int height() const { return m_h; }
    // The newest whole frame (width × height × 4, BGRA), once there is one
    const std::vector<uint8_t>& frame() const { return m_frame; }
    bool fresh() const { return m_fresh; } // not drawn yet
    void drawn() { m_fresh = false; }

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    bool launch(bool gpu);
    void stop();
    std::string m_path;
    int m_w, m_h;
    size_t m_bytes; // a frame's
    pid_t m_pid = -1;
    int m_fd = -1;
    bool m_gpu = true, m_paused = false, m_fresh = false, m_any = false;
    std::vector<uint8_t> m_frame, m_buf;
    size_t m_have = 0; // of the frame being read
};

} // namespace m3::shell
