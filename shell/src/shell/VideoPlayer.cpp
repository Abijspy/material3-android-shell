// The video player: the shell's own, in an app window, playing through
// libmpv (every format and codec mpv knows, hardware decoding, subtitles,
// web addresses through yt-dlp). mpv draws each frame into a texture of
// ours with its OpenGL render API; the shell draws that like any picture.
//
// The folder's other videos (or songs) are its playlist; the bar under the
// picture seeks, pauses, steps, sets the volume and speed, picks audio and
// subtitle tracks, takes a screenshot and goes full screen, where it hides
// itself until the pointer moves.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/Compositor.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <EGL/egl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

std::function<void(const std::string&)> playerGo;

namespace {

bool subtitleFile(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* x : {".srt", ".ass", ".ssa", ".vtt", ".sub", ".idx", ".sup", ".smi", ".sami", ".lrc", ".ttml", ".dfxp", ".mpl", ".txt", ".usf", ".jss", ".rt", ".pjs", ".stl"})
        if (e == x) return true;
    return false;
}

std::string clock(double s) {
    if (!std::isfinite(s) || s < 0) s = 0;
    const long t = std::lround(s);
    return t >= 3600 ? std::format("{}:{:02}:{:02}", t / 3600, t % 3600 / 60, t % 60) : std::format("{}:{:02}", t / 60, t % 60);
}

// mpv itself, and the texture it draws into. Freed with the GL context
// current, its callbacks cut off first (they come from mpv's threads and
// reach the loop through post)
struct Mpv {
    mpv_handle* handle = nullptr;
    mpv_render_context* render = nullptr;
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool frame = false; // a new frame to render
    std::weak_ptr<Mpv> self;
    std::function<void()> onEvents, onFrame;

    ~Mpv() {
        if (handle) mpv_set_wakeup_callback(handle, nullptr, nullptr);
        Renderer::get().makeCurrent();
        if (render) {
            mpv_render_context_set_update_callback(render, nullptr, nullptr);
            mpv_render_context_free(render);
        }
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (tex) glDeleteTextures(1, &tex);
        if (handle) mpv_terminate_destroy(handle);
    }

    static void* proc(void*, const char* name) {
        return reinterpret_cast<void*>(eglGetProcAddress(name));
    }
    static void wakeup(void* ctx) {
        std::weak_ptr<Mpv> weak = static_cast<Mpv*>(ctx)->self;
        EventLoop::get().post([weak] {
            if (auto m = weak.lock(); m && m->onEvents) m->onEvents();
        });
    }
    static void update(void* ctx) {
        std::weak_ptr<Mpv> weak = static_cast<Mpv*>(ctx)->self;
        EventLoop::get().post([weak] {
            auto m = weak.lock();
            if (!m || !m->render) return;
            if (mpv_render_context_update(m->render) & MPV_RENDER_UPDATE_FRAME) {
                m->frame = true;
                if (m->onFrame) m->onFrame();
            }
        });
    }

    static std::shared_ptr<Mpv> create() {
        auto m = std::make_shared<Mpv>();
        m->self = m;
        m->handle = mpv_create();
        if (!m->handle) return nullptr;
        for (const auto& [k, v] : std::initializer_list<std::pair<const char*, const char*>>{
                 {"vo", "libmpv"}, {"hwdec", "auto-safe"}, {"keep-open", "yes"}, {"idle", "yes"}, {"osc", "no"}, {"osd-level", "0"},
                 {"input-default-bindings", "no"}, {"input-vo-keyboard", "no"}, {"terminal", "no"}, {"ytdl", "yes"},
                 {"audio-client-name", "Material3UI Videos"}, {"screenshot-format", "png"}, {"sub-auto", "fuzzy"},
                 {"audio-display", "embedded-first"}, {"load-scripts", "no"},
                 {"sub-font-size", "46"}, {"sub-border-size", "2.5"}, {"sub-shadow-offset", "1"},
                 {"sub-codepage", "auto"}, {"slang", "en,eng"}, {"sub-file-paths", "subs:Subs:subtitles:Subtitles"}, {"demuxer-max-bytes", "150MiB"},
                 {"cache", "auto"}})
            mpv_set_option_string(m->handle, k, v);
        // Settings → Videos: resuming, and the starting volume
        if (Config::get().value<bool>("/player/resume", true)) {
            const std::string later = paths::stateDir() + "/watch_later";
            std::error_code ec;
            std::filesystem::create_directories(later, ec);
            mpv_set_option_string(m->handle, "save-position-on-quit", "yes");
            mpv_set_option_string(m->handle, "watch-later-directory", later.c_str());
            mpv_set_option_string(m->handle, "watch-later-options", "start,volume,mute,sid,aid,sub-delay,speed");
        }
        mpv_set_option_string(m->handle, "volume", std::to_string(static_cast<int>(Config::get().value<double>("/player/volume", 100))).c_str());
        if (mpv_initialize(m->handle) < 0) return nullptr;
        Renderer::get().makeCurrent();
        mpv_opengl_init_params gl{&Mpv::proc, nullptr};
        int advanced = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
            {MPV_RENDER_PARAM_WL_DISPLAY, wl::Display::get().display()},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        if (mpv_render_context_create(&m->render, m->handle, params) < 0) {
            m->render = nullptr;
            return nullptr;
        }
        mpv_set_wakeup_callback(m->handle, &Mpv::wakeup, m.get());
        mpv_render_context_set_update_callback(m->render, &Mpv::update, m.get());
        return m;
    }

