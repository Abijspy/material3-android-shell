#include "services/Equalizer.h"

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

extern char** environ;

namespace m3 {

const std::array<int, Equalizer::kBands>& Equalizer::frequencies() {
    static const std::array<int, kBands> f = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    return f;
}

std::string Equalizer::label(int hz) {
    return hz >= 1000 ? std::format("{}k", hz / 1000) : std::to_string(hz);
}

std::string Equalizer::key(int hz) {
    return "/equalizer/band" + std::to_string(hz);
}

const std::vector<std::pair<std::string, std::array<double, Equalizer::kBands>>>& Equalizer::presets() {
    static const std::vector<std::pair<std::string, std::array<double, kBands>>> p = {
        {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {"Bass", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
        {"Treble", {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
        {"Vocal", {-2, -2, -1, 1, 3, 4, 3, 1, 0, -1}},
        {"Rock", {4, 3, 2, 0, -1, -1, 1, 3, 4, 4}},
        {"Classical", {3, 2, 1, 0, 0, 0, 0, 1, 2, 3}},
    };
    return p;
}

namespace {
// The gains as one string: whether what's running is what's set
std::string gainsKey(const std::array<double, Equalizer::kBands>& g) {
    std::string k;
    for (double v : g) k += std::format("{:.1f},", v);
    return k;
}
} // namespace

Equalizer& Equalizer::get() {
    static Equalizer instance;
    return instance;
}

bool Equalizer::on() const {
    return Config::get().value<bool>("/equalizer/enabled", false);
}

std::array<double, Equalizer::kBands> Equalizer::gains() const {
    std::array<double, kBands> g{};
    for (size_t i = 0; i < kBands; ++i) g[i] = std::clamp(Config::get().value<double>(key(frequencies()[i]), 0.0), -12.0, 12.0);
    return g;
}

void Equalizer::setPreset(const std::array<double, kBands>& gains) {
    for (size_t i = 0; i < kBands; ++i) Config::get().set(key(frequencies()[i]), gains[i]);
}

void Equalizer::start() {
    // On, off, or the gains changed: the chain again, once things settle
    Config::get().changed.connectForever([this] {
        const std::string want = on() ? gainsKey(gains()) : "";
        if (want == m_applied) return;
        EventLoop::get().debounce(m_soon, 400, [this] { restart(); });
    });
    if (on()) restart();
}

void Equalizer::stop() {
    if (m_pid <= 0) return;
    kill(m_pid, SIGTERM);
    waitpid(m_pid, nullptr, 0); // ours: the shell's reaper leaves it alone
    m_pid = -1;
}

void Equalizer::restart() {
    stop();
    m_applied = on() ? gainsKey(gains()) : "";
    if (!on()) {
        changed.emit();
        return;
    }
    // The chain: a low shelf, eight peaking bands an octave apart, a high shelf
    const auto g = gains();
    std::string nodes, links;
    for (size_t i = 0; i < kBands; ++i) {
        const char* kind = i == 0 ? "bq_lowshelf" : i + 1 == kBands ? "bq_highshelf" : "bq_peaking";
        nodes += std::format("        {{ type = builtin name = band{} label = {} control = {{ \"Freq\" = {}.0 \"Q\" = 1.41 \"Gain\" = {:.1f} }} }}\n", i,
                             kind, frequencies()[i], g[i]);
        if (i) links += std::format("        {{ output = \"band{}:Out\" input = \"band{}:In\" }}\n", i - 1, i);
    }
    const std::string conf = std::format(R"(# Written by Material3UI (Settings → Sound → Equaliser)
context.properties = {{ log.level = 0 }}
context.spa-libs = {{ audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }}
context.modules = [
  {{ name = libpipewire-module-rt flags = [ ifexists nofail ] }}
  {{ name = libpipewire-module-protocol-native }}
  {{ name = libpipewire-module-client-node }}
  {{ name = libpipewire-module-adapter }}
  {{ name = libpipewire-module-filter-chain
    args = {{
      node.description = "Material3UI equaliser"
      media.name = "Material3UI equaliser"
      filter.graph = {{
        nodes = [
{}        ]
        links = [
{}        ]
      }}
      audio.channels = 2
      audio.position = [ FL FR ]
      capture.props = {{
        node.name = "material3ui.equaliser"
        media.class = Audio/Sink
        filter.smart = true
        filter.smart.name = "material3ui-equaliser"
      }}
      playback.props = {{
        node.name = "material3ui.equaliser.output"
        node.passive = true
      }}
    }}
  }}
]
)",
                                         nodes, links);
    std::error_code ec;
    std::filesystem::create_directories(paths::runtimeDir(), ec);
    const std::string file = paths::runtimeDir() + "/equaliser.conf";
    std::ofstream(file) << conf;
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
        log::warn("equaliser: couldn't start pipewire");
        m_pid = -1;
    }
    posix_spawn_file_actions_destroy(&fa);
    changed.emit();
}

} // namespace m3
