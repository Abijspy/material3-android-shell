#include "services/Screenshot.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Hyprland.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <format>

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
            auto w = Hyprland::get().requestJson("activewindow");
            if (!w || !w->contains("at") || !w->contains("size")) {
                log::warn("screenshot: no active window");
                m_busy = false;
                return;
            }
            const auto& at = (*w)["at"];
            const auto& size = (*w)["size"];
            shoot({"-g", std::format("{},{} {}x{}", at[0].get<int>(), at[1].get<int>(), size[0].get<int>(), size[1].get<int>())});
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

void Screenshot::toggleRecording(bool region, const std::string& output) {
    if (m_recording) {
        // SIGINT lets wf-recorder finish the file
        run({"pkill", "-INT", "-x", "wf-recorder"}, nullptr);
        return;
    }
    auto start = [this](const std::vector<std::string>& target) {
        std::string dir = paths::home() + "/Videos/Recordings";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string file = dir + "/Recording_" + stamp() + ".mp4";
        std::vector<std::string> argv = {"wf-recorder"};
        argv.insert(argv.end(), target.begin(), target.end());
        argv.insert(argv.end(), {"-f", file});
        m_recording = true;
        recordingChanged.emit();
        run(argv, [this, file](int code, const std::string&) {
            m_recording = false;
            recordingChanged.emit();
            if (code < 0) {
                run({"notify-send", "-a", "Screen recording", "Recording failed", "Is wf-recorder installed?"}, nullptr);
                return;
            }
            run({"notify-send", "-a", "Screen recording", "Recording saved", fs::path(file).filename().string()}, nullptr);
        });
    };
    if (!region) {
        start(output.empty() ? std::vector<std::string>{} : std::vector<std::string>{"-o", output});
        return;
    }
    run({"slurp", "-d"}, [start](int code, const std::string& out) {
        const std::string geometry = trimmed(out);
        if (code == 0 && !geometry.empty()) start({"-g", geometry});
    });
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