    void command(std::initializer_list<const char*> args) {
        std::vector<const char*> v(args);
        v.push_back(nullptr);
        mpv_command_async(handle, 0, v.data());
    }
    void set(const char* name, const std::string& value) { mpv_set_property_string(handle, name, value.c_str()); }
    void set(const char* name, double v) { mpv_set_property(handle, name, MPV_FORMAT_DOUBLE, &v); }
    void setFlag(const char* name, bool on) {
        int f = on;
        mpv_set_property(handle, name, MPV_FORMAT_FLAG, &f);
    }
};

// Where the video shows: the texture mpv drew, at the node's own size in
// device pixels (mpv letterboxes into it)
class VideoView : public Node {
public:
    explicit VideoView(std::shared_ptr<Mpv> mpv) : m_mpv(std::move(mpv)) {}
    std::function<void()> onActivity, onToggle, onFullscreen;
    std::function<void(int)> onWheel;
    bool hasVideo = false;
    std::string placeholder; // shown with no picture (a song without art)

    bool interactive() const override { return true; }
    void onPointerMove(float, float) override {
        if (onActivity) onActivity();
    }
    void onPress(uint32_t button, float, float) override {
        if (button != BTN_LEFT) return;
        const auto now = EventLoop::Clock::now();
        // A double click goes full screen; a single one pauses, after a beat
        // (unless a second follows)
        if (now - m_lastPress < std::chrono::milliseconds(350)) {
            m_lastPress = {};
            if (m_pending) EventLoop::get().cancel(m_pending), m_pending = 0;
            if (onFullscreen) onFullscreen();
            return;
        }
        m_lastPress = now;
        m_pending = EventLoop::get().addTimer(300, [this, alive = m_alive] {
            if (!*alive) return;
            m_pending = 0;
            if (onToggle) onToggle();
        });
    }
    bool onScroll(int steps) override {
        if (onWheel) onWheel(steps);
        return true;
    }
    ~VideoView() override {
        *m_alive = false;
        if (m_pending) EventLoop::get().cancel(m_pending);
    }

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, std::max(120.0f, available.h)}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Color{0, 0, 0, 1}, Radii(18));
        auto& m = *m_mpv;
        if (!hasVideo || !m.render) {
            if (!placeholder.empty()) paintPlaceholder(r);
            return;
        }
        const int w = std::max(1, static_cast<int>(std::lround(b.w * r.scale()))), h = std::max(1, static_cast<int>(std::lround(b.h * r.scale())));
        if (w != m.w || h != m.h) {
            if (!m.tex) glGenTextures(1, &m.tex), glGenFramebuffers(1, &m.fbo);
            glBindTexture(GL_TEXTURE_2D, m.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, m.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m.tex, 0);
            m.w = w, m.h = h;
            m.frame = true;
        }
        if (m.frame) {
            m.frame = false;
            mpv_opengl_fbo fbo{static_cast<int>(m.fbo), w, h, 0};
            int flip = 0; // into a texture: rows already run top-down for our quad
            mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo}, {MPV_RENDER_PARAM_FLIP_Y, &flip}, {MPV_RENDER_PARAM_INVALID, nullptr}};
            mpv_render_context_render(m.render, params);
            r.resetState();
        }
        r.imageRGBA(m.tex, b, 1, Radii(18));
    }

