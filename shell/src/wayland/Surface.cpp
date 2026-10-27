#include "core/Guard.h"
#include "wayland/Surface.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include "ext-background-effect-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cxxabi.h>
#include <format>
#include <typeinfo>
#include <cstdlib>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::wl {

namespace {

const wl_callback_listener kFrame = {
    .done = [](void* data, wl_callback*, uint32_t) { m3::guard("a Wayland event", [&] { static_cast<Surface*>(data)->frameDone(); }); },
};

const wl_surface_listener kSurface = {
    .enter = [](void*, wl_surface*, wl_output*) {},
    .leave = [](void*, wl_surface*, wl_output*) {},
    .preferred_buffer_scale = [](void* data, wl_surface*, int32_t scale) {
        m3::guard("a Wayland event", [&] { static_cast<Surface*>(data)->preferredScale(static_cast<float>(scale), false); });
    },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

const wp_fractional_scale_v1_listener kFractional = {
    .preferred_scale = [](void* data, wp_fractional_scale_v1*, uint32_t scale120) {
        m3::guard("a Wayland event", [&] { static_cast<Surface*>(data)->preferredScale(static_cast<float>(scale120) / 120.0f, true); });
    },
};

bool debugFrames() {
    static const bool on = std::getenv("M3_DEBUG_FRAMES") != nullptr;
    return on;
}

} // namespace

namespace {
std::vector<Surface*>& registry() {
    static std::vector<Surface*> list;
    return list;
}
} // namespace

const std::vector<Surface*>& Surface::all() {
    return registry();
}

std::string Surface::describe() const {
    return std::format("surface {} {}x{}", m_tag, m_width, m_height);
}

std::string Surface::dump() const {
    std::string out = describe() + "\n";
    std::function<void(const ui::Node*, int)> walk = [&](const ui::Node* n, int depth) {
        if (!n->shown()) return;
        int status = 0;
        char* name = abi::__cxa_demangle(typeid(*n).name(), nullptr, nullptr, &status);
        std::string type = status == 0 && name ? name : typeid(*n).name();
        std::free(name);
        if (auto pos = type.rfind("::"); pos != std::string::npos) type = type.substr(pos + 2);
        const auto& r = n->rect();
        std::string text;
        if (auto* l = dynamic_cast<const ui::Label*>(n)) text = l->text();
        out += std::format("{} {} {:.0f} {:.0f} {:.0f} {:.0f} {}{}\n", depth, type, r.x, r.y, r.w, r.h,
                           n->interactive() ? "* " : "", text);
        for (const auto& c : n->children()) walk(c.get(), depth + 1);
    };
    if (m_root) walk(m_root.get(), 0);
    return out;
}

std::string Surface::overflow(int& checked) const {
    std::string out;
    auto typeOf = [](const ui::Node* n) {
        int status = 0;
        char* name = abi::__cxa_demangle(typeid(*n).name(), nullptr, nullptr, &status);
        std::string type = status == 0 && name ? name : typeid(*n).name();
        std::free(name);
        if (auto pos = type.rfind("::"); pos != std::string::npos) type = type.substr(pos + 2);
        if (auto* l = dynamic_cast<const ui::Label*>(n); l && !l->text().empty()) type += " \"" + l->text().substr(0, 40) + "\"";
        return type;
    };
    const RectF screen{0, 0, static_cast<float>(m_width), static_cast<float>(m_height)};
    // How far `r` sticks out of `box` on each side ("" when it doesn't)
    auto outside = [](const RectF& r, const RectF& box) {
        const float l = box.x - r.x, t = box.y - r.y, rt = r.right() - box.right(), b = r.bottom() - box.bottom();
        if (std::max({l, t, rt, b}) <= 1) return std::string();
        std::string sides;
        auto side = [&](float d, const char* name) {
            if (d > 1) sides += std::format("{}{} {:.0f}px", sides.empty() ? "" : ", ", name, d);
        };
        side(l, "left");
        side(t, "top");
        side(rt, "right");
        side(b, "bottom");
        return sides;
    };
    // `onScreen`: not inside a box that clips (a scroll view's content may
    // be scrolled past the surface's edge; it's still checked against the
    // boxes inside it)
    std::function<void(const ui::Node*, const ui::Node*, bool)> walk = [&](const ui::Node* n, const ui::Node* box, bool onScreen) {
        // Hidden, moved on purpose, or empty: nothing to see
        if (!n->shown() || n->opacity() <= 0.01f || n->transformed()) return;
        const RectF& r = n->rect();
        if (r.w <= 0.5f || r.h <= 0.5f) return;
        ++checked;
        if (const auto s = onScreen ? outside(r, screen) : std::string(); !s.empty())
            out += std::format("{}: {} at {:.0f},{:.0f} {:.0f}x{:.0f} is off the surface ({})\n", m_tag, typeOf(n), r.x, r.y, r.w, r.h, s);
        else if (box)
            if (const auto s = outside(r, box->rect()); !s.empty())
                out += std::format("{}: {} at {:.0f},{:.0f} {:.0f}x{:.0f} sticks out of its {} ({})\n", m_tag, typeOf(n), r.x, r.y, r.w, r.h,
                                   typeOf(box), s);
        // A box that clips (a scroll view): what's in it can't draw outside
        // it, but can still stick out of the boxes inside it
        const auto* flex = dynamic_cast<const ui::Flex*>(n);
        const ui::Node* next = n->clips() ? nullptr : flex && flex->paintsBox() ? n : box;
        for (const auto& c : n->children()) walk(c.get(), next, onScreen && !n->clips());
    };
    if (m_root) walk(m_root.get(), nullptr, true);
    return out;
}

Surface::Surface(Output* output) : m_output(output) {
    registry().push_back(this);
    m_scale = output ? static_cast<float>(output->scale) : 1.0f;
    m_imagesReady = Images::get().ready.connect([this] { requestRedraw(); });
}

Surface::~Surface() {
    std::erase(registry(), this);
    *m_alive = false;
    if (m_ambientTimer) EventLoop::get().cancel(m_ambientTimer);
    // Subclasses call destroySurface() in their destructors (their role
    // must go first); this is the safety net.
    destroySurface();
    m_root.reset();
}

void Surface::createSurface() {
    auto& d = Display::get();
    m_surface = wl_compositor_create_surface(d.compositor());
    wl_surface_add_listener(m_surface, &kSurface, this);
    // Fractional scale needs a viewport to map the buffer to logical size
    if (d.fractionalScale() && d.viewporter()) {
        m_fractional = wp_fractional_scale_manager_v1_get_fractional_scale(d.fractionalScale(), m_surface);
        wp_fractional_scale_v1_add_listener(m_fractional, &kFractional, this);
        m_viewport = wp_viewporter_get_viewport(d.viewporter(), m_surface);
    }
    m_configured = false;
}

void Surface::destroySurface() {
    if (!m_surface) return;
    Display::get().forget(this);
    m_hovered = m_pressed = nullptr;
    m_hasKeyboard = false;
    if (m_frame) wl_callback_destroy(m_frame);
    m_frame = nullptr;
    if (m_eglSurface != EGL_NO_SURFACE) Renderer::get().destroySurface(m_eglSurface);
    m_eglSurface = EGL_NO_SURFACE;
    if (m_eglWindow) wl_egl_window_destroy(m_eglWindow);
    m_eglWindow = nullptr;
    if (m_viewport) wp_viewport_destroy(m_viewport);
    m_viewport = nullptr;
    if (m_blur) ext_background_effect_surface_v1_destroy(m_blur);
    m_blur = nullptr;
    m_blurRects.clear();
    if (m_fractional) wp_fractional_scale_v1_destroy(m_fractional);
    m_fractional = nullptr;
    destroyRole();
    wl_surface_destroy(m_surface);
    m_surface = nullptr;
    m_configured = false;
}

Surface* Surface::fromSurface(wl_surface* surface) {
    return surface ? static_cast<Surface*>(wl_surface_get_user_data(surface)) : nullptr;
}

void Surface::setRoot(std::unique_ptr<ui::Node> root) {
    // The old tree goes: a tooltip shown or waiting for one of its nodes
    // goes with it (the dock rebuilds under the pointer)
    if (m_tooltip && tooltipHook) tooltipHook(this, nullptr);
    m_hovered = m_pressed = m_focus = m_tooltip = nullptr;
    m_root = std::move(root);
    m_root->setHost(this);
    requestLayout();
}

int Surface::bufferWidth() const {
    return std::max(1, static_cast<int>(std::lround(m_width * m_scale)));
}

int Surface::bufferHeight() const {
    return std::max(1, static_cast<int>(std::lround(m_height * m_scale)));
}

void Surface::resizeBuffer() {
    // Nothing to size before the first configure (and 0×0 is a protocol error)
    if (m_width <= 0 || m_height <= 0) return;
    if (m_eglWindow) wl_egl_window_resize(m_eglWindow, bufferWidth(), bufferHeight(), 0, 0);
    if (m_viewport) wp_viewport_set_destination(m_viewport, m_width, m_height);
}

void Surface::configured(int width, int height) {
    m_width = width;
    m_height = height;
    if (!m_eglWindow) {
        m_eglWindow = wl_egl_window_create(m_surface, bufferWidth(), bufferHeight());
        m_eglSurface = Renderer::get().createSurface(m_eglWindow);
    }
    resizeBuffer();
    m_inputDirty = m_input.has_value();
    m_configured = true;
    m_needsLayout = m_dirty = true;
    // A configured surface must commit a buffer to be shown
    render();
}

void Surface::preferredScale(float scale, bool fractional) {
    // Fractional-capable compositors also send the integer hint; ignore it
    if (m_fractional && !fractional) return;
    if (scale <= 0 || scale == m_scale) return;
    m_scale = scale;
    resizeBuffer();
    requestLayout();
}

void Surface::requestLayout() {
    m_needsLayout = true;
    requestRedraw();
}

void Surface::requestRedraw() {
    m_dirty = true;
    schedule();
}

void Surface::nodeDestroyed(ui::Node* node) {
    std::erase(m_hoverChain, node);
    if (m_hovered == node) m_hovered = nullptr;
    if (m_tooltip == node) {
        m_tooltip = nullptr;
        if (tooltipHook) tooltipHook(this, nullptr);
    }
    if (m_pressed == node) m_pressed = nullptr;
    if (m_focus == node) m_focus = nullptr;
    if (ui::Node::focusRing == node) ui::Node::focusRing = nullptr;
}

void Surface::schedule() {
    // A frame callback in flight will repaint when it fires
    if (m_scheduled || m_frame || !m_configured) return;
    m_scheduled = true;
    EventLoop::get().defer([this, alive = m_alive] {
        if (!*alive) return;
        m_scheduled = false;
        if (m_dirty) render();
    });
}

void Surface::frameDone() {
    wl_callback_destroy(m_frame);
    m_frame = nullptr;
    if (m_dirty) render();
    else if (m_ambient) scheduleAmbient();
}

void Surface::requestAmbientRedraw() {
    m_ambient = true;
    // While painting the frame isn't timed yet: render() schedules it after,
    // a whole ambient step on (from here it would come at once, and the
    // calm rate would become the display's)
    if (!m_frame && !m_painting) scheduleAmbient(); // else frameDone does
}

void Surface::scheduleAmbient() {
    // Waves and scrolling text read as smooth at 30 fps; drawing them at
    // the display's 60–144 only heats the CPU, the GPU and the compositor
    constexpr int kAmbientFps = 30;
    if (m_ambientTimer || m_dirty) return;
    const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_lastFrame).count();
    const int wait = std::max(0, static_cast<int>(1000 / kAmbientFps - since));
    m_ambientTimer = EventLoop::get().addTimer(wait, [this, alive = m_alive] {
        if (!*alive) return;
        m_ambientTimer = 0;
        m_dirty = true;
        render();
    });
}

