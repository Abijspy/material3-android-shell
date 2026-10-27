#include "services/Screenshot.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Compositor.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, "%Y-%m-%d_%H-%M-%S", &tm);
    return buf;
}

} // namespace

Screenshot& Screenshot::get() {
    static Screenshot instance;
    return instance;
}

std::string Screenshot::directory() const {
    auto dir = Config::get().value<std::string>("/screenshot/directory", "");
    if (dir.empty()) dir = paths::home() + "/Pictures/Screenshots";
    if (dir.starts_with("~/")) dir = paths::home() + dir.substr(1);
    return dir;
}

std::vector<std::string> Screenshot::recent(size_t count) const {
    std::vector<std::pair<fs::file_time_type, std::string>> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(directory(), ec)) {
        const auto ext = e.path().extension().string();
        if (e.is_regular_file(ec) && (ext == ".png" || ext == ".jpg")) files.push_back({e.last_write_time(ec), e.path()});
    }
    std::ranges::sort(files, std::greater{});
    std::vector<std::string> out;
    for (size_t i = 0; i < files.size() && i < count; ++i) out.push_back(files[i].second);
    return out;
}

void Screenshot::capture(Mode mode, int delayMs, const std::string& output) {
    if (m_busy) return;
    m_busy = true;
    EventLoop::get().addTimer(std::max(0, delayMs), [this, mode, output] {
        switch (mode) {
        case Mode::Screen:
            shoot(output.empty() ? std::vector<std::string>{} : std::vector<std::string>{"-o", output});
            return;
        case Mode::Window: {
            // The focused window's box on the desktop
            std::optional<Compositor::Window> active;
            if (Compositor::get().can(Compositor::Feature::WindowGeometry))
                for (const auto& w : Compositor::get().windows())
                    if (w.focusOrder == 0) active = w;
            if (!active) {
                log::warn("screenshot: no active window");
                m_busy = false;
                return;
            }
            shoot({"-g", std::format("{},{} {}x{}", static_cast<int>(active->x), static_cast<int>(active->y), static_cast<int>(active->w),
                                     static_cast<int>(active->h))});
            return;
        }
        case Mode::Region:
            run({"slurp", "-d"}, [this](int code, const std::string& out) {
                const std::string geometry = trimmed(out);
                if (code != 0 || geometry.empty()) {
                    m_busy = false; // cancelled with Esc
                    return;
                }
                shoot({"-g", geometry});
            });
            return;
        }
    });
}

void Screenshot::ocr() {
    run({"slurp", "-d"}, [](int code, const std::string& out) {
        const std::string geometry = trimmed(out);
        if (code != 0 || geometry.empty()) return;
        run({"sh", "-c", "grim -g \"$1\" - | tesseract - - 2>/dev/null", "sh", geometry}, [](int, const std::string& text) {
            if (trimmed(text).empty()) {
                run({"notify-send", "-a", "Screenshot", "No text found", "Is tesseract installed?"}, nullptr);
                return;
            }
            run({"wl-copy"}, nullptr, trimmed(text));
            run({"notify-send", "-a", "Screenshot", "Text copied", trimmed(text).substr(0, 120)}, nullptr);
        });
    });
}

Screenshot::RecordOptions Screenshot::RecordOptions::saved() {
    const auto& c = Config::get();
    RecordOptions o;
    o.mode = std::clamp(c.value<int>("/recorder/mode", 0), 0, 2);
    o.format = c.value<std::string>("/recorder/format", "mp4");
    o.height = c.value<int>("/recorder/height", 0);
    o.fps = c.value<int>("/recorder/fps", 60);
    o.quality = c.value<std::string>("/recorder/quality", "high");
    o.audio = c.value<std::string>("/recorder/audio", "off");
    o.hardware = c.value<bool>("/recorder/hardware", false);
    return o;
}

void Screenshot::stopRecording() {
    // SIGINT lets wf-recorder finish the file
    if (m_recording) run({"pkill", "-INT", "-x", "wf-recorder"}, nullptr);
}

void Screenshot::toggleRecording(bool region, const std::string& output) {
    if (m_recording) return stopRecording();
    auto o = RecordOptions::saved();
    o.mode = region ? 0 : 2;
    startRecording(o, output);
}