private:
    void paintPlaceholder(Renderer& r) {
        // A song without its cover: a note, large, in the middle
        if (!m_note) {
            m_note = std::make_unique<Icon>("music_note", 96);
            m_note->setColor("primary", false);
        }
        const auto& b = rect();
        const float s = std::min(160.0f, std::min(b.w, b.h) * 0.6f);
        r.rect({b.x + b.w / 2 - s / 2, b.y + b.h / 2 - s / 2, s, s}, Theme::get().color("primary_container").withAlpha(0.35f), Radii(s / 2));
        m_note->measure({96, 96});
        m_note->arrange({b.x + b.w / 2 - 48, b.y + b.h / 2 - 48, 96, 96});
        m_note->paint(r);
    }

    std::shared_ptr<Mpv> m_mpv;
    EventLoop::Clock::time_point m_lastPress{};
    EventLoop::TimerId m_pending = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<Icon> m_note;
};

struct PlayerState {
    std::shared_ptr<Mpv> mpv;
    VideoView* view = nullptr;
    Flex *controls = nullptr, *empty = nullptr, *stage = nullptr;
    Slider *seek = nullptr, *volume = nullptr;
    Label *time = nullptr, *duration = nullptr, *title = nullptr, *where = nullptr, *speedLabel = nullptr;
    Icon *playIcon = nullptr, *volumeIcon = nullptr;
    std::vector<std::string> playlist;
    int index = 0;
    double pos = 0, length = 0, vol = 100, speed = 1;
    bool paused = false, muted = false, seeking = false, fullscreen = false, eof = false;
    int64_t vw = 0, vh = 0;
    std::string mediaTitle;
    EventLoop::TimerId hide = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::function<void()> refresh;
    Popup* popup = nullptr;
};

