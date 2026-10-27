#include "share/SharePicker.h"

#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"
#include "wayland/LayerSurface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::share {

using namespace m3::ui;
using shell::panels::connectedGroup;
using shell::panels::pillButton;

namespace {

// $XDPH_WINDOW_SHARING_LIST: id[HC>]class[HT>]title[HE>]address[HA>]…
std::vector<SharePicker::Window> windowsFromEnv() {
    std::vector<SharePicker::Window> out;
    const char* env = std::getenv("XDPH_WINDOW_SHARING_LIST");
    if (!env) return out;
    std::string list = env;
    size_t at = 0;
    while (at < list.size()) {
        const size_t end = list.find("[HA>]", at);
        const std::string entry = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? list.size() : end + 5;
        const size_t c = entry.find("[HC>]"), t = entry.find("[HT>]"), e = entry.find("[HE>]");
        if (c == std::string::npos || t == std::string::npos) continue;
        out.push_back({entry.substr(0, c), entry.substr(c + 5, t - c - 5),
                       entry.substr(t + 5, e == std::string::npos ? std::string::npos : e - t - 5)});
    }
    return out;
}

// The scrim, and the card centred on it
class PickerRoot : public Node {
public:
    std::function<void()> onDismiss;
    bool interactive() const override { return true; }
    void onClick(uint32_t button) override {
        if (button == BTN_LEFT && onDismiss) onDismiss(); // outside the card
    }

protected:
    SizeF doMeasure(SizeF available) override { return available; }
    void doArrange() override {
        if (children().empty()) return;
        const auto& r = rect();
        auto& card = children().front();
        const SizeF s = card->measure({std::min(r.w - 32, 760.0f), r.h - 32});
        card->arrange({r.x + (r.w - s.w) / 2, r.y + std::max(16.0f, (r.h - s.h) / 2), s.w, std::min(s.h, r.h - 32)});
    }
    void doPaint(Renderer& r) override { r.rect(rect(), Color{0, 0, 0, 0.45f}); }
};

// A selectable tile: a preview or an icon, with a name under it
Button* tile(Node* parent, const std::string& name, const std::string& sub, std::function<void(Button*)> fill) {
    auto* t = parent->add<Button>(Flex::Direction::Column);
    t->setPadding(Insets(10))->setSpacing(6)->setRadius(Radii(22))->setAlignItems(Align::Center);
    t->setBackground("surface_container_high", false);
    t->pressedRadius = 14;
    fill(t);
    t->add<Label>(name)->setSize(14)->setWeight(600)->setMaxWidth(200);
    if (!sub.empty()) t->add<Label>(sub)->setSize(12)->setMaxWidth(200)->setColor("on_surface_variant", false);
    return t;
}

} // namespace

SharePicker::SharePicker(bool allowToken) : m_allowToken(allowToken) {}
SharePicker::~SharePicker() = default;

void SharePicker::finish(const std::string& selection) {
    // XDPH reads the last line; nothing at all means cancelled
    if (!selection.empty()) std::cout << "[SELECTION]" << (m_allowToken ? "r" : "") << "/" << selection << std::endl;
    EventLoop::get().quit(selection.empty() ? 1 : 0);
}

void SharePicker::pickRegion() {
    // Out of the way while the region is drawn, then back to XDPH
    m_surface->setVisible(false);
    run({"slurp", "-f", "%o %x %y %w %h"}, [this](int code, const std::string& out) {
        if (code != 0 || out.empty()) return finish("");
        char output[128] = {};
        int x = 0, y = 0, w = 0, h = 0;
        if (std::sscanf(out.c_str(), "%127s %d %d %d %d", output, &x, &y, &w, &h) != 5) return finish("");
        // Coordinates within that screen
        for (const auto& m : Compositor::get().outputs())
            if (m.name == output) {
                x -= static_cast<int>(m.x);
                y -= static_cast<int>(m.y);
            }
        finish(std::format("region:{}@{},{},{},{}", output, x, y, w, h));
    });
}