void Surface::layout() {
    if (!m_root) return;
    // Sizes may depend on things that changed without a requestLayout
    // (fields set directly, theme fonts): measure afresh, once per node
    ui::Node::invalidateMeasures();
    const SizeF size{static_cast<float>(m_width), static_cast<float>(m_height)};
    m_root->measure(size);
    m_root->arrange({0, 0, size.w, size.h});
    m_needsLayout = false;
}

void Surface::render() {
    if (!m_configured || m_eglSurface == EGL_NO_SURFACE) return;
    // One frame in flight at a time: the pending callback repaints when it fires
    if (m_frame) {
        m_dirty = true;
        return;
    }
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    const bool laidOut = m_needsLayout;
    const long measuresBefore = ui::Node::measureCount();
    if (m_needsLayout) layout();
    const auto t1 = Clock::now();
    m_dirty = false;

    auto& r = Renderer::get();
    r.begin(m_eglSurface, bufferWidth(), bufferHeight(), m_scale);
    r.inlineBlur = inlineBlur && Theme::get().widgetBlur() > 0 ? Theme::get().widgetBlur() : 0;
    m_ambient = false;
    m_painting = true;
    if (m_root) m_root->paint(r);
    m_painting = false;
    r.inlineBlur = 0;
    const auto t2 = Clock::now();
    // Painting may flag more work (running animations); that becomes the
    // next frame, or, for ambient motion alone, one at the ambient rate
    const auto motion = m_root ? m_root->motion() : ui::Node::Motion::None;
    m_dirty = m_dirty || motion == ui::Node::Motion::Live;
    m_ambient = m_ambient || motion == ui::Node::Motion::Ambient;
    m_lastFrame = t0;
    if (debugFrames()) {
        auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
        log::info("frame {} {}x{}@{} dirty={} motion={} layout={} {:.2f}ms ({} measures) paint {:.2f}ms", m_tag, m_width, m_height,
                  m_scale, m_dirty, static_cast<int>(motion), laidOut, ms(t1 - t0), ui::Node::measureCount() - measuresBefore,
                  ms(t2 - t1));
    }

    m_frame = wl_surface_frame(m_surface);
    wl_callback_add_listener(m_frame, &kFrame, this);
    if (!m_viewport) wl_surface_set_buffer_scale(m_surface, static_cast<int>(m_scale));
    if (m_inputDirty) {
        m_inputDirty = false;
        if (!m_input) {
            wl_surface_set_input_region(m_surface, nullptr);
        } else {
            wl_region* region = wl_compositor_create_region(Display::get().compositor());
            for (const auto& r : *m_input)
                wl_region_add(region, static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(std::ceil(r.w)),
                              static_cast<int>(std::ceil(r.h)));
            wl_surface_set_input_region(m_surface, region);
            wl_region_destroy(region);
        }
    }
    updateBlurRegion();
    r.end(m_eglSurface); // eglSwapBuffers commits the surface
    TextEngine::get().collect();
}