class PlayerKeeper : public Node {
public:
    explicit PlayerKeeper(std::shared_ptr<PlayerState> st) : m_st(std::move(st)) { floating = true; }
    ~PlayerKeeper() override {
        *m_st->alive = false;
        if (m_st->hide) EventLoop::get().cancel(m_st->hide);
        m_st->refresh = nullptr;
        if (m_st->mpv) m_st->mpv->onEvents = m_st->mpv->onFrame = nullptr;
        m_st->mpv.reset(); // the view keeps its own until it goes too
        m_st->view = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<PlayerState> m_st;
};

// The folder's videos (or its songs, for a song), in name order
std::vector<std::string> siblings(const std::string& file) {
    if (file.find("://") != std::string::npos) return {file};
    const auto kind = fileops::kindOf(file);
    std::vector<std::string> out;
    for (const auto& e : fileops::list(fs::path(file).parent_path().string(), false))
        if (!e.dir && fileops::kindOf(e.path) == kind) out.push_back(e.path);
    if (std::ranges::find(out, file) == out.end()) out.push_back(file);
    std::ranges::sort(out, [](const std::string& a, const std::string& b) { return strverscmp(fs::path(a).filename().c_str(), fs::path(b).filename().c_str()) < 0; });
    return out;
}

void load(const std::shared_ptr<PlayerState>& st, int index) {
    if (st->playlist.empty() || !st->mpv) return;
    st->index = (index % static_cast<int>(st->playlist.size()) + static_cast<int>(st->playlist.size())) % static_cast<int>(st->playlist.size());
    st->eof = false;
    st->pos = st->length = 0;
    st->vw = st->vh = 0;
    st->mediaTitle.clear();
    // Settings that apply from this file on
    auto& m = *st->mpv;
    m.set("hwdec", Config::get().value<bool>("/player/hwdec", true) ? "auto-safe" : "no");
    m.set("slang", Config::get().value<std::string>("/player/subLang", "en,eng"));
    m.set("sub-font-size", std::to_string(static_cast<int>(Config::get().value<double>("/player/subSize", 46))));
    // Where the one before was up to, kept for next time
    if (Config::get().value<bool>("/player/resume", true)) m.command({"write-watch-later-config"});
    st->mpv->command({"loadfile", st->playlist[st->index].c_str(), "replace"});
    st->mpv->setFlag("pause", false);
    if (st->empty) st->empty->setVisible(false);
    if (st->stage) st->stage->setVisible(true);
    if (st->controls) st->controls->setVisible(true);
    if (st->refresh) st->refresh();
}

void play(const std::shared_ptr<PlayerState>& st, const std::string& file) {
    // A subtitle file (opened from Files) goes with what's playing
    if (subtitleFile(file)) {
        if (!st->mpv || st->playlist.empty()) return; // nothing playing to go with
        st->mpv->command({"sub-add", file.c_str(), "select"});
        m3::run({"notify-send", "-a", "Videos", "-i", "subtitles", "Subtitles added", fs::path(file).filename().string()}, nullptr);
        return;
    }
    st->playlist = siblings(file);
    const auto it = std::ranges::find(st->playlist, file);
    load(st, static_cast<int>(it - st->playlist.begin()));
}

void showControls(const std::shared_ptr<PlayerState>& st) {
    if (!st->controls) return;
    if (!st->controls->visible() && !st->playlist.empty()) st->controls->setVisible(true);
    if (st->hide) EventLoop::get().cancel(st->hide), st->hide = 0;
    // Full screen and playing: the bar steps aside after a moment
    if (st->fullscreen && !st->paused)
        st->hide = EventLoop::get().addTimer(2500, [st] {
            st->hide = 0;
            if (*st->alive && st->fullscreen && !st->paused && st->controls) st->controls->setVisible(false);
        });
}

void toggleFullscreen(const std::shared_ptr<PlayerState>& st) {
    for (const auto& w : Compositor::get().windows(true))
        if (w.appId == "material3ui-player") {
            m3::run({"niri", "msg", "action", "fullscreen-window", "--id", w.id}, nullptr);
            st->fullscreen = !st->fullscreen;
            showControls(st);
            return;
        }
}

void screenshot(const std::shared_ptr<PlayerState>& st) {
    if (!st->mpv || st->playlist.empty()) return;
    const char* pics = std::getenv("XDG_PICTURES_DIR");
    const std::string dir = (pics && *pics ? std::string(pics) : paths::home() + "/Pictures") + "/Screenshots";
    std::error_code ec;
    fs::create_directories(dir, ec);
    char stamp[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", std::localtime(&t));
    const std::string file = std::format("{}/{}_{}.png", dir, fs::path(st->playlist[st->index]).stem().string(), stamp);
    st->mpv->command({"screenshot-to-file", file.c_str(), "video"});
    m3::run({"notify-send", "-a", "Videos", "-i", file, "Frame saved", fileops::displayPath(file)}, nullptr);
}

// The audio or subtitle tracks, as a menu
void trackMenu(const std::shared_ptr<PlayerState>& st, Node* from, const std::string& type) {
    if (!st->mpv) return;
    mpv_node list;
    if (mpv_get_property(st->mpv->handle, "track-list", MPV_FORMAT_NODE, &list) < 0) return;
    std::vector<MenuItem> items;
    bool any = false;
    if (type == "sub") items.emplace_back("subtitles_off", "Off", "", [st] { st->mpv->set("sid", "no"); }, false);
    if (list.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < list.u.list->num; ++i) {
            const mpv_node& t = list.u.list->values[i];
            if (t.format != MPV_FORMAT_NODE_MAP) continue;
            std::string ttype, title, lang, codec;
            int64_t id = 0;
            bool selected = false;
            for (int j = 0; j < t.u.list->num; ++j) {
                const std::string key = t.u.list->keys[j];
                const mpv_node& v = t.u.list->values[j];
                if (key == "type" && v.format == MPV_FORMAT_STRING) ttype = v.u.string;
                else if (key == "title" && v.format == MPV_FORMAT_STRING) title = v.u.string;
                else if (key == "lang" && v.format == MPV_FORMAT_STRING) lang = v.u.string;
                else if (key == "codec" && v.format == MPV_FORMAT_STRING) codec = v.u.string;
                else if (key == "id" && v.format == MPV_FORMAT_INT64) id = v.u.int64;
                else if (key == "selected" && v.format == MPV_FORMAT_FLAG) selected = v.u.flag;
            }
            if (ttype != type) continue;
            any = true;
            std::string label = title.empty() ? std::format("Track {}", id) : title;
            if (!lang.empty()) label += "  ·  " + lang;
            const std::string prop = type == "sub" ? "sid" : "aid";
            items.emplace_back(type == "sub" ? "subtitles" : "music_note", label, codec, [st, prop, id] { st->mpv->set(prop.c_str(), std::to_string(id)); }, selected);
        }
    }
    mpv_free_node_contents(&list);
    if (type == "sub") {
        items[0].checked = std::none_of(items.begin() + 1, items.end(), [](const MenuItem& m) { return m.checked; });
        auto step = [st](const char* prop, double by, const char* label) {
            return MenuItem("", label, "", [st, prop, by] {
                if (st->mpv) st->mpv->command({"add", prop, std::format("{}", by).c_str()});
            });
        };
        double delay = 0, scale = 1;
        mpv_get_property(st->mpv->handle, "sub-delay", MPV_FORMAT_DOUBLE, &delay);
        mpv_get_property(st->mpv->handle, "sub-scale", MPV_FORMAT_DOUBLE, &scale);
        auto earlier = step("sub-delay", -0.1, "Show earlier (Z)");
        earlier.icon = "fast_rewind", earlier.hint = std::format("{:+.1f} s", delay), earlier.separatorBefore = true;
        items.push_back(earlier);
        auto later = step("sub-delay", 0.1, "Show later (X)");
        later.icon = "fast_forward";
        items.push_back(later);
        auto bigger = step("sub-scale", 0.1, "Bigger (Shift+G)");
        bigger.icon = "text_increase", bigger.hint = std::format("{:.0f}%", scale * 100);
        items.push_back(bigger);
        auto smaller = step("sub-scale", -0.1, "Smaller (Shift+F)");
        smaller.icon = "text_decrease";
        items.push_back(smaller);
        auto up = step("sub-pos", -5, "Move up (R)");
        up.icon = "vertical_align_top";
        items.push_back(up);
        auto down = step("sub-pos", 5, "Move down (T)");
        down.icon = "vertical_align_bottom";
        items.push_back(down);
        items.emplace_back("folder_open", "Load a subtitle file…", "Open it in Files", [st] {
            // Beside the video, the subtitle file of the same name is found
            // by itself; any other through Files
            if (!st->playlist.empty() && open) open("files " + fs::path(st->playlist[st->index]).parent_path().string());
        }, false, false, true, true);
    }
    if (!any && type != "sub") items.emplace_back("music_off", "No audio tracks", "", [] {}, false, false, false);
    floatingMenu(from, items);
}

void speedMenu(const std::shared_ptr<PlayerState>& st, Node* from) {
    std::vector<MenuItem> items;
    for (double s : {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 3.0})
        items.emplace_back("speed", s == 1.0 ? "Normal" : std::format("{:g}×", s), "", [st, s] {
            if (st->mpv) st->mpv->set("speed", s);
        }, std::abs(st->speed - s) < 0.01);
    floatingMenu(from, items);
}

Icon* iconOf(Button* b) {
    for (auto& c : b->children())
        if (auto* i = dynamic_cast<Icon*>(c.get())) return i;
    return nullptr;
}

void handleEvents(const std::shared_ptr<PlayerState>& st) {
    if (!st->mpv) return;
    bool changed = false;
    while (true) {
        mpv_event* e = mpv_wait_event(st->mpv->handle, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        if (e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            const auto* p = static_cast<mpv_event_property*>(e->data);
            const std::string name = p->name;
            if (p->format == MPV_FORMAT_DOUBLE) {
                const double v = *static_cast<double*>(p->data);
                if (name == "time-pos") st->pos = v;
                else if (name == "duration") st->length = v;
                else if (name == "volume") st->vol = v;
                else if (name == "speed") st->speed = v;
            } else if (p->format == MPV_FORMAT_FLAG) {
                const bool v = *static_cast<int*>(p->data);
                if (name == "pause") st->paused = v, showControls(st);
                else if (name == "mute") st->muted = v;
                else if (name == "eof-reached") st->eof = v;
                else if (name == "vo-configured" && st->view) st->view->hasVideo = v, st->view->requestRedraw();
            } else if (p->format == MPV_FORMAT_INT64) {
                const int64_t v = *static_cast<int64_t*>(p->data);
                if (name == "width") st->vw = v;
                else if (name == "height") st->vh = v;
            } else if (p->format == MPV_FORMAT_STRING) {
                if (name == "media-title") st->mediaTitle = *static_cast<char**>(p->data);
            }
            changed = true;
        } else if (e->event_id == MPV_EVENT_END_FILE) {
            const auto* end = static_cast<mpv_event_end_file*>(e->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                const std::string what = st->playlist.empty() ? "" : fs::path(st->playlist[st->index]).filename().string();
                m3::run({"notify-send", "-a", "Videos", "-i", "dialog-error", "Can't play " + what, mpv_error_string(end->error)}, nullptr);
            }
        }
    }
    // At the end, on to the next in the folder (the last one stays)
    if (st->eof && st->index + 1 < static_cast<int>(st->playlist.size()) && Config::get().value<bool>("/player/autoNext", true)) {
        st->eof = false;
        load(st, st->index + 1);
        return;
    }
    if (changed && st->refresh) st->refresh();
}

} // namespace

void player(Flex* card, Popup& popup, const std::string& file) {
    auto st = std::make_shared<PlayerState>();
    st->popup = &popup;
    card->setPadding(Insets(10))->setSpacing(8);
    st->mpv = Mpv::create();
    if (!st->mpv) {
        caption(card, "The video player couldn't start: libmpv didn't initialise (is the mpv package installed?)");
        return;
    }

    // Nothing to play yet: a way to some
    st->empty = card->add<Flex>(Flex::Direction::Column);
    st->empty->setAlignItems(Align::Center)->setJustify(Align::Center)->setSpacing(14);
    st->empty->grow = true;
    st->empty->add<Icon>("smart_display", 72)->setColor("primary", false);
    st->empty->add<Label>("Videos")->setSize(26)->setWeight(500);
    caption(st->empty, "Open a video or a song from Files, or drop its address here: material3ui msg play <file or URL>");
    pillButton(st->empty, "Open your Videos folder", true, [] {
        const char* v = std::getenv("XDG_VIDEOS_DIR");
        if (open) open("files " + (v && *v ? std::string(v) : paths::home() + "/Videos"));
    }, "folder_open");

    st->stage = card->add<Flex>(Flex::Direction::Column);
    st->stage->setAlignItems(Align::Stretch);
    st->stage->grow = true;
    st->view = st->stage->add<VideoView>(st->mpv);
    st->view->grow = true;

    // The bar: where it's up to, then what to do
    st->controls = card->add<Flex>(Flex::Direction::Column);
    st->controls->setSpacing(2)->setAlignItems(Align::Stretch)->setPadding({8, 0, 8, 2});
    auto* seekRow = st->controls->add<Flex>(Flex::Direction::Row);
    seekRow->setSpacing(12)->setAlignItems(Align::Center);
    st->time = seekRow->add<Label>("0:00");
    st->time->setSize(12)->setColor("on_surface_variant", false);
    st->time->fixedWidth = 56;
    st->seek = seekRow->add<Slider>();
    st->seek->setRange(0, 1);
    st->seek->grow = true;
    st->seek->onChanged = [st](float v) {
        st->seeking = true;
        st->time->setText(clock(v * st->length));
    };
    st->seek->onCommitted = [st](float v) {
        st->seeking = false;
        if (st->mpv && st->length > 0) st->mpv->command({"seek", std::format("{}", v * 100).c_str(), "absolute-percent"});
    };
    st->duration = seekRow->add<Label>("0:00");
    st->duration->setSize(12)->setColor("on_surface_variant", false);
    st->duration->fixedWidth = 56;

    auto* row = st->controls->add<Flex>(Flex::Direction::Row);
    row->setSpacing(4)->setAlignItems(Align::Center);
    iconButton(row, "skip_previous", [st] {
        // Back to the start first, as players do; at the start, the one before
        if (st->pos > 3 || st->playlist.size() < 2) st->mpv->command({"seek", "0", "absolute"});
        else load(st, st->index - 1);
    }, 24)->tooltip = "Previous (P)";
    auto* playBtn = row->add<Button>();
    playBtn->setPadding(Insets(10))->setRadius(Radii(18));
    playBtn->setBackground("primary", false);
    st->playIcon = playBtn->add<Icon>("pause", 28);
    st->playIcon->setFill(true)->setColor("on_primary", false);
    playBtn->onClicked = [st](uint32_t b) {
        if (b == BTN_LEFT && st->mpv) st->mpv->setFlag("pause", !st->paused);
    };
    playBtn->tooltip = "Play or pause (Space)";
    iconButton(row, "skip_next", [st] { load(st, st->index + 1); }, 24)->tooltip = "Next (N)";
    row->add<Node>()->fixedWidth = 10;
    auto* texts = row->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(1);
    texts->grow = true;
    texts->givesWay = true;
    st->title = texts->add<Label>("");
    st->title->setSize(14)->setWeight(600)->setMaxWidth(420);
    st->where = texts->add<Label>("");
    st->where->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420);
    auto* volBtn = iconButton(row, "volume_up", [st] {
        if (st->mpv) st->mpv->setFlag("mute", !st->muted);
    }, 22);
    volBtn->tooltip = "Mute (M)";
    st->volumeIcon = iconOf(volBtn);
    st->volume = row->add<Slider>();
    st->volume->setRange(0, 130);
    st->volume->fixedWidth = 110;
    st->volume->onChanged = [st](float v) {
        if (st->mpv) st->mpv->set("volume", static_cast<double>(v));
    };
    auto* speedBtn = row->add<Button>();
    pill(speedBtn->setBackground("surface_container_high", false))->setPadding({12, 6});
    st->speedLabel = speedBtn->add<Label>("1×");
    st->speedLabel->setSize(13)->setWeight(500);
    speedBtn->tooltip = "Speed ([ and ])";
    speedBtn->onClicked = [st, speedBtn](uint32_t b) {
        if (b == BTN_LEFT) speedMenu(st, speedBtn);
    };
    Button* subBtn = iconButton(row, "subtitles", [] {}, 22);
    subBtn->onClicked = [st, subBtn](uint32_t b) {
        if (b == BTN_LEFT) trackMenu(st, subBtn, "sub");
    };
    subBtn->tooltip = "Subtitles (C)";
    Button* audioBtn = iconButton(row, "graphic_eq", [] {}, 22);
    audioBtn->onClicked = [st, audioBtn](uint32_t b) {
        if (b == BTN_LEFT) trackMenu(st, audioBtn, "audio");
    };
    audioBtn->tooltip = "Audio track (A)";
    iconButton(row, "photo_camera", [st] { screenshot(st); }, 22)->tooltip = "Save this frame (S)";
    iconButton(row, "folder_open", [st] {
        if (!st->playlist.empty() && st->playlist[st->index].find("://") == std::string::npos && open) open("files " + st->playlist[st->index]);
    }, 22)->tooltip = "Show in Files";
    iconButton(row, "fullscreen", [st] { toggleFullscreen(st); }, 24)->tooltip = "Full screen (F, or double click)";

