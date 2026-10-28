#include "services/NightLight.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Process.h"
#include "services/Hyprland.h"
#include "wayland/Display.h"

#include "wlr-gamma-control-unstable-v1-client-protocol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

namespace m3 {

struct NightLight::Control {
    wl::Output* output = nullptr;
    zwlr_gamma_control_v1* control = nullptr;
    uint32_t size = 0; // the ramp's length, once the compositor says
    bool failed = false;
    ~Control() {
        if (control) zwlr_gamma_control_v1_destroy(control);
    }
};

namespace {

// One ramp per channel, each `size` long, scaled down by its share
void setGamma(NightLight::Control& c, int kelvin) {
    if (!c.control || c.failed || c.size == 0) return;
    double r, g, b;
    NightLight::tint(kelvin, r, g, b);
    const size_t n = c.size;
    std::vector<uint16_t> ramps(n * 3);
    for (size_t i = 0; i < n; ++i) {
        const double v = static_cast<double>(i) / static_cast<double>(n - 1) * 65535.0;
        ramps[i] = static_cast<uint16_t>(std::lround(v * r));
        ramps[n + i] = static_cast<uint16_t>(std::lround(v * g));
        ramps[2 * n + i] = static_cast<uint16_t>(std::lround(v * b));
    }
    const int fd = memfd_create("material3ui-gamma", MFD_CLOEXEC);
    if (fd < 0) return;
    const size_t bytes = ramps.size() * sizeof(uint16_t);
    if (::write(fd, ramps.data(), bytes) == static_cast<ssize_t>(bytes) && lseek(fd, 0, SEEK_SET) == 0)
        zwlr_gamma_control_v1_set_gamma(c.control, fd);
    close(fd);
    wl_display_flush(wl::Display::get().display());
}

const zwlr_gamma_control_v1_listener kListener = {
    .gamma_size =
        [](void* data, zwlr_gamma_control_v1*, uint32_t size) {
            auto* c = static_cast<NightLight::Control*>(data);
            c->size = size;
            setGamma(*c, NightLight::get().temperature());
        },
    .failed =
        [](void* data, zwlr_gamma_control_v1*) {
            // Another program holds the screen's gamma (or it has none)
            auto* c = static_cast<NightLight::Control*>(data);
            c->failed = true;
            log::warn("night light: {} won't take a gamma ramp", c->output ? c->output->name : "?");
        },
};

} // namespace

NightLight& NightLight::get() {
    static NightLight instance;
    return instance;
}

bool NightLight::on() const {
    return Config::get().value<bool>("/nightLight/enabled", false);
}

int NightLight::temperature() const {
    return std::clamp(Config::get().value<int>("/nightLight/temperature", 4500), 1000, 6500);
}

bool NightLight::native() const {
    return !Hyprland::get().available();
}

void NightLight::start() {
    // Settings (and the Quick Settings tile) change the config: follow it
    Config::get().changed.connectForever([this] { apply(); });
    if (!native()) {
        // hyprsunset outlives the shell: started only when it isn't running,
        // and left alone (yours, maybe) until night light is switched
        m_appliedOn = on() ? 1 : 0;
        m_appliedK = temperature();
        if (on()) spawn(std::format("pgrep -x hyprsunset >/dev/null || hyprsunset -t {}", m_appliedK));
        return;
    }
    apply();
}

void NightLight::set(bool enabled) {
    Config::get().set("/nightLight/enabled", enabled);
}

void NightLight::setTemperature(int kelvin) {
    Config::get().set("/nightLight/temperature", std::clamp(kelvin, 1000, 6500));
}

void NightLight::apply() {
    const int now = on() ? 1 : 0, k = temperature();
    if (now == m_appliedOn && (!now || k == m_appliedK)) return;
    m_appliedOn = now;
    m_appliedK = k;
    if (native()) applyGamma(now);
    else if (now) spawn(std::format("pkill -x hyprsunset; hyprsunset -t {}", k));
    else spawn("pkill -x hyprsunset");
    changed.emit();
}

void NightLight::applyGamma(bool enabled) {
    auto* manager = static_cast<zwlr_gamma_control_manager_v1*>(wl::Display::get().gammaManager());
    if (!enabled || !manager) {
        // Each screen's own gamma back
        m_controls.clear();
        wl_display_flush(wl::Display::get().display());
        if (enabled) log::warn("night light: the compositor has no gamma control");
        return;
    }
    // A control per screen; the ones there already take the new warmth
    for (auto& o : wl::Display::get().outputs()) {
        if (!o->ready) continue;
        auto it = std::ranges::find(m_controls, o.get(), &Control::output);
        if (it != m_controls.end()) {
            setGamma(**it, temperature());
            continue;
        }
        auto c = std::make_unique<Control>();
        c->output = o.get();
        c->control = zwlr_gamma_control_manager_v1_get_gamma_control(manager, o->handle);
        zwlr_gamma_control_v1_add_listener(c->control, &kListener, c.get());
        m_controls.push_back(std::move(c));
    }
    wl_display_flush(wl::Display::get().display());
}

void NightLight::outputsChanged() {
    if (native() && on()) applyGamma(true);
}

void NightLight::forget(wl::Output* output) {
    std::erase_if(m_controls, [output](const auto& c) { return c->output == output; });
}

void NightLight::tint(int kelvin, double& r, double& g, double& b) {
    // Tanner Helland's fit of a black body's colour, as wlsunset and
    // redshift approximate it, normalised so 6500 K is white
    auto channel = [](double t) {
        double rr, gg, bb;
        if (t <= 66) {
            rr = 255;
            gg = 99.4708025861 * std::log(t) - 161.1195681661;
            bb = t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307;
        } else {
            rr = 329.698727446 * std::pow(t - 60, -0.1332047592);
            gg = 288.1221695283 * std::pow(t - 60, -0.0755148492);
            bb = 255;
        }
        return std::array<double, 3>{std::clamp(rr, 0.0, 255.0), std::clamp(gg, 0.0, 255.0), std::clamp(bb, 0.0, 255.0)};
    };
    const auto warm = channel(std::clamp(kelvin, 1000, 6500) / 100.0), white = channel(65);
    r = warm[0] / white[0];
    g = warm[1] / white[1];
    b = warm[2] / white[2];
}

} // namespace m3
