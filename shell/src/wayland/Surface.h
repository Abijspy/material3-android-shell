#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "ui/Input.h"
#include "ui/Node.h"

#include <EGL/egl.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include <memory>
#include <string>

struct wl_surface;
struct wl_callback;
struct wl_egl_window;
struct wp_fractional_scale_v1;
struct wp_viewport;
struct ext_background_effect_surface_v1;

namespace m3::wl {

struct Output;

// A wl_surface hosting a node tree, independent of its role (layer shell,
// session lock). It lays the tree out on configure, paints only when asked,
// paces repaints with frame callbacks while animations run, renders at the
// output's fractional scale, and routes pointer and keyboard input.
class Surface : public ui::Host {
public:
    explicit Surface(Output* output);
    // It draws its own wallpaper (the desktop, the lock and login screens):
    // blurBehind cards are blurred in the frame, not by the compositor
    bool inlineBlur = false;
    ~Surface() override;

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    void setRoot(std::unique_ptr<ui::Node> root);
    ui::Node* root() const { return m_root.get(); }
    Output* output() const { return m_output; }
    // The pointer came to rest on a node with a tooltip (null: none now, or
    // the pointer left or pressed); the shell shows or hides it
    static inline std::function<void(Surface*, ui::Node*)> tooltipHook;
    SizeF size() const { return {static_cast<float>(m_width), static_cast<float>(m_height)}; }
    float scale() const { return m_scale; }
    const std::string& tag() const { return m_tag; }

    // Where the pointer may hit, in logical px; nullopt (default) = everywhere.
    // An empty list makes the surface click-through.
    void setInputRegion(std::optional<std::vector<RectF>> rects);
    // Tab, Shift+Tab and the arrow keys move between its controls, Enter and
    // Space press them, a ring showing where the keyboard is (the Settings
    // apps; popups keep their keys to themselves)
    bool keyboardNavigation = false;

    // Keyboard: the node that receives keys first; keys it doesn't take
    // bubble up its parents, then reach onKey.
    void setFocus(ui::Node* node);
    ui::Node* focus() const { return m_focus; }
    bool hasKeyboard() const { return m_hasKeyboard; }
    std::function<bool(const ui::KeyEvent&)> onKey;
    // Pointer pressed where no interactive node is (popups close on this)
    std::function<void(float x, float y)> onEmptyClick;

    // ui::Host
    void requestLayout() override;
    void requestRedraw() override;
    void requestAmbientRedraw() override;
    void nodeDestroyed(ui::Node* node) override;
    void requestFocus(ui::Node* node) override { setFocus(node); }

    // Input, routed from wl::Display
    void pointerMotion(double x, double y);
    void pointerLeave();
    void pointerButton(uint32_t button, bool pressed);
    void pointerScroll(int direction);
    // True when a node under the pointer scrolled by pixels
    bool pointerScrollPixels(float dy, bool precise);
    void pointerScrollEnd();
    void keyboardFocus(bool focused);
    void key(const ui::KeyEvent& event);

    static Surface* fromSurface(wl_surface* surface);

    // Every live surface, and a text dump of its tree (for UI tests)
    static const std::vector<Surface*>& all();
    virtual std::string describe() const;
    std::string dump() const;

    // Protocol callbacks
    void frameDone();
    void preferredScale(float scale, bool fractional);

protected:
    // Subclasses create their role after createSurface(), call configured()
    // from their configure handler, and destroy the role in destroyRole().
    void createSurface();
    void destroySurface();
    virtual void destroyRole() {}
    void configured(int width, int height);
    wl_surface* wlSurface() const { return m_surface; }
    bool isConfigured() const { return m_configured; }

    Output* m_output;
    std::string m_tag = "surface";

private:
    void schedule();
    void scheduleAmbient(); // a frame when the ambient frame rate allows
    void render();
    void layout();
    void resizeBuffer();
    int bufferWidth() const;
    int bufferHeight() const;

    std::unique_ptr<ui::Node> m_root;

    wl_surface* m_surface = nullptr;
    wp_fractional_scale_v1* m_fractional = nullptr;
    wp_viewport* m_viewport = nullptr;
    wl_egl_window* m_eglWindow = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    wl_callback* m_frame = nullptr;

    int m_width = 0, m_height = 0;
    float m_scale = 1;
    bool m_configured = false;
    bool m_needsLayout = true;
    bool m_dirty = true;
    bool m_scheduled = false;
    // Only ambient motion running: frames at kAmbientFps, not every vblank
    bool m_ambient = false;
    bool m_painting = false; // ambient requests made now wait for this frame's time
    EventLoop::TimerId m_ambientTimer = 0;
    std::chrono::steady_clock::time_point m_lastFrame{};
    bool m_hasKeyboard = false;

    // Deferred renders check this so a surface destroyed meanwhile is skipped
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    ui::Node* m_hovered = nullptr;
    ui::Node* m_tooltip = nullptr; // the node whose tooltip is up (or coming)
    ui::Node* m_pressed = nullptr;
    ui::Node* m_focus = nullptr;
    bool navigate(const ui::KeyEvent& event); // keyboardNavigation's keys
    float m_pointerX = 0, m_pointerY = 0;
    std::optional<std::vector<RectF>> m_input;
    std::vector<ui::Node*> m_hoverChain; // nodes the pointer is within
    void updateHoverWithin(ui::Node* hit);
    // Blur behind blurBehind nodes, when the compositor offers it
    void updateBlurRegion();
    ext_background_effect_surface_v1* m_blur = nullptr;
    std::vector<RectF> m_blurRects;
    Connection m_imagesReady;
    bool m_inputDirty = false;
};

} // namespace m3::wl
