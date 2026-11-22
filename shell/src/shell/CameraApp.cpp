// Camera (material3ui-camera): photos and videos from your webcam.
//
// The picture comes through mpv's v4l2 input, drawn like Videos'. A photo is
// the frame on screen, saved to Pictures/Camera (after a timer, if you like);
// a video is recorded by ffmpeg (H.264, and the microphone), which also
// sends mpv a copy to keep the picture moving, since only one program can
// have the camera at a time.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/FileOps.h"
#include "shell/MpvView.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <csignal>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;
using mpvview::Mpv;
using mpvview::VideoView;

namespace {

struct Camera {
    std::string device, name;
};

// The capture devices (each camera's first node; the others carry metadata)
std::vector<Camera> cameras() {
    std::vector<Camera> out;
    if (std::getenv("M3_CAMERA_FAKE")) return {{"fake", "Test pattern"}};
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/video4linux", ec)) {
        std::ifstream idx(e.path() / "index"), nm(e.path() / "name");
        std::string index, name;
        std::getline(idx, index);
        std::getline(nm, name);
        if (index != "0") continue;
        out.push_back({"/dev/" + e.path().filename().string(), name.empty() ? e.path().filename().string() : name});
    }
    std::ranges::sort(out, [](const Camera& a, const Camera& b) { return a.device < b.device; });
    return out;
}

