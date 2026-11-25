// Sound Recorder (material3ui-recorder): voice notes and anything the
// microphone hears. ffmpeg records (Opus, MP3, FLAC or WAV) from the
// microphone you pick; parec, beside it, feeds the live level drawn as it
// comes. Recordings live in Music/Recordings: play, rename, show, trash.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cmath>
#include <csignal>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <map>
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

extern char** environ;

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::string folder() {
    const char* v = std::getenv("XDG_MUSIC_DIR");
    const std::string dir = (v && *v ? std::string(v) : paths::home() + "/Music") + "/Recordings";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// The microphone's level, live: parec's raw samples through a pipe
class Level : public EventSource {
public:
    std::deque<float> history; // 0–1, newest last
    std::function<void()> changed;
    explicit Level(const std::string& source) {
        int p[2];
        if (pipe2(p, O_CLOEXEC) < 0) return;
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, p[1], 1);
        std::vector<std::string> args = {"parec", "--format=s16le", "--rate=8000", "--channels=1", "--latency-msec=40", "--raw"};
        if (!source.empty()) args.push_back("--device=" + source);
        std::vector<char*> argv;
        for (auto& a : args) argv.push_back(a.data());
        argv.push_back(nullptr);
        if (posix_spawnp(&m_pid, "parec", &fa, nullptr, argv.data(), environ) != 0) m_pid = -1;
        posix_spawn_file_actions_destroy(&fa);
        close(p[1]);
        m_fd = p[0];
        fcntl(m_fd, F_SETFL, O_NONBLOCK);
        EventLoop::get().add(this);
    }
    ~Level() override {
        EventLoop::get().remove(this);
        if (m_fd >= 0) close(m_fd);
        if (m_pid > 0) {
            ::kill(m_pid, SIGTERM);
            waitpid(m_pid, nullptr, 0);
        }
    }
    int fd() const override { return m_fd; }
    void dispatch(short) override {
        unsigned char buf[2048];
        bool any = false;
        for (ssize_t n; (n = read(m_fd, buf, sizeof buf)) > 0;) {
            // Samples are two bytes: one left over from the last read goes first
            m_bytes.insert(m_bytes.end(), buf, buf + n);
            const size_t whole = m_bytes.size() / 2;
            // A bar each 40 ms (320 samples)
            for (size_t i = 0; i < whole; ++i) {
                const auto v = static_cast<int16_t>(m_bytes[2 * i] | (m_bytes[2 * i + 1] << 8));
                m_sum += static_cast<double>(v) * v;
                if (++m_count == 320) {
                    const double rms = std::sqrt(m_sum / m_count) / 32768.0;
                    const float db = static_cast<float>(20 * std::log10(std::max(rms, 1e-5)));
                    history.push_back(std::clamp((db + 60) / 60, 0.0f, 1.0f));
                    while (history.size() > 160) history.pop_front();
                    m_sum = 0, m_count = 0;
                    any = true;
                }
            }
            m_bytes.erase(m_bytes.begin(), m_bytes.begin() + static_cast<long>(whole * 2));
        }
        if (any && changed) changed();
    }

private:
    int m_fd = -1;
    pid_t m_pid = -1;
    double m_sum = 0;
    int m_count = 0;
    std::vector<unsigned char> m_bytes;
};

class Wave : public Node {
public:
    const std::deque<float>* data = nullptr;
    bool live = false;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 140}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Theme::get().color("surface_container_high"), Radii(28));
        if (!data) return;
        const float bw = 4, gap = 2;
        const int n = static_cast<int>((b.w - 32) / (bw + gap));
        const Color c = Theme::get().color(live ? "error" : "primary");
        for (int i = 0; i < n; ++i) {
            const int idx = static_cast<int>(data->size()) - n + i;
            const float v = idx >= 0 ? (*data)[static_cast<size_t>(idx)] : 0;
            const float h = std::max(4.0f, v * (b.h - 36));
            r.rect({b.x + 16 + static_cast<float>(i) * (bw + gap), b.y + b.h / 2 - h / 2, bw, h}, c.withAlpha(idx >= 0 ? 1.0f : 0.25f), Radii(2));
        }
    }
};

struct State {
    std::unique_ptr<Level> level;
    int pid = -1;
    std::string path;
    EventLoop::Clock::time_point start;
    Wave* wave = nullptr;
    Label* time = nullptr;
    Flex *list = nullptr, *controls = nullptr;
    std::map<std::string, double> durations;
    std::function<void()> refresh, rebuild;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId tick = 0;
};

bool recording(const std::shared_ptr<State>& st) {
    return st->pid > 0;
}