bool SharePicker::start() {
    auto& d = wl::Display::get();
    if (!d.connect() || !Renderer::get().init(d.display())) return false;
    m_windows = windowsFromEnv();
    // The screens, with a small preview each
    const auto monitors = Compositor::get().outputs();
    std::vector<std::pair<std::string, std::string>> shots; // name → preview file
    std::error_code ec;
    std::filesystem::create_directories(paths::runtimeDir(), ec);
    for (const auto& m : monitors) {
        const std::string file = paths::runtimeDir() + "/share-" + m.name + ".png";
        // Taken now, before the picker covers the screen
        std::system(std::format("grim -s 0.25 -o '{}' '{}' 2>/dev/null", m.name, file).c_str());
        shots.push_back({m.name, file});
    }

    // On the focused screen
    wl::Output* where = nullptr;
    const std::string focused = Compositor::get().focusedOutput();
    for (auto& o : d.outputs())
        if (!where || o->name == focused) where = o.get();
    wl::LayerSurface::Options o;
    o.ns = "material3ui-share-picker";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1;
    o.keyboard = wl::LayerSurface::Keyboard::Exclusive;
    m_surface = std::make_unique<wl::LayerSurface>(where, o);

    auto root = std::make_unique<PickerRoot>();
    root->onDismiss = [this] { finish(""); };
    auto* card = root->add<Flex>(Flex::Direction::Column);
    card->setPadding(Insets(24))->setSpacing(16)->setAlignItems(Align::Stretch)->setRadius(Radii(32));
    card->setBackground("surface_container", false);

    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(14)->setAlignItems(Align::Center);
    auto* badge = head->add<Flex>(Flex::Direction::Row);
    badge->setJustify(Align::Center)->setRadius(Radii(16))->setBackground("primary_container", false);
    badge->fixedWidth = badge->fixedHeight = 48;
    badge->add<Icon>("screen_share", 26)->setFill(true)->setColor("on_primary_container", false);
    auto* titles = head->add<Flex>(Flex::Direction::Column);
    titles->setSpacing(2)->setAlignItems(Align::Start);
    titles->grow = true;
    titles->add<Label>("Share your screen")->setSize(22)->setWeight(600);
    titles->add<Label>("An app asked to see your screen. Choose what it can see.")->setSize(13)->setColor("on_surface_variant", false);

    // Screens, windows or a region: tabs, and their choices under them
    struct State {
        int tab = 0;
        std::string selection;
        Button* chosen = nullptr;
        Flex* body = nullptr;
        Button* share = nullptr;
        std::function<void()> fill;
    };
    auto st = std::make_shared<State>();
    auto* tabs = card->add<Flex>(Flex::Direction::Row);
    tabs->setJustify(Align::Center);
    st->body = card->add<Flex>(Flex::Direction::Column);
    st->body->setAlignItems(Align::Stretch);

    auto choose = [st](Button* b, const std::string& selection) {
        if (st->chosen) st->chosen->setBackground("surface_container_high")->setBorder(0, "primary");
        st->chosen = b;
        st->selection = selection;
        b->setBackground("secondary_container")->setBorder(3, "primary");
        if (st->share) st->share->setOpacity(1);
    };
    st->fill = [this, st, shots, choose] {
        st->body->clearChildren();
        st->chosen = nullptr;
        st->selection.clear();
        if (st->share) st->share->setOpacity(0.4f);
        if (st->tab == 0) {
            auto* grid = st->body->add<shell::kit::Flow>();
            grid->spacing = 10;
            for (const auto& [name, file] : shots) {
                auto* t = tile(grid, name, "Everything on this screen", [file](Button* b) {
                    auto* img = b->add<ImageView>(208, 117, [file](float scale, int& w, int& h) -> GLuint {
                        auto* t = Images::get().picture(file, static_cast<int>(208 * scale), static_cast<int>(117 * scale), true);
                        if (!t) return 0;
                        w = t->width;
                        h = t->height;
                        return t->id;
                    });
                    img->cover = true;
                });
                t->onClicked = [t, name, choose](uint32_t button) {
                    if (button == BTN_LEFT) choose(t, "screen:" + name);
                };
            }
        } else if (st->tab == 1) {
            if (m_windows.empty()) {
                st->body->add<Label>("No windows to share")->setSize(14)->setColor("on_surface_variant", false);
                return;
            }
            auto* scroll = st->body->add<ScrollView>();
            scroll->maxHeight = 360;
            auto* list = scroll->add<Flex>(Flex::Direction::Column);
            list->setSpacing(4)->setAlignItems(Align::Stretch);
            for (const auto& w : m_windows) {
                auto* row = list->add<Button>(Flex::Direction::Row);
                row->setPadding({14, 10})->setSpacing(14)->setRadius(Radii(18))->setAlignItems(Align::Center);
                row->setBackground("surface_container_high", false);
                row->pressedRadius = 10;
                const std::string icon = Apps::get().iconForClass(w.cls);
                row->add<ImageView>(32, [icon](float scale, int& iw, int& ih) -> GLuint {
                    auto* t = Images::get().icon(icon.empty() ? "application-x-executable" : icon, static_cast<int>(std::lround(32 * scale)));
                    if (!t) t = Images::get().icon("application-x-executable", static_cast<int>(std::lround(32 * scale)));
                    if (!t) return 0;
                    iw = t->width;
                    ih = t->height;
                    return t->id;
                });
                auto* texts = row->add<Flex>(Flex::Direction::Column);
                texts->setSpacing(1)->setAlignItems(Align::Start);
                texts->grow = true;
                texts->add<Label>(w.title.empty() ? w.cls : w.title)->setSize(14)->setWeight(500)->setMaxWidth(520);
                texts->add<Label>(w.cls)->setSize(12)->setColor("on_surface_variant", false);
                const std::string id = w.id;
                row->onClicked = [row, id, choose](uint32_t button) {
                    if (button == BTN_LEFT) choose(row, "window:" + id);
                };
            }
        } else {
            auto* box = st->body->add<Flex>(Flex::Direction::Column);
            box->setPadding(Insets(24))->setSpacing(12)->setAlignItems(Align::Center)->setRadius(Radii(24));
            box->setBackground("surface_container_high", false);
            box->add<Icon>("crop", 40)->setColor("primary", false);
            box->add<Label>("Draw the part of the screen to share")->setSize(15)->setWeight(500);
            box->add<Label>("It's shared as soon as you let go")->setSize(13)->setColor("on_surface_variant", false);
            pillButton(box, "Select a region", true, [this] { EventLoop::get().defer([this] { pickRegion(); }); }, "crop");
        }
        st->body->requestLayout();
    };
    connectedGroup<int>(tabs, 0, {{0, "Screens"}, {1, "Windows"}, {2, "Region"}}, [st](int v) {
        st->tab = v;
        EventLoop::get().defer([st] { st->fill(); });
    });

    // Remember, and the answer
    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(10)->setAlignItems(Align::Center);
    auto* sw = foot->add<Switch>();
    sw->setChecked(m_allowToken, false);
    sw->onToggled = [this](bool on) { m_allowToken = on; };
    auto* remember = foot->add<Label>("Let this app share again without asking");
    remember->setSize(13)->setMaxWidth(320)->setMaxLines(2)->setColor("on_surface_variant", false);
    remember->grow = true;
    pillButton(foot, "Cancel", false, [this] { finish(""); });
    st->share = pillButton(foot, "Share", true, [this, st] {
        if (!st->selection.empty()) finish(st->selection);
    }, "screen_share");
    st->fill();

    m_surface->onKey = [this, st](const KeyEvent& e) {
        if (e.sym == XKB_KEY_Escape) finish("");
        else if ((e.sym == XKB_KEY_Return || e.sym == XKB_KEY_KP_Enter) && !st->selection.empty()) finish(st->selection);
        return true;
    };
    m_surface->setRoot(std::move(root));
    return true;
}

} // namespace m3::share