namespace {

// wl_region is rectangles only: a rounded rect becomes a stack of bands whose
// insets follow the corner arc, kept inside the curve so no blur shows past it
void addRounded(std::vector<RectF>& out, const RectF& r, float radius) {
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    if (radius < 2) {
        out.push_back(r);
        return;
    }
    // About a band per 2 px of radius: steps fine enough that nothing
    // behind peeks out around the curve
    const int bands = std::clamp(static_cast<int>(radius / 2), 2, 16);
    for (int i = 0; i < bands; ++i) {
        const float d0 = radius * static_cast<float>(i) / static_cast<float>(bands);
        const float d1 = radius * static_cast<float>(i + 1) / static_cast<float>(bands);
        const float dy = radius - d0; // distance from the arc's centre line
        const float inset = radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
        out.push_back({r.x + inset, r.y + d0, r.w - inset * 2, d1 - d0});
        out.push_back({r.x + inset, r.bottom() - d1, r.w - inset * 2, d1 - d0});
    }
    out.push_back({r.x, r.y + radius, r.w, r.h - radius * 2});
}

// Where it's drawn: moved with its ancestors (a dock sliding away takes its
// blur with it), and none for what's fading out
void collectBlur(const ui::Node* n, std::vector<RectF>& out, float dx = 0, float dy = 0) {
    if (!n->shown() || n->opacity() <= 0.01f) return;
    dx += n->translateX();
    dy += n->translateY();
    if (n->blurBehind && n->rect().w > 0 && n->rect().h > 0) {
        RectF r = n->rect();
        r.x += dx;
        r.y += dy;
        addRounded(out, r, n->cornerRadius());
        return; // children sit on top of it
    }
    for (const auto& c : n->children()) collectBlur(c.get(), out, dx, dy);
}

} // namespace

