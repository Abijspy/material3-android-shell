#pragma once

#include "wayland/Surface.h"

struct zwlr_layer_surface_v1;

namespace m3::wl {

// A wlr-layer-shell surface: bars, popups, toasts, wallpapers.
class LayerSurface : public Surface {
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
        int marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
    };

    LayerSurface(Output* output, Options options);
    ~LayerSurface() override;

    void setSize(int width, int height);
    // Edge and size at once, in one commit: moving to another edge in two
    // would leave a size of 0 along an edge that isn't stretched (an error)
    void setPlacement(uint32_t anchor, int width, int height);
    void setMargins(int top, int right, int bottom, int left);
    const Options& options() const { return m_options; }
    void setAnchor(uint32_t anchor);
    void setExclusiveZone(int zone);
    void setKeyboard(Keyboard mode);
    void setVisible(bool visible);
    bool visible() const { return m_visible; }

    std::string describe() const override;

    // Protocol callbacks
    void configure(uint32_t serial, uint32_t width, uint32_t height);
    void closed();

protected:
    void destroyRole() override;

private:
    void create();

    Options m_options;
    zwlr_layer_surface_v1* m_layer = nullptr;
    bool m_visible = true;
};

// One output's surface of the session lock (ext-session-lock-v1).
class LockSurface : public Surface {
public:
    LockSurface(Output* output, void* lock); // lock: ext_session_lock_v1*
    ~LockSurface() override;

    void configure(uint32_t serial, uint32_t width, uint32_t height);

protected:
    void destroyRole() override;

private:
    void* m_lockSurface = nullptr; // ext_session_lock_surface_v1*
};

} // namespace m3::wl
