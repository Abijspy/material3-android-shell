#include "services/NoiseSuppression.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <csignal>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>
#include <vector>

extern char** environ;

namespace m3 {

NoiseSuppression& NoiseSuppression::get() {
    static NoiseSuppression instance;
    return instance;
}

bool NoiseSuppression::pluginInstalled() {
    std::error_code ec;
    return std::filesystem::exists(kPlugin, ec);
}

bool NoiseSuppression::on() const {
    return Config::get().value<bool>("/microphone/noiseSuppression", false);
}

int NoiseSuppression::strength() const {
    return std::clamp(Config::get().value<int>("/microphone/noiseStrength", 50), 5, 95);
}

std::string NoiseSuppression::config(int strength) {
    return std::format(R"CONF(# Written by Material3UI (Settings → Sound → Microphone → Noise suppression)
context.properties = {{ log.level = 0 }}
context.spa-libs = {{ audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }}
context.modules = [
  {{ name = libpipewire-module-rt flags = [ ifexists nofail ] }}
  {{ name = libpipewire-module-protocol-native }}
  {{ name = libpipewire-module-client-node }}
  {{ name = libpipewire-module-adapter }}
  {{ name = libpipewire-module-filter-chain
    args = {{
      node.description = "Microphone (noise suppressed)"
      media.name = "Microphone (noise suppressed)"
      filter.graph = {{
        nodes = [
          {{ type = ladspa name = rnnoise plugin = {} label = noise_suppressor_mono control = {{ "VAD Threshold (%)" = {}.0 }} }}
        ]
      }}
      audio.rate = 48000
      audio.channels = 1
      audio.position = [ MONO ]
      capture.props = {{
        node.name = "material3ui.noise.input"
        node.passive = true
      }}
      playback.props = {{
        node.name = "material3ui.noise"
        media.class = Audio/Source
        filter.smart = true
        filter.smart.name = "material3ui-noise-suppression"
      }}
    }}
  }}
]
)CONF",
                       kPlugin, strength);
}

void NoiseSuppression::start() {
    Config::get().changed.connectForever([this] {
        const std::string want = on() ? std::to_string(strength()) : "";
        if (want == m_applied) return;
        EventLoop::get().debounce(m_soon, 400, [this] { restart(); });
    });
    if (on()) restart();
}

void NoiseSuppression::stop() {
    if (m_pid <= 0) return;
    kill(m_pid, SIGTERM);
    waitpid(m_pid, nullptr, 0); // ours: the shell's reaper leaves it alone
    m_pid = -1;
}

void NoiseSuppression::restart() {
    stop();
    m_applied = on() ? std::to_string(strength()) : "";
    if (!on()) {
        changed.emit();
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(paths::runtimeDir(), ec);
    const std::string file = paths::runtimeDir() + "/noise-suppression.conf";
    std::ofstream(file) << config(strength());
    if (!pluginInstalled()) {
        log::warn("noise suppression: {} isn't installed", kPackage);
        changed.emit();
        return;
    }
    // Its own pipewire process, ended with the shell however that ends
    std::vector<std::string> args;
    if (haveCommand("setpriv")) args = {"setpriv", "--pdeathsig", "TERM", "--"};
    args.insert(args.end(), {"pipewire", "-c", file});
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    if (posix_spawnp(&m_pid, argv[0], &fa, nullptr, argv.data(), environ) != 0) {
        log::warn("noise suppression: couldn't start pipewire");
        m_pid = -1;
    }
    posix_spawn_file_actions_destroy(&fa);
    changed.emit();
}

} // namespace m3