    card->add<PlayerKeeper>(st);

    st->refresh = [st] {
        if (!st->view) return;
        st->playIcon->setText(st->paused || st->eof ? "play_arrow" : "pause");
        st->time->setText(clock(st->seeking ? st->seek->value() * st->length : st->pos));
        st->duration->setText(clock(st->length));
        if (!st->seeking) st->seek->setValue(st->length > 0 ? static_cast<float>(st->pos / st->length) : 0, false);
        st->volume->setValue(static_cast<float>(st->vol), false);
        if (st->volumeIcon) st->volumeIcon->setText(st->muted || st->vol <= 0 ? "volume_off" : st->vol < 50 ? "volume_down" : "volume_up");
        st->speedLabel->setText(std::abs(st->speed - 1) < 0.01 ? "1×" : std::format("{:g}×", st->speed));
        if (st->playlist.empty()) return;
        const std::string f = st->playlist[st->index];
        const bool url = f.find("://") != std::string::npos;
        st->title->setText(!st->mediaTitle.empty() ? st->mediaTitle : url ? f : fs::path(f).filename().string());
        std::string where = url ? "From the web" : fileops::displayPath(fs::path(f).parent_path().string());
        if (st->playlist.size() > 1) where = std::format("{} of {}  ·  {}", st->index + 1, st->playlist.size(), where);
        if (st->vw > 0) where += std::format("  ·  {}×{}", st->vw, st->vh);
        st->where->setText(where);
        st->view->placeholder = st->view->hasVideo ? "" : "music";
    };

