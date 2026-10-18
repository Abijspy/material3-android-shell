#pragma once

#include "ui/Input.h"
#include "ui/Node.h"

#include <EGL/egl.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct wl_surface;
struct wl_callback;
struct wl_egl_window;
struct zwlr_layer_surface_v1;
struct wp_fractional_scale_v1;
struct wp_viewport;

namespace m3::wl {

struct Output;

// A wlr-layer-shell surface hosting a node tree. It lays the tree out on
// configure, paints only when something asked for it, paces repaints with
// frame callbacks while animations run, renders at the output's fractional
// scale, and routes pointer and keyboard input into the tree.
class LayerSurface : public ui::Host {
public:
    enum class Layer { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    enum Anchor : uint32_t { Top = 1, Bottom = 2, Left = 4, Right = 8 };
    enum class Keyboard { None = 0, Exclusive = 1, OnDemand = 2 };

    struct Options {
        std::string ns = "material3ui";
        Layer layer = Layer::Top;
        uint32_t anchor = 0;
        int width = 0; // 0 along an anchored pair: stretch
        int height = 0;
        int exclusiveZone = 0;
        Keyboard keyboard = Keyboard::None;
    };

    LayerSurface(Output* output, Options options);
    ~LayerSurface() override;

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    void setRoot(std::unique_ptr<ui::Node> root);
    ui::Node* root() const { return m_root.get(); }
    Output* output() const { return m_output; }
    SizeF size() const { return {static_cast<float>(m_width), static_cast<float>(m_height)}; }

    void setSize(int width, int height);
    void setExclusiveZone(int zone);
    void setVisible(bool visible);
    bool visible() const { return m_visible; }

    // Keyboard: the node that receives keys first; keys it doesn't take
    // bubble up its parents, then reach onKey.
    void setFocus(ui::Node* node);
    ui::Node* focus() const { return m_focus; }
    std::function<bool(const ui::KeyEvent&)> onKey;
    // Pointer pressed where no interactive node is (popups close on this)
    std::function<void(float x, float y)> onEmptyClick;

    // ui::Host
    void requestLayout() override;
    void requestRedraw() override;
    void nodeDestroyed(ui::Node* node) override;
    void requestFocus(ui::Node* node) override { setFocus(node); }

    // Input, routed from wl::Display
    void pointerMotion(double x, double y);
    void pointerLeave();
    void pointerButton(uint32_t button, bool pressed);
    void pointerScroll(int direction);
    void keyboardFocus(bool focused);
    void key(const ui::KeyEvent& event);

    static LayerSurface* fromSurface(wl_surface* surface);

    // Protocol callbacks
    void configure(uint32_t serial, uint32_t width, uint32_t height);
    void closed();
    void frameDone();
    void preferredScale(float scale, bool fractional);

private:
    void create();
    void destroy();
    void schedule();
    void render();
    void layout();
    void resizeBuffer();
    int bufferWidth() const;
    int bufferHeight() const;

    Output* m_output;
    Options m_options;
    std::unique_ptr<ui::Node> m_root;

    wl_surface* m_surface = nullptr;
    zwlr_layer_surface_v1* m_layer = nullptr;
    wp_fractional_scale_v1* m_fractional = nullptr;
    wp_viewport* m_viewport = nullptr;
    wl_egl_window* m_eglWindow = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    wl_callback* m_frame = nullptr;

    int m_width = 0, m_height = 0;
    float m_scale = 1;
    bool m_configured = false;
    bool m_visible = true;
    bool m_needsLayout = true;
    bool m_dirty = true;
    bool m_scheduled = false;
    bool m_hasKeyboard = false;

    // Deferred renders check this so a surface destroyed meanwhile is skipped
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    ui::Node* m_hovered = nullptr;
    ui::Node* m_pressed = nullptr;
    ui::Node* m_focus = nullptr;
    float m_pointerX = 0, m_pointerY = 0;
};

} // namespace m3::wl