std::string folder(const char* var, const char* fallback, bool create = true) {
    const char* v = std::getenv(var);
    const std::string base = v && *v ? v : paths::home() + "/" + fallback;
    const std::string dir = base + "/Camera";
    std::error_code ec;
    if (create) fs::create_directories(dir, ec);
    return dir;
}
std::string stamp() {
    char s[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(s, sizeof s, "%Y%m%d_%H%M%S", std::localtime(&t));
    return s;
}
std::string resolution() {
    return Config::get().value<std::string>("/camera/resolution", "1280x720");
}

struct State {
    std::vector<Camera> cams;
    int cam = 0;
    std::shared_ptr<Mpv> mpv;
    VideoView* view = nullptr;
    bool video = false, recording = false;
    int recordPid = -1, timerSecs = 0, countdown = 0;
    std::string recordPath, last;
    EventLoop::Clock::time_point recordStart;
    EventLoop::TimerId tick = 0;
    Label *status = nullptr, *count = nullptr;
    Flex* controls = nullptr;
    std::function<void()> rebuild;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

void preview(const std::shared_ptr<State>& st, const std::string& url = {}) {
    if (!st->mpv || st->cams.empty()) return;
    auto& m = *st->mpv;
    const auto& c = st->cams[static_cast<size_t>(st->cam)];
    std::string src = url;
    if (src.empty()) {
        if (c.device == "fake") src = "av://lavfi:testsrc2=size=" + resolution() + ":rate=30";
        else {
            // MJPEG lets webcams give HD at full frame rate
            m.set("demuxer-lavf-o", "video_size=" + resolution() + ",input_format=mjpeg");
            src = "av://v4l2:" + c.device;
        }
    }
    // ffmpeg's copy (while recording) comes mirrored already
    m.set("vf", Config::get().value<bool>("/camera/mirror", true) && url.empty() ? "hflip" : "");
    m.command({"loadfile", src.c_str(), "replace"});
}

void flashSaved(const std::shared_ptr<State>& st, const std::string& path, bool video) {
    st->last = path;
    m3::run({"notify-send", "-a", "Camera", "-i", video ? "camera-video" : "camera-photo", video ? "Video saved" : "Photo saved", fileops::displayPath(path)}, nullptr);
    if (st->rebuild) st->rebuild();
}

void takePhoto(const std::shared_ptr<State>& st) {
    const std::string path = folder("XDG_PICTURES_DIR", "Pictures") + "/IMG_" + stamp() + ".jpg";
    st->mpv->command({"screenshot-to-file", path.c_str(), "video"});
    // The file appears a moment later
    EventLoop::get().addTimer(400, [st, path] {
        if (*st->alive) flashSaved(st, path, false);
    });
}

void startRecording(const std::shared_ptr<State>& st) {
    if (st->cams.empty()) return;
    const auto& c = st->cams[static_cast<size_t>(st->cam)];
    st->recordPath = folder("XDG_VIDEOS_DIR", "Videos") + "/VID_" + stamp() + ".mp4";
    const bool mic = Config::get().value<bool>("/camera/microphone", true);
    // The camera's ffmpeg's now; mpv watches its copy
    st->mpv->command({"stop"});
    std::vector<std::string> cmd = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-y"};
    if (c.device == "fake") cmd.insert(cmd.end(), {"-re", "-f", "lavfi", "-i", "testsrc2=size=" + resolution() + ":rate=30"});
    else cmd.insert(cmd.end(), {"-f", "v4l2", "-input_format", "mjpeg", "-video_size", resolution(), "-framerate", "30", "-i", c.device});
    if (mic) cmd.insert(cmd.end(), {"-f", "pulse", "-i", "default"});
    const std::string mirror = Config::get().value<bool>("/camera/mirror", true) ? "hflip," : "";
    cmd.insert(cmd.end(), {"-filter_complex", "[0:v]" + mirror + "split=2[rec][pre]", "-map", "[rec]"});
    if (mic) cmd.insert(cmd.end(), {"-map", "1:a", "-c:a", "aac", "-b:a", "160k"});
    cmd.insert(cmd.end(), {"-c:v", "libx264", "-preset", "veryfast", "-crf", "22", "-pix_fmt", "yuv420p", "-movflags", "+faststart", st->recordPath,
                           "-map", "[pre]", "-c:v", "mpeg1video", "-q:v", "4", "-f", "mpegts", "udp://127.0.0.1:23101?pkt_size=1316"});
    st->recording = true;
    st->recordStart = EventLoop::Clock::now();
    st->recordPid = m3::run(cmd, [st](int code, const std::string& out) {
        if (!*st->alive) return;
        st->recording = false;
        st->recordPid = -1;
        std::error_code ec;
        if (fs::exists(st->recordPath, ec) && fs::file_size(st->recordPath, ec) > 1000) flashSaved(st, st->recordPath, true);
        else m3::run({"notify-send", "-a", "Camera", "-i", "dialog-error", "Couldn't record", out.substr(0, 300) + (code ? "" : "")}, nullptr);
        preview(st);
        if (st->rebuild) st->rebuild();
    });
    // The preview from ffmpeg, as soon as it's sending
    EventLoop::get().addTimer(900, [st] {
        if (*st->alive && st->recording) preview(st, "udp://127.0.0.1:23101");
    });
    if (st->rebuild) st->rebuild();
}

void stopRecording(const std::shared_ptr<State>& st) {
    // SIGINT lets ffmpeg finish the file properly
    if (st->recordPid > 0) ::kill(st->recordPid, SIGINT);
}

void shutter(const std::shared_ptr<State>& st) {
    if (st->video) {
        if (st->recording) stopRecording(st);
        else startRecording(st);
        return;
    }
    if (st->countdown > 0) return;
    if (st->timerSecs == 0) return takePhoto(st);
    st->countdown = st->timerSecs;
    if (st->rebuild) st->rebuild();
    auto step = std::make_shared<std::function<void()>>();
    *step = [st, step] {
        if (!*st->alive) return;
        if (--st->countdown <= 0) {
            st->countdown = 0;
            takePhoto(st);
        } else {
            EventLoop::get().addTimer(1000, *step);
        }
        if (st->rebuild) st->rebuild();
    };
    EventLoop::get().addTimer(1000, *step);
}

class CameraKeeper : public Node {
public:
    explicit CameraKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~CameraKeeper() override {
        *m_st->alive = false;
        if (m_st->tick) EventLoop::get().cancel(m_st->tick);
        if (m_st->recordPid > 0) ::kill(m_st->recordPid, SIGINT);
        if (m_st->mpv) m_st->mpv->onEvents = m_st->mpv->onFrame = nullptr;
        m_st->mpv.reset();
        m_st->view = nullptr;
        m_st->rebuild = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void cameraApp(Flex* card, Popup& popup, const std::string& arg) {
    auto st = std::make_shared<State>();
    st->video = arg == "video";
    card->setPadding(Insets(10))->setSpacing(10);
    st->cams = cameras();
    st->mpv = Mpv::create(false, {{"profile", "low-latency"}, {"untimed", "yes"}, {"cache", "no"}, {"keep-open", "no"}, {"idle", "yes"}, {"osc", "no"}, {"audio", "no"}});
    if (!st->mpv) {
        caption(card, "Camera couldn't start: libmpv didn't initialise (is mpv installed?)");
        return;
    }
    if (st->cams.empty()) {
        auto* e = card->add<Flex>(Flex::Direction::Column);
        e->setAlignItems(Align::Center)->setJustify(Align::Center)->setSpacing(12);
        e->grow = true;
        e->add<Icon>("no_photography", 72)->setColor("outline", false);
        e->add<Label>("No camera found")->setSize(22)->setWeight(500);
        caption(e, "Plug one in, or check it isn't switched off (some laptops have a key or a shutter for it)");
        return;
    }
    st->view = card->add<VideoView>(st->mpv);
    st->view->grow = true;
    st->view->hasVideo = true;
    st->mpv->onFrame = [st] {
        if (*st->alive && st->view) st->view->requestRedraw();
    };
    st->mpv->onEvents = [st] {
        // Drained; nothing here needs them
        while (*st->alive && st->mpv) {
            mpv_event* e = mpv_wait_event(st->mpv->handle, 0);
            if (e->event_id == MPV_EVENT_NONE) break;
        }
    };
    st->view->onToggle = [st] { shutter(st); };

    st->controls = card->add<Flex>(Flex::Direction::Row);
    st->controls->setSpacing(10)->setAlignItems(Align::Center)->setPadding({8, 2});
    card->add<CameraKeeper>(st);

    st->rebuild = [st] {
        auto* c = st->controls;
        c->clearChildren();
        // The last capture, a click away
        auto* last = c->add<Button>();
        last->setPadding(Insets(0))->setRadius(Radii(16));
        last->fixedWidth = last->fixedHeight = 56;
        if (!st->last.empty() && !st->last.ends_with(".mp4")) {
            const std::string pic = st->last;
            auto* v = last->add<ImageView>(56, 56, [pic](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().picture(pic, static_cast<int>(56 * scale), static_cast<int>(56 * scale), true);
                if (!t) return 0;
                w = t->width, h = t->height;
                return t->id;
            });
            v->cover = true;
            v->radius = 16;
        } else {
            last->setBackground("surface_container_high", false);
            last->add<Icon>(st->last.empty() ? "photo_library" : "movie", 24)->setColor("on_surface_variant", false);
        }
        last->tooltip = "The last one (or your Camera folder)";
        last->onClicked = [st](uint32_t b) {
            if (b != BTN_LEFT || !open) return;
            if (st->last.empty()) open("viewer " + folder("XDG_PICTURES_DIR", "Pictures"));
            else open((st->last.ends_with(".mp4") ? "player " : "viewer ") + st->last);
        };
        // Photo or video
        auto* modes = c->add<Flex>(Flex::Direction::Row);
        modes->grow = true;
        modes->setJustify(Align::Center);
        connectedGroup<int>(modes, st->video ? 1 : 0, {{0, "Photo"}, {1, "Video"}}, [st](int v) {
            if (st->recording) return;
            st->video = v == 1;
            EventLoop::get().defer([st] {
                if (*st->alive && st->rebuild) st->rebuild();
            });
        });
        // The shutter
        auto* sh = c->add<Button>();
        sh->setPadding(Insets(14))->setRadius(Radii(34));
        sh->setBackground(st->recording ? "error" : st->video ? "error_container" : "primary", false);
        sh->add<Icon>(st->recording ? "stop" : st->video ? "videocam" : st->countdown ? "timer" : "photo_camera", 32)->setFill(true)->setColor(st->recording ? "on_error" : st->video ? "on_error_container" : "on_primary", false);
        sh->tooltip = st->video ? "Record (Space)" : "Take a photo (Space)";
        sh->onClicked = [st](uint32_t b) {
            if (b == BTN_LEFT) shutter(st);
        };
        st->count = c->add<Label>(st->countdown ? std::to_string(st->countdown) : st->recording ? "0:00" : "");
        st->count->setSize(18)->setWeight(600)->setColor(st->recording ? "error" : "primary", false);
        st->count->fixedWidth = 56;
        auto* right = c->add<Flex>(Flex::Direction::Row);
        right->grow = true;
        right->setJustify(Align::End)->setSpacing(4)->setAlignItems(Align::Center);
        if (!st->video)
            connectedGroup<int>(right, st->timerSecs, {{0, "No timer"}, {3, "3 s"}, {10, "10 s"}}, [st](int v) { st->timerSecs = v; });
        iconButton(right, "flip", [st] {
            Config::get().set("/camera/mirror", !Config::get().value<bool>("/camera/mirror", true));
            if (!st->recording) preview(st);
        }, 22)->tooltip = "Mirror the picture";
        if (st->cams.size() > 1)
            iconButton(right, "cameraswitch", [st] {
                if (st->recording) return;
                st->cam = (st->cam + 1) % static_cast<int>(st->cams.size());
                preview(st);
            }, 22)->tooltip = "The other camera";
    };
    // The recording's time
    st->tick = EventLoop::get().addTimer(500, [st] {
        if (!*st->alive || !st->recording || !st->count) return;
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(EventLoop::Clock::now() - st->recordStart).count();
        st->count->setText(std::format("{}:{:02}", s / 60, s % 60));
    }, true);
    popup.onKey = [st](const KeyEvent& e) {
        if (e.sym == XKB_KEY_space || e.sym == XKB_KEY_Return) return shutter(st), true;
        return false;
    };
    st->rebuild();
    preview(st);
}

void cameraSettings(Flex* page) {
    auto* g = group(page, "Picture", "From the next time the camera starts");
    choiceRow<std::string>(g, "high_quality", "Resolution", "Webcams that can't do it give the nearest", "/camera/resolution", "1280x720",
                           {{"640x480", "480p"}, {"1280x720", "720p"}, {"1920x1080", "1080p"}});
    switchRow(g, "flip", "Mirror", "As in a mirror (photos and videos are saved so too)", "/camera/mirror", true);
    auto* v = group(page, "Videos");
    switchRow(v, "mic", "Record sound", "From your default microphone", "/camera/microphone", true);
    caption(page, "Photos go to " + fileops::displayPath(folder("XDG_PICTURES_DIR", "Pictures", false)) + ", videos to " + fileops::displayPath(folder("XDG_VIDEOS_DIR", "Videos", false)));
}

} // namespace m3::shell::panels
