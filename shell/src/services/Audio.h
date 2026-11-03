#pragma once

#include "core/Signal.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace m3 {

// Output and input devices through PipeWire's PulseAudio server (libpulse).
// libpulse runs its own thread; every update arrives on the event loop as a
// snapshot, so callers only ever see consistent state.
class Audio {
public:
    struct Device {
        std::string name;        // pulse name, used to select it
        std::string description; // human readable
        std::vector<float> volumes; // per channel, 1.0 = 100%
        std::vector<std::string> channels; // "Front Left", …
        bool muted = false;
        bool isDefault = false;

        float volume() const; // loudest channel
        float balance() const; // -1 left … 1 right (stereo)
    };

    // An app's stream: playing to an output, or recording from an input
    struct Stream {
        uint32_t index = 0;
        std::string app, title; // "Firefox", "YouTube — …"
        std::string icon;       // its icon name, else its program's name
        std::string device;     // the output (or input) it's on
        std::vector<float> volumes;
        bool muted = false, paused = false;
        float volume() const;
    };

    static Audio& get();

    bool ready() const { return m_ready; }
    const Device* sink() const;   // default output
    const Device* source() const; // default input
    const std::vector<Device>& sinks() const { return m_sinks; }
    const std::vector<Device>& sources() const { return m_sources; }

    void setVolume(float v, bool input = false);   // keeps the balance
    void stepVolume(float delta);
    void setBalance(float b);                      // default output
    void setChannelVolume(size_t channel, float v); // default output
    void setMuted(bool m, bool input = false);
    void toggleMuted(bool input = false);
    void setDefault(const std::string& name, bool input = false);

    // Apps playing (one per stream), and apps recording from a microphone
    // (not from an output's monitor: a screen recorder taking the sound)
    const std::vector<Stream>& playing() const { return m_playing; }
    const std::vector<Stream>& recording() const { return m_recording; }
    void setStreamVolume(uint32_t index, float v);
    void setStreamMuted(uint32_t index, bool muted);
    void moveStream(uint32_t index, const std::string& sink); // to another output

    Signal<> changed;

    ~Audio();

private:
    Audio();
    struct Impl;
    std::shared_ptr<Impl> m_impl;

    bool m_ready = false;
    std::vector<Device> m_sinks, m_sources;
    std::vector<Stream> m_playing, m_recording;
    friend struct Impl;
};

} // namespace m3
