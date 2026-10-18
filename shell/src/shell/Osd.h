#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "wayland/LayerSurface.h"

#include <functional>
#include <memory>
#include <string>

namespace m3::shell {

// Volume and brightness indicator: a pill near the bottom edge that appears
// on change and fades out. Click-through.
class Osd {
public:
    explicit Osd(std::function<wl::Output*()> output);

    void show(const std::string& icon, float value, const std::string& label);

private:
    std::function<wl::Output*()> m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    class Bar;
    ui::Icon* m_icon = nullptr;
    Bar* m_bar = nullptr;
    ui::Label* m_label = nullptr;
    ui::Flex* m_pill = nullptr;
    EventLoop::TimerId m_hide = 0, m_destroy = 0;

    // Last seen state, so only real changes show the OSD
    float m_volume = -1, m_brightness = -1;
    bool m_muted = false, m_audioSeen = false;
    std::string m_sink;
    Connection m_audio, m_light;
};

} // namespace m3::shell
