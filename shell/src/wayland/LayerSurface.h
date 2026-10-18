#pragma once

#include "ui/Node.h"

#include <EGL/egl.h>
#include <cstdint>
#include <memory>
#include <string>

struct wl_surface;
struct wl_callback;
struct wl_egl_window;
struct zwlr_layer_surface_v1;

namespace m3::wl {

struct Output;

// A wlr-layer-shell surface hosting a node tree. It lays the tree out on
// configure, paints only when something asked for it, and paces repaints with
// frame callbacks while animations run.
class LayerSurface : public ui::Host {
public:
    enum class Layer { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    enum Anchor : uint32_t { Top = 1, Bottom = 2, Left = 4, Right = 8 };

    struct Options {
        std::string ns = "material3ui";
        Layer layer = Layer::Top;
        uint32_t anchor = 0;
        int width = 0;  // 0 along an anchored pair: stretch
        int height = 0;
        int exclusiveZone = 0;
        bool keyboard = false;
    };

    LayerSurface(Output* output, Options options);
    ~LayerSurface() override;

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    void setRoot(std::unique_ptr<ui::Node> root);
    ui::Node* root() const { return m_root.get(); }
    Output* output() const { return m_output; }

    void setSize(int width, int height);
    void setExclusiveZone(int zone);
    void setVisible(bool visible);
    bool visible() const { return m_visible; }

    // ui::Host
    void requestLayout() override;
    void requestRedraw() override;
    void nodeDestroyed(ui::Node* node) override;

    // Input, routed from wl::Display
    void pointerMotion(double x, double y);
    void pointerLeave();
    void pointerButton(uint32_t button, bool pressed);
    void pointerScroll(int direction);

    static LayerSurface* fromSurface(wl_surface* surface);

    // Protocol callbacks
    void configure(uint32_t serial, uint32_t width, uint32_t height);
    void closed();
    void frameDone();
    void preferredScale(int32_t scale);

private:
    void create();
    void destroy();
    void schedule();
    void render();
    void layout();

    Output* m_output;
    Options m_options;
    std::unique_ptr<ui::Node> m_root;

    wl_surface* m_surface = nullptr;
    zwlr_layer_surface_v1* m_layer = nullptr;
    wl_egl_window* m_eglWindow = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    wl_callback* m_frame = nullptr;

    int m_width = 0, m_height = 0, m_scale = 1;
    bool m_configured = false;
    bool m_visible = true;
    bool m_needsLayout = true;
    bool m_dirty = true;
    bool m_scheduled = false;

    // Deferred renders check this so a surface destroyed meanwhile is skipped
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    ui::Node* m_hovered = nullptr;
    ui::Node* m_pressed = nullptr;
};

} // namespace m3::wl
