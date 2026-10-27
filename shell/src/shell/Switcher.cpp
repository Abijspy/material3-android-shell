#include "shell/Switcher.h"

#include "core/EventLoop.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"
#include "wayland/LayerSurface.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon.h>

namespace m3::shell::switcher {

using namespace m3::ui;

namespace {

struct Win {
    std::string address, cls, title;
    int workspace = 0;
    std::string workspaceName;
};

// Open windows, most recently used first (the compositor's focus history)
std::vector<Win> windows() {
    std::vector<std::pair<int, Win>> found;
    for (const auto& w : Compositor::get().windows(true)) {
        // Special workspaces (the scratchpad) aren't in the round; minimised windows are
        if (w.minimised && w.workspaceName != "special:minimized") continue;
        found.push_back({w.focusOrder, {w.id, w.appId, w.title, w.minimised ? -1 : w.workspace, w.workspaceName}});
    }
    std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Win> out;
    for (auto& [_, w] : found) out.push_back(std::move(w));
    return out;
}

struct State {
    std::unique_ptr<wl::LayerSurface> surface;
    std::vector<Win> wins;
    size_t selected = 0;
    bool superSeen = false; // Super was held while it showed
    bool gotModifiers = false;
    EventLoop::TimerId timeout = 0;
    std::function<void()> rebuild;
};
State& state() {
    static State s;
    return s;
}


void finish(bool commit) {
    auto& s = state();
    if (!s.surface) return;
    if (s.timeout) EventLoop::get().cancel(s.timeout);
    s.timeout = 0;
    wl::Display::get().onModifiers = nullptr;
    Compositor::get().grabKeys(false); // the compositor's binds back, always
    const std::string address = commit && s.selected < s.wins.size() ? s.wins[s.selected].address : "";
    // Off the screen first, then the window comes forward (its workspace with it)
    EventLoop::get().defer([address] {
        state().surface.reset();
        state().wins.clear();
        if (!address.empty()) Compositor::get().focusWindow(address);
    });
}

void move(int by) {
    auto& s = state();
    if (s.wins.empty()) return;
    const long n = static_cast<long>(s.wins.size());
    s.selected = static_cast<size_t>(((static_cast<long>(s.selected) + by) % n + n) % n);
    if (s.rebuild) s.rebuild();
}

// The card: app icons in a row, the chosen one lifted on a tonal tile, its
// title and workspace beneath
class SwitcherRoot : public Node {
public:
    SwitcherRoot() {
        m_card = add<Flex>(Flex::Direction::Column);
        m_card->setPadding({18, 18})->setSpacing(14)->setAlignItems(Align::Center)->setRadius(Radii(32));
        m_card->setBackground(ColorRef("surface_container", 0.92f), false);
        m_card->blurBehind = true;
        state().rebuild = [this] { fill(); };
        fill();
    }
    ~SwitcherRoot() override { state().rebuild = nullptr; }

    bool interactive() const override { return true; }
    void onClick(uint32_t button) override {
        if (button == BTN_LEFT) finish(false); // outside the card: stay put
    }

protected:
    void doArrange() override {
        const auto& b = rect();
        const SizeF s = m_card->measure({b.w * 0.9f, b.h});
        m_card->arrange({b.x + (b.w - s.w) / 2, b.y + (b.h - s.h) / 2, s.w, s.h});
    }

private:
    void fill() {
        m_card->clearChildren();
        auto& s = state();
        auto* row = m_card->add<Flex>(Flex::Direction::Row);
        row->setSpacing(8);
        // Smaller icons when there are many windows
        const float size = std::clamp(900.0f / static_cast<float>(std::max<size_t>(s.wins.size(), 1)) - 24, 36.0f, 64.0f);
        for (size_t i = 0; i < s.wins.size(); ++i) {
            const bool on = i == s.selected;
            auto* tile = row->add<Button>(Flex::Direction::Column);
            tile->setPadding(Insets(10))->setRadius(Radii(22))->setJustify(Align::Center);
            if (on) tile->setBackground("primary_container", false)->setBorder(2, ColorRef("primary"));
            const std::string icon = Apps::get().iconForClass(s.wins[i].cls);
            tile->add<ImageView>(size, [icon, size](float scale, int& w, int& h) -> GLuint {
                const int px = static_cast<int>(std::lround(size * scale));
                auto* t = icon.empty() ? nullptr : Images::get().icon(icon, px);
                if (!t) t = Images::get().icon("application-x-executable", px);
                if (!t) return 0;
                w = t->width;
                h = t->height;
                return t->id;
            });
            tile->onClicked = [i](uint32_t button) {
                if (button != BTN_LEFT) return;
                state().selected = i;
                finish(true);
            };
        }
        if (s.selected < s.wins.size()) {
            const auto& w = s.wins[s.selected];
            m_card->add<Marquee>(560, w.title.empty() ? w.cls : w.title)->label()->setSize(16)->setWeight(600);
            const std::string where = w.workspace < 0 ? "Minimised" : "Workspace " + (w.workspaceName.empty() ? std::to_string(w.workspace) : w.workspaceName);
            m_card->add<Label>(where)->setSize(13)->setColor("on_surface_variant", false);
        }
        requestLayout();
    }

    Flex* m_card;
};

} // namespace

bool isOpen() {
    return state().surface != nullptr;
}

void step(wl::Output* output, int by) {
    auto& s = state();
    if (s.surface) return move(by);
    s.wins = windows();
    if (s.wins.size() < 2) {
        // One window (or none): nothing to switch to
        s.wins.clear();
        return;
    }
    // The next most recent window (or the oldest, going back)
    s.selected = by >= 0 ? 1 : s.wins.size() - 1;
    s.superSeen = false;
    s.gotModifiers = false;

    // Every key to the switcher while it shows: Tab mustn't reach the compositor's binds
    Compositor::get().grabKeys(true);
    wl::LayerSurface::Options o;
    o.ns = "material3ui-switcher";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1;
    o.keyboard = wl::LayerSurface::Keyboard::Exclusive;
    s.surface = std::make_unique<wl::LayerSurface>(output, o);
    auto root = std::make_unique<SwitcherRoot>();
    root->setOpacity(0, false);
    root->setOpacity(1);
    s.surface->setRoot(std::move(root));
    s.surface->onKey = [](const KeyEvent& e) {
        switch (e.sym) {
        case XKB_KEY_Tab: move(e.shift() ? -1 : 1); return true;
        case XKB_KEY_ISO_Left_Tab:
        case XKB_KEY_Left: move(-1); return true;
        case XKB_KEY_Right: move(1); return true;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter: finish(true); return true;
        case XKB_KEY_Escape: finish(false); return true;
        default: return true; // nothing else leaks out meanwhile
        }
    };
    // Letting go of Super picks the window; Super already up when the
    // switcher got the keyboard (a quick tap) picks the previous one at once
    wl::Display::get().onModifiers = [](uint32_t mods) {
        auto& s = state();
        const bool super = mods & ModSuper;
        if (super) s.superSeen = true;
        if (!super && (s.superSeen || !s.gotModifiers)) finish(true);
        s.gotModifiers = true;
    };
    // Never stuck: it gives up after a while without a choice
    s.timeout = EventLoop::get().addTimer(30000, [] {
        state().timeout = 0;
        finish(false);
    });
}

} // namespace m3::shell::switcher
