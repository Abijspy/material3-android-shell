#pragma once

#include "core/Signal.h"

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

    Signal<> changed;

    ~Audio();

private:
    Audio();
    struct Impl;
    std::shared_ptr<Impl> m_impl;

    bool m_ready = false;
    std::vector<Device> m_sinks, m_sources;
    friend struct Impl;
};

} // namespace m3