void Surface::updateBlurRegion() {
    // Blurred by its alpha already: a region would only add the steps
    if (Display::get().shapedBlur(m_tag)) {
        if (m_blur) {
            ext_background_effect_surface_v1_destroy(m_blur);
            m_blur = nullptr;
            m_blurRects.clear();
        }
        return;
    }
    auto* manager = Display::get().backgroundEffect();
    std::vector<RectF> rects;
    if (manager && m_root && Theme::get().blur()) collectBlur(m_root.get(), rects);
    auto same = [](const std::vector<RectF>& a, const std::vector<RectF>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const RectF& x, const RectF& y) {
                   return x.x == y.x && x.y == y.y && x.w == y.w && x.h == y.h;
               });
    };
    if (same(rects, m_blurRects) && (m_blur || rects.empty())) return;
    if (!m_blur) {
        if (!manager) return;
        m_blur = ext_background_effect_manager_v1_get_background_effect(manager, m_surface);
    }
    m_blurRects = std::move(rects);
    if (m_blurRects.empty()) {
        ext_background_effect_surface_v1_set_blur_region(m_blur, nullptr);
        return;
    }
    wl_region* region = wl_compositor_create_region(Display::get().compositor());
    // The corner bands share edges: rounding both ends the same way makes
    // them tile exactly (shrinking each left 1 px gaps, where the window
    // behind showed through unblurred as thin lines). Sideways they stay
    // inside the curve.
    for (const auto& b : m_blurRects) {
        const int x0 = static_cast<int>(std::ceil(b.x)), x1 = static_cast<int>(std::floor(b.right()));
        const int y0 = static_cast<int>(std::lround(b.y)), y1 = static_cast<int>(std::lround(b.bottom()));
        if (x1 > x0 && y1 > y0) wl_region_add(region, x0, y0, x1 - x0, y1 - y0);
    }
    ext_background_effect_surface_v1_set_blur_region(m_blur, region);
    wl_region_destroy(region);
}