    auto& mpv = *st->mpv;
    for (const auto& [name, format] : std::initializer_list<std::pair<const char*, mpv_format>>{
             {"time-pos", MPV_FORMAT_DOUBLE}, {"duration", MPV_FORMAT_DOUBLE}, {"volume", MPV_FORMAT_DOUBLE}, {"speed", MPV_FORMAT_DOUBLE},
             {"pause", MPV_FORMAT_FLAG}, {"mute", MPV_FORMAT_FLAG}, {"eof-reached", MPV_FORMAT_FLAG}, {"vo-configured", MPV_FORMAT_FLAG},
             {"width", MPV_FORMAT_INT64}, {"height", MPV_FORMAT_INT64}, {"media-title", MPV_FORMAT_STRING}})
        mpv_observe_property(mpv.handle, 0, name, format);
    mpv.onEvents = [st] {
        if (*st->alive) handleEvents(st);
    };
    mpv.onFrame = [st] {
        if (*st->alive && st->view) st->view->requestRedraw();
    };
    st->view->onActivity = [st] { showControls(st); };
    st->view->onToggle = [st] {
        if (st->mpv) st->mpv->setFlag("pause", !st->paused);
    };
    st->view->onFullscreen = [st] { toggleFullscreen(st); };
    st->view->onWheel = [st](int steps) {
        if (st->mpv) st->mpv->set("volume", std::clamp(st->vol - steps * 5.0, 0.0, 130.0));
    };

