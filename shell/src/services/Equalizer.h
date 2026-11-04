#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <array>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace m3 {

// A ten-band equaliser for what you hear (Settings → Sound → Equaliser):
// a PipeWire filter chain run as its own process (pipewire -c), placed by
// WirePlumber in front of the output as a smart filter, so the output you
// pick stays the default. /equalizer/enabled, /equalizer/band<Hz> in dB
// (−12…12); a change restarts the chain once you stop moving the slider
class Equalizer {
public:
    static constexpr size_t kBands = 10;
    static const std::array<int, kBands>& frequencies(); // 31 … 16000 Hz
    static std::string label(int hz);                    // "31", "1k"
    static std::string key(int hz);                      // "/equalizer/band31"
    // Flat, Bass, Treble, Vocal, Rock, Classical
    static const std::vector<std::pair<std::string, std::array<double, kBands>>>& presets();

    static Equalizer& get();
    void start();
    bool on() const;
    bool running() const { return m_pid > 0; }
    void setPreset(const std::array<double, kBands>& gains);
    std::array<double, kBands> gains() const;
    Signal<> changed;

private:
    Equalizer() = default;
    void restart();
    void stop();
    pid_t m_pid = -1;
    std::string m_applied; // the gains running, as written
    EventLoop::TimerId m_soon = 0;
};

} // namespace m3