void startRecording(const std::shared_ptr<State>& st) {
    const std::string fmt = Config::get().value<std::string>("/recorder/format", "opus");
    const std::string src = Config::get().value<std::string>("/recorder/source", "");
    char name[48];
    const std::time_t t = std::time(nullptr);
    std::strftime(name, sizeof name, "Recording %Y-%m-%d %H.%M.%S", std::localtime(&t));
    st->path = folder() + "/" + name + "." + fmt;
    std::vector<std::string> cmd = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "pulse", "-i", src.empty() ? "default" : src};
    // Mono (voice) or stereo, and how much detail the lossy formats keep
    cmd.insert(cmd.end(), {"-ac", Config::get().value<bool>("/recorder/stereo", false) ? "2" : "1"});
    const std::string quality = Config::get().value<std::string>("/recorder/quality", "normal");
    if (fmt == "opus") cmd.insert(cmd.end(), {"-c:a", "libopus", "-b:a", quality == "small" ? "48k" : quality == "high" ? "160k" : "96k"});
    else if (fmt == "mp3") cmd.insert(cmd.end(), {"-c:a", "libmp3lame", "-q:a", quality == "small" ? "6" : quality == "high" ? "0" : "2"});
    else if (fmt == "flac") cmd.insert(cmd.end(), {"-c:a", "flac"});
    cmd.push_back(st->path);
    st->start = EventLoop::Clock::now();
    st->pid = m3::run(cmd, [st](int, const std::string& err) {
        if (!*st->alive) return;
        st->pid = -1;
        std::error_code ec;
        if (!fs::exists(st->path, ec) || fs::file_size(st->path, ec) < 200)
            m3::run({"notify-send", "-a", "Sound Recorder", "-i", "dialog-error", "Couldn't record", err.substr(0, 300)}, nullptr);
        if (st->rebuild) st->rebuild();
        if (st->refresh) st->refresh();
    });
    if (st->wave) st->wave->live = true;
    if (st->rebuild) st->rebuild();
}

void stopRecording(const std::shared_ptr<State>& st) {
    if (st->pid > 0) ::kill(st->pid, SIGINT); // ffmpeg finishes the file
    if (st->wave) st->wave->live = false;
}