void Screenshot::startRecording(const RecordOptions& o, const std::string& output, int delayMs) {
    if (m_recording) return;
    std::string dir = paths::home() + "/Videos/Recordings";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const bool gif = o.format == "gif";
    const std::string ext = o.format == "webm" || o.format == "mkv" || gif ? o.format : "mp4";
    const std::string file = dir + "/Recording_" + stamp() + "." + ext;
    // A GIF is recorded as a video first, then made with its own palette
    const std::string capture = gif ? paths::runtimeDir() + "/recording-" + stamp() + ".mp4" : file;

    // The encoder and its quality
    std::vector<std::string> args = {"wf-recorder", "-y", "-r", std::to_string(std::clamp(o.fps, 10, 120))};
    const int level = o.quality == "low" ? 0 : o.quality == "medium" ? 1 : 2;
    const bool vaapi = o.hardware && !gif && ext != "webm" && access("/dev/dri/renderD128", R_OK) == 0;
    std::string filter;
    if (vaapi) {
        args.insert(args.end(), {"-c", "h264_vaapi", "-d", "/dev/dri/renderD128", "-p", "qp=" + std::to_string(30 - level * 5)});
        filter = "scale_vaapi=format=nv12" + (o.height > 0 ? ":w=-2:h=" + std::to_string(o.height) : std::string());
    } else if (ext == "webm") {
        args.insert(args.end(), {"-c", "libvpx-vp9", "-p", "crf=" + std::to_string(40 - level * 8), "-p", "b=0", "-p", "deadline=realtime",
                                 "-p", "cpu-used=8"});
        if (o.height > 0) filter = "scale=-2:" + std::to_string(o.height);
    } else {
        args.insert(args.end(), {"-c", "libx264", "-p", "crf=" + std::to_string(28 - level * 5), "-p", "preset=veryfast"});
        if (o.height > 0) filter = "scale=-2:" + std::to_string(o.height);
    }
    if (!filter.empty()) args.insert(args.end(), {"-F", filter});
    if (ext == "webm" && o.audio != "off") args.insert(args.end(), {"-C", "libopus"});

    auto launch = [this, args, capture, file, gif, o](std::vector<std::string> argv, const std::string& audioDevice) {
        if (!audioDevice.empty() && !gif) argv.push_back("--audio=" + audioDevice);
        argv.insert(argv.end(), {"-f", capture});
        m_recording = true;
        m_since = std::time(nullptr);
        recordingChanged.emit();
        run(argv, [this, capture, file, gif](int code, const std::string&) {
            m_recording = false;
            m_since = 0;
            recordingChanged.emit();
            std::error_code ec;
            if (code < 0 || !fs::exists(capture, ec)) {
                run({"notify-send", "-a", "Screen recording", "Recording failed", "Is wf-recorder installed?"}, nullptr);
                return;
            }
            if (!gif) {
                run({"notify-send", "-a", "Screen recording", "Recording saved", fs::path(file).filename().string()}, nullptr);
                recorded.emit(file);
                return;
            }
            // 15 frames a second, at most 960 wide, with its own palette
            run({"notify-send", "-a", "Screen recording", "Making the GIF…"}, nullptr);
            run({"ffmpeg", "-y", "-loglevel", "error", "-i", capture, "-vf",
                 "fps=15,scale='min(960,iw)':-2:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer",
                 file},
                [this, capture, file](int c, const std::string&) {
                    std::error_code e;
                    fs::remove(capture, e);
                    if (c == 0) recorded.emit(file);
                    run({"notify-send", "-a", "Screen recording", c == 0 ? "GIF saved" : "The GIF couldn't be made",
                         fs::path(file).filename().string()},
                        nullptr);
                });
        });
    };
    // Where the sound comes from: the speakers' monitor, or the microphone
    auto withAudio = [launch, o](std::vector<std::string> argv) {
        if (o.audio == "off" || o.format == "gif") return launch(argv, "");
        run({"pactl", o.audio == "mic" ? "get-default-source" : "get-default-sink"}, [launch, argv, o](int, const std::string& out) {
            const std::string dev = trimmed(out);
            launch(argv, dev.empty() ? "" : o.audio == "mic" ? dev : dev + ".monitor");
        });
    };
    auto begin = [withAudio, args, o, output](std::vector<std::string> target) {
        std::vector<std::string> argv = args;
        argv.insert(argv.end(), target.begin(), target.end());
        withAudio(argv);
    };
    // What to record: picked now, then the countdown, then it starts
    auto after = [delayMs](std::function<void()> fn) {
        if (delayMs <= 0) return fn();
        EventLoop::get().addTimer(delayMs, std::move(fn));
    };
    if (o.mode == 2) {
        after([begin, output] { begin(output.empty() ? std::vector<std::string>{} : std::vector<std::string>{"-o", output}); });
        return;
    }
    std::string boxes;
    if (o.mode == 1) {
        // A window: slurp offers the visible windows' rectangles to pick from
        if (Compositor::get().can(Compositor::Feature::WindowGeometry))
            for (const auto& w : Compositor::get().windows())
                boxes += std::format("{},{} {}x{}\n", static_cast<int>(w.x), static_cast<int>(w.y), static_cast<int>(w.w), static_cast<int>(w.h));
    }
    std::vector<std::string> slurp = {"slurp", "-d"};
    if (o.mode == 1) slurp.push_back("-r");
    run(slurp, [begin, after](int code, const std::string& out) {
        const std::string geometry = trimmed(out);
        if (code == 0 && !geometry.empty()) after([begin, geometry] { begin({"-g", geometry}); });
    }, boxes);
}

void Screenshot::shoot(const std::vector<std::string>& target) {
    const std::string dir = directory();
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string file = dir + "/Screenshot_" + stamp() + ".png";

    std::vector<std::string> argv = {"grim"};
    argv.insert(argv.end(), target.begin(), target.end());
    argv.push_back(file);
    run(argv, [this, file](int code, const std::string&) {
        m_busy = false;
        if (code != 0) {
            log::warn("screenshot: grim failed ({})", code);
            return;
        }
        const auto& c = Config::get();
        if (c.value<bool>("/screenshot/copy", true))
            run({"sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", file}, nullptr);
        if (c.value<bool>("/screenshot/edit", false))
            spawn(std::format("command -v satty >/dev/null && satty --filename '{0}' --output-filename '{0}' || swappy -f '{0}'", file));
        else if (c.value<bool>("/screenshot/notify", true))
            run({"notify-send", "-a", "Screenshot", "-i", file, "Screenshot saved",
                 fs::path(file).filename().string() + (c.value<bool>("/screenshot/copy", true) ? " · copied to the clipboard" : "")},
                nullptr);
        taken.emit(file);
    });
}

} // namespace m3