// ── Input ──

void Surface::pointerMotion(double x, double y) {
    m_pointerX = static_cast<float>(x);
    m_pointerY = static_cast<float>(y);
    ui::Node* hit = m_root ? m_root->hitTest(m_pointerX, m_pointerY) : nullptr;
    if (hit != m_hovered && tooltipHook) {
        // The nearest node with a tooltip, from the one under the pointer up
        ui::Node* tip = nullptr;
        for (ui::Node* n = hit; n && !tip; n = n->parentNode())
            if (!n->tooltip.empty()) tip = n;
        if (tip != m_tooltip) {
            m_tooltip = tip;
            tooltipHook(this, tip);
        }
    }
    if (hit != m_hovered) {
        if (std::getenv("M3_DEBUG_FOCUS")) log::info("hover {} → {}", static_cast<void*>(m_hovered), static_cast<void*>(hit));
        updateHoverWithin(hit);
        if (m_hovered) m_hovered->onHover(false);
        m_hovered = hit;
        if (m_hovered) m_hovered->onHover(true);
    }
    // Everything under the pointer hears where it is (a copy: a handler may
    // change the tree, and destroyed nodes leave the chain)
    const auto chain = m_hoverChain;
    for (ui::Node* n : chain)
        if (std::ranges::find(m_hoverChain, n) != m_hoverChain.end()) n->onPointerMove(m_pointerX, m_pointerY);
    // A pressed node keeps receiving motion (slider drags)
    if (m_pressed) m_pressed->onDrag(m_pointerX, m_pointerY);
}

