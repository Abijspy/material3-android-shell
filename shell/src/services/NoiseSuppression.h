#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <string>
#include <sys/types.h>

namespace m3 {

// Microphone noise suppression (Settings → Sound → Microphone): RNNoise
// (noise-suppression-for-voice's LADSPA plugin) in a PipeWire filter chain
// run as its own process, placed by WirePlumber in front of the
// microphone as a smart filter, so apps recording from it get the voice
// without the fan or the keyboard. /microphone/noiseSuppression,
// /microphone/noiseStrength (the voice detection's threshold, %)
class NoiseSuppression {
public:
    static NoiseSuppression& get();
    static bool pluginInstalled();
    static constexpr const char* kPlugin = "/usr/lib/ladspa/librnnoise_ladspa.so";
    static constexpr const char* kPackage = "noise-suppression-for-voice";
    void start();
    bool on() const;
    int strength() const;
    bool running() const { return m_pid > 0; }
    // The chain's configuration (written to the runtime folder when on)
    static std::string config(int strength);
    Signal<> changed;

private:
    NoiseSuppression() = default;
    void restart();
    void stop();
    pid_t m_pid = -1;
    std::string m_applied;
    EventLoop::TimerId m_soon = 0;
};

} // namespace m3
