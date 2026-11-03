#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <string>
#include <vector>

namespace m3 {

// Who's using the microphone or the camera, for the bar's privacy chip (as
// Android's green indicator). The microphone: apps recording from an input
// (Audio::recording(), paused ones left out). The camera: /dev/video* opened
// (inotify), then whoever holds one found in /proc
class Privacy : public EventSource {
public:
    static Privacy& get();
    ~Privacy() override;
    void start();

    std::vector<std::string> micApps() const;
    const std::vector<std::string>& cameraApps() const { return m_camera; }
    bool micInUse() const { return !micApps().empty(); }
    bool cameraInUse() const { return !m_camera.empty(); }

    Signal<> changed;

    int fd() const override { return m_fd; }
    void dispatch(short revents) override;

private:
    Privacy() = default;
    void scanCameras();
    int m_fd = -1;
    std::vector<std::string> m_camera;
    EventLoop::TimerId m_scan = 0;
    Connection m_audio;
};

} // namespace m3