class RecKeeper : public Node {
public:
    explicit RecKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~RecKeeper() override {
        *m_st->alive = false;
        if (m_st->tick) EventLoop::get().cancel(m_st->tick);
        if (m_st->pid > 0) ::kill(m_st->pid, SIGINT);
        m_st->level.reset();
        m_st->refresh = m_st->rebuild = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void recorderApp(Flex* card, Popup& popup, const std::string& arg) {
    (void)arg;
    auto st = std::make_shared<State>();
    card->setPadding({18, 10, 18, 16})->setSpacing(14);
    st->wave = card->add<Wave>();
    st->level = std::make_unique<Level>(Config::get().value<std::string>("/recorder/source", ""));
    st->wave->data = &st->level->history;
    st->level->changed = [st] {
        if (*st->alive && st->wave) st->wave->requestRedraw();
    };
    st->time = card->add<Label>("0:00");
    st->time->setSize(40)->setWeight(400)->setCentered(true);
    st->time->alignSelf = Align::Center;
    st->controls = card->add<Flex>(Flex::Direction::Row);
    st->controls->setJustify(Align::Center)->setSpacing(12);
    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    st->list = scroll->add<Flex>(Flex::Direction::Column);
    st->list->setSpacing(4)->setAlignItems(Align::Stretch);
    card->add<RecKeeper>(st);

    st->rebuild = [st] {
        auto* c = st->controls;
        c->clearChildren();
        auto* b = c->add<Button>();
        b->setPadding(Insets(18))->setRadius(Radii(40));
        b->setBackground(recording(st) ? "error" : "error_container", false);
        b->add<Icon>(recording(st) ? "stop" : "mic", 36)->setFill(true)->setColor(recording(st) ? "on_error" : "on_error_container", false);
        b->tooltip = recording(st) ? "Stop (Space)" : "Record (Space)";
        b->onClicked = [st](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            if (recording(st)) stopRecording(st);
            else startRecording(st);
        };
        if (!recording(st)) st->time->setText("Ready");
    };
    st->refresh = [st] {
        auto* l = st->list;
        l->clearChildren();
        std::vector<fs::directory_entry> files;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(folder(), ec))
            if (e.is_regular_file(ec) && fileops::kindOf(e.path().string()) == fileops::Kind::Audio) files.push_back(e);
        std::ranges::sort(files, [](const auto& a, const auto& b) { return a.last_write_time() > b.last_write_time(); });
        caption(l, files.empty() ? "Your recordings show here" : std::format("{} recordings  ·  {}", files.size(), fileops::displayPath(folder())));
        for (const auto& e : files) {
            const std::string path = e.path().string();
            auto* r = l->add<Flex>(Flex::Direction::Row);
            r->setPadding({14, 8})->setSpacing(12)->setAlignItems(Align::Center)->setRadius(Radii(18))->setBackground("surface_container_high", false);
            iconButton(r, "play_arrow", [path] {
                if (open) open("player " + path);
            }, 22)->tooltip = "Play";
            auto* t = r->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start);
            t->grow = true;
            t->add<Label>(e.path().stem().string())->setSize(14)->setWeight(500)->setMaxWidth(360);
            std::string sub = fileops::humanSize(e.file_size(ec));
            if (auto it = st->durations.find(path); it != st->durations.end()) {
                const long s = std::lround(it->second);
                sub = std::format("{}:{:02}  ·  {}", s / 60, s % 60, sub);
            } else {
                m3::run({"ffprobe", "-v", "quiet", "-show_entries", "format=duration", "-of", "csv=p=0", path}, [st, path](int, const std::string& out) {
                    if (!*st->alive) return;
                    st->durations[path] = std::atof(out.c_str());
                    EventLoop::get().debounce(st->tick, 200, [st] {
                        if (*st->alive && st->refresh) st->refresh();
                    });
                });
            }
            t->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false);
            Button* more = iconButton(r, "more_vert", [] {}, 20);
            more->onClicked = [st, path, more](uint32_t btn) {
                if (btn != BTN_LEFT) return;
                std::vector<MenuItem> items;
                items.emplace_back("folder_open", "Show in Files", "", [path] {
                    if (open) open("files " + path);
                });
                items.emplace_back("share", "Quick Share", "", [path] {
                    if (open) open("quickshare-send " + path);
                });
                items.emplace_back("delete", "Move to the trash", "", [st, path] {
                    fileops::trash({path}, [st](const std::string&) {
                        if (*st->alive && st->refresh) st->refresh();
                    });
                }, false, true, true, true);
                floatingMenu(more, items);
            };
        }
    };
    // The clock while recording
    EventLoop::get().addTimer(250, [st] {
        if (!*st->alive) return;
        if (recording(st) && st->time) {
            const auto s = std::chrono::duration_cast<std::chrono::seconds>(EventLoop::Clock::now() - st->start).count();
            st->time->setText(std::format("{}:{:02}", s / 60, s % 60));
        }
    }, true);
    popup.onKey = [st](const KeyEvent& e) {
        if (e.sym == XKB_KEY_space) {
            if (recording(st)) stopRecording(st);
            else startRecording(st);
            return true;
        }
        return false;
    };
    st->rebuild();
    st->refresh();
}

void recorderSettings(Flex* page) {
    auto* g = group(page, "Recording");
    choiceRow<std::string>(g, "audio_file", "Format", "Opus is small and clear; FLAC and WAV keep everything", "/recorder/format", "opus",
                           {{"opus", "Opus"}, {"mp3", "MP3"}, {"flac", "FLAC"}, {"wav", "WAV"}});
    choiceRow<std::string>(g, "high_quality", "Quality", "For Opus and MP3", "/recorder/quality", "normal", {{"small", "Smaller files"}, {"normal", "Balanced"}, {"high", "Best"}});
    switchRow(g, "surround_sound", "Stereo", "Two channels, for music; off, mono (half the size, fine for voice)", "/recorder/stereo", false);
    auto r = row(g, "mic", "Microphone", Config::get().value<std::string>("/recorder/source", "").empty() ? "The default one" : Config::get().value<std::string>("/recorder/source", ""));
    auto* holder = r.row;
    m3::run({"pactl", "list", "short", "sources"}, [holder](int, const std::string& out) {
        std::vector<std::pair<std::string, std::string>> items = {{"", "Default"}};
        std::istringstream in(out);
        for (std::string line; std::getline(in, line);) {
            std::istringstream ls(line);
            std::string idx, name;
            ls >> idx >> name;
            if (!name.empty() && name.find(".monitor") == std::string::npos) items.push_back({name, name.size() > 40 ? name.substr(0, 40) + "…" : name});
        }
        dropdown(holder, holder, "Choose", items, [](const std::string& v) { Config::get().set("/recorder/source", v); });
    });
    caption(page, "Recordings go to " + fileops::displayPath(folder()));
}

} // namespace m3::shell::panels