    auto& p = popup;
    popup.onKey = [st, &p](const KeyEvent& e) {
        if (!st->mpv) return false;
        auto& m = *st->mpv;
        showControls(st);
        switch (e.sym) {
        case XKB_KEY_space:
        case XKB_KEY_k: m.setFlag("pause", !st->paused); return true;
        case XKB_KEY_Left: m.command({"seek", e.shift() ? "-30" : "-5", "relative"}); return true;
        case XKB_KEY_Right: m.command({"seek", e.shift() ? "30" : "5", "relative"}); return true;
        case XKB_KEY_j: m.command({"seek", "-10", "relative"}); return true;
        case XKB_KEY_l: m.command({"seek", "10", "relative"}); return true;
        case XKB_KEY_Home: m.command({"seek", "0", "absolute"}); return true;
        case XKB_KEY_comma: m.command({"frame-back-step"}); return true;
        case XKB_KEY_period: m.command({"frame-step"}); return true;
        case XKB_KEY_Up: m.set("volume", std::min(130.0, st->vol + 5)); return true;
        case XKB_KEY_Down: m.set("volume", std::max(0.0, st->vol - 5)); return true;
        case XKB_KEY_m: m.setFlag("mute", !st->muted); return true;
        case XKB_KEY_f:
        case XKB_KEY_F11: toggleFullscreen(st); return true;
        case XKB_KEY_n:
        case XKB_KEY_Page_Down: load(st, st->index + 1); return true;
        case XKB_KEY_p:
        case XKB_KEY_Page_Up: load(st, st->index - 1); return true;
        case XKB_KEY_bracketleft: m.set("speed", std::max(0.25, st->speed - 0.25)); return true;
        case XKB_KEY_bracketright: m.set("speed", std::min(4.0, st->speed + 0.25)); return true;
        case XKB_KEY_BackSpace: m.set("speed", 1.0); return true;
        case XKB_KEY_s: screenshot(st); return true;
        case XKB_KEY_c: m.command({"cycle", "sub"}); return true;
        case XKB_KEY_v: m.command({"cycle", "sub-visibility"}); return true;
        case XKB_KEY_z: m.command({"add", "sub-delay", "-0.1"}); return true;
        case XKB_KEY_x: m.command({"add", "sub-delay", "0.1"}); return true;
        case XKB_KEY_G: m.command({"add", "sub-scale", "0.1"}); return true;
        case XKB_KEY_F: m.command({"add", "sub-scale", "-0.1"}); return true;
        case XKB_KEY_r: m.command({"add", "sub-pos", "-5"}); return true;
        case XKB_KEY_t: m.command({"add", "sub-pos", "5"}); return true;
        case XKB_KEY_a: m.command({"cycle", "audio"}); return true;
        case XKB_KEY_Escape:
            if (st->fullscreen) toggleFullscreen(st);
            else p.close();
            return true;
        default:
            // 0–9: a tenth of the way through each
            if (e.sym >= XKB_KEY_0 && e.sym <= XKB_KEY_9) {
                m.command({"seek", std::format("{}", (e.sym - XKB_KEY_0) * 10).c_str(), "absolute-percent"});
                return true;
            }
            return false;
        }
    };