void Surface::updateHoverWithin(ui::Node* hit) {
    // Ancestors (and self) of the new target, outermost first
    std::vector<ui::Node*> chain;
    for (ui::Node* n = hit; n; n = n->parentNode()) chain.push_back(n);
    for (ui::Node* n : m_hoverChain)
        if (std::ranges::find(chain, n) == chain.end()) n->onHoverWithin(false);
    for (ui::Node* n : chain)
        if (std::ranges::find(m_hoverChain, n) == m_hoverChain.end()) n->onHoverWithin(true);
    m_hoverChain = std::move(chain);
}

void Surface::pointerLeave() {
    if (m_tooltip && tooltipHook) tooltipHook(this, nullptr);
    m_tooltip = nullptr;
    updateHoverWithin(nullptr);
    if (m_hovered) m_hovered->onHover(false);
    if (m_pressed) m_pressed->onRelease();
    m_hovered = m_pressed = nullptr;
}

void Surface::pointerButton(uint32_t button, bool pressed) {
    if (pressed && m_tooltip && tooltipHook) tooltipHook(this, nullptr); // a press puts it away
    // The pointer's back: no ring until the keyboard moves again
    if (pressed && ui::Node::focusRing) {
        ui::Node::focusRing = nullptr;
        requestRedraw();
    }
    if (pressed) {
        m_pressed = m_hovered;
        if (m_pressed) {
            if (m_pressed->focusable()) setFocus(m_pressed);
            m_pressed->onPress(button, m_pointerX, m_pointerY);
        } else if (onEmptyClick) {
            onEmptyClick(m_pointerX, m_pointerY);
        }
    } else {
        // A click is press and release on the same node
        ui::Node* target = m_pressed;
        m_pressed = nullptr;
        if (!target) return;
        target->onRelease();
        if (target == m_hovered) target->onClick(button);
    }
}

void Surface::pointerScroll(int direction) {
    // The deepest node under the pointer that takes scrolling, else its ancestors
    for (ui::Node* n = m_hovered ? m_hovered : (m_root ? m_root->hitTest(m_pointerX, m_pointerY, true) : nullptr); n;
         n = n->parentNode())
        if (n->onScroll(direction)) return;
}

bool Surface::pointerScrollPixels(float dy, bool precise) {
    for (ui::Node* n = m_hovered ? m_hovered : (m_root ? m_root->hitTest(m_pointerX, m_pointerY, true) : nullptr); n;
         n = n->parentNode()) {
        if (n->onScrollPixels(dy, precise)) return true;
        if (n->scrollsDiscretely()) return false;
    }
    return false;
}

void Surface::pointerScrollEnd() {
    for (ui::Node* n = m_hovered ? m_hovered : (m_root ? m_root->hitTest(m_pointerX, m_pointerY, true) : nullptr); n;
         n = n->parentNode())
        n->onScrollEnd();
}

void Surface::setInputRegion(std::optional<std::vector<RectF>> rects) {
    auto same = [](const auto& a, const auto& b) {
        if (a.has_value() != b.has_value()) return false;
        if (!a) return true;
        if (a->size() != b->size()) return false;
        for (size_t i = 0; i < a->size(); ++i) {
            const auto &x = (*a)[i], &y = (*b)[i];
            if (x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h) return false;
        }
        return true;
    };
    if (same(rects, m_input)) return;
    m_input = std::move(rects);
    m_inputDirty = true;
    requestRedraw(); // applied with the next commit
}

void Surface::setFocus(ui::Node* node) {
    if (node == m_focus) return;
    if (m_focus) m_focus->onFocus(false);
    m_focus = node;
    if (m_focus) m_focus->onFocus(true);
}

void Surface::keyboardFocus(bool focused) {
    if (std::getenv("M3_DEBUG_FOCUS")) log::info("keyboard {} {}", m_tag, focused);
    m_hasKeyboard = focused;
    if (m_focus) m_focus->onFocus(focused);
}

void Surface::key(const ui::KeyEvent& event) {
    for (ui::Node* n = m_focus; n; n = n->parentNode())
        if (n->onKey(event)) return;
    if (keyboardNavigation && navigate(event)) return;
    if (onKey) onKey(event);
}

