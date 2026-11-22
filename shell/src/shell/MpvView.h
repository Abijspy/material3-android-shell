#pragma once
// libmpv drawing into a texture of the shell's, and the node that shows it:
// what Videos and Camera share. Header-only: the shell's library doesn't
// link libmpv, the apps that include this do.

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "render/Renderer.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <EGL/egl.h>
#include <filesystem>
#include <functional>
#include <linux/input-event-codes.h>
#include <memory>
#include <string>
#include <vector>

namespace m3::shell::panels::mpvview {

using namespace m3::ui;

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

    // `player`: Videos' own settings (resume, volume); `extra` options after the defaults
    static std::shared_ptr<Mpv> create(bool player = true, const std::vector<std::pair<std::string, std::string>>& extra = {}) {
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
        if (player && Config::get().value<bool>("/player/resume", true)) {
            const std::string later = paths::stateDir() + "/watch_later";
            std::error_code ec;
            std::filesystem::create_directories(later, ec);
            mpv_set_option_string(m->handle, "save-position-on-quit", "yes");
            mpv_set_option_string(m->handle, "watch-later-directory", later.c_str());
            mpv_set_option_string(m->handle, "watch-later-options", "start,volume,mute,sid,aid,sub-delay,speed");
        }
        if (player) mpv_set_option_string(m->handle, "volume", std::to_string(static_cast<int>(Config::get().value<double>("/player/volume", 100))).c_str());
        for (const auto& [k, v] : extra) mpv_set_option_string(m->handle, k.c_str(), v.c_str());
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


} // namespace m3::shell::panels::mpvview