    playerGo = [st](const std::string& f) {
        if (*st->alive) play(st, f);
    };
    if (file.empty() || subtitleFile(file)) {
        st->stage->setVisible(false);
        st->controls->setVisible(false);
    } else {
        st->empty->setVisible(false);
        play(st, file);
    }
}

void playerClosed() {
    playerGo = nullptr;
}

// Settings → Videos
void playerSettings(Flex* page) {
    auto* g = group(page, "Playback");
    switchRow(g, "memory", "Hardware decoding", "The graphics card decodes, where it can (less power, cooler)", "/player/hwdec", true);
    switchRow(g, "history", "Resume where you left off", "A video opened again starts where it stopped", "/player/resume", true);
    switchRow(g, "playlist_play", "Play the next one", "When a video ends, the folder's next starts", "/player/autoNext", true);
    sliderRow(g, "volume_up", "Starting volume", "/player/volume", 100, 0, 130, 5, [](double v) { return std::format("{:.0f}%", v); });
    auto* sub = group(page, "Subtitles");
    textRow(sub, "translate", "Preferred languages", "/player/subLang", "en,eng");
    caption(sub, "  Language codes, in order: en,eng or ta,tam,en (Enter saves)");
    sliderRow(sub, "format_size", "Size", "/player/subSize", 46, 24, 90, 2, [](double v) { return std::format("{:.0f}", v); });
    caption(page, "They apply from the next video you open");
}

} // namespace m3::shell::panels