namespace {

// Everything the keyboard can reach, in the tree's order (reading order)
void navigable(ui::Node* n, std::vector<ui::Node*>& out) {
    if (!n->shown() || n->opacity() <= 0.01f) return;
    if (n->keyboardNavigable() && !n->skipKeyboard && n->rect().w > 0 && n->rect().h > 0) out.push_back(n);
    for (const auto& c : n->children()) navigable(c.get(), out);
}

// The nearest in a direction: how far along it, and (counting double) how
// far off to the side. First among those wholly past this one's edge (Right
// from the sidebar goes into the page, not up to the search above it), then,
// if none, any whose centre lies that way
ui::Node* nearest(const std::vector<ui::Node*>& all, const ui::Node* from, uint32_t key) {
    const RectF a = from->rect();
    const float ax = a.x + a.w / 2, ay = a.y + a.h / 2;
    for (const bool strict : {true, false}) {
        ui::Node* best = nullptr;
        float bestScore = 1e9f;
        for (ui::Node* n : all) {
            if (n == from) continue;
            const RectF b = n->rect();
            const float bx = b.x + b.w / 2, by = b.y + b.h / 2;
            float along = 0, side = 0;
            bool past = false, ahead = false;
            switch (key) {
            case XKB_KEY_Down: along = b.y - a.bottom(), side = std::abs(bx - ax), past = b.y >= a.bottom() - 4, ahead = by > ay + 1; break;
            case XKB_KEY_Up: along = a.y - b.bottom(), side = std::abs(bx - ax), past = b.bottom() <= a.y + 4, ahead = by < ay - 1; break;
            case XKB_KEY_Right: along = b.x - a.right(), side = std::abs(by - ay), past = b.x >= a.right() - 4, ahead = bx > ax + 1; break;
            default: along = a.x - b.right(), side = std::abs(by - ay), past = b.right() <= a.x + 4, ahead = bx < ax - 1; break;
            }
            if (strict ? !past : !ahead) continue;
            const float score = std::max(0.0f, along) + side * 2;
            if (score < bestScore) {
                bestScore = score;
                best = n;
            }
        }
        if (best) return best;
    }
    return nullptr;
}

} // namespace

bool Surface::navigate(const ui::KeyEvent& e) {
    const uint32_t k = e.sym;
    if (e.ctrl() || (e.mods & ui::ModAlt)) return false;
    // Enter and Space press what the keyboard is on, as a click would
    if (k == XKB_KEY_Return || k == XKB_KEY_KP_Enter || k == XKB_KEY_space) {
        if (!m_focus || ui::Node::focusRing != m_focus) return false;
        m_focus->onClick(BTN_LEFT);
        return true;
    }
    const bool tab = k == XKB_KEY_Tab || k == XKB_KEY_ISO_Left_Tab;
    const bool arrow = k == XKB_KEY_Up || k == XKB_KEY_Down || k == XKB_KEY_Left || k == XKB_KEY_Right;
    if ((!tab && !arrow) || !m_root) return false;
    std::vector<ui::Node*> all;
    navigable(m_root.get(), all);
    if (all.empty()) return false;
    const auto at = std::ranges::find(all, m_focus);
    ui::Node* next = nullptr;
    if (at == all.end()) {
        next = tab && (e.shift() || k == XKB_KEY_ISO_Left_Tab) ? all.back() : all.front(); // from nowhere: an end
    } else if (tab) {
        const long n = static_cast<long>(all.size()), i = at - all.begin();
        next = all[static_cast<size_t>((i + (e.shift() || k == XKB_KEY_ISO_Left_Tab ? n - 1 : 1)) % n)];
    } else {
        next = nearest(all, m_focus, k);
        if (!next) return true; // the edge: stay
    }
    setFocus(next);
    ui::Node::focusRing = next;
    // Scrolled into view, wherever it sits
    for (ui::Node* p = next->parentNode(); p; p = p->parentNode())
        if (auto* scroll = dynamic_cast<ui::ScrollView*>(p)) scroll->reveal(next->rect());
    requestRedraw();
    return true;
}

} // namespace m3::wl
