#include "shell/DesktopEditor.h"

#include "core/EventLoop.h"
#include "core/Process.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/Search.h"
#include "services/Wallpaper.h"
#include "shell/Panels.h"
#include "shell/DesktopItems.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/LayerSurface.h"

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon.h>

namespace m3::shell::desktopeditor {

using namespace m3::ui;

namespace {

constexpr float kGrid = 8; // px the cards snap to

std::unique_ptr<wl::LayerSurface> g_surface;

// One item, live, inside a frame that takes the pointer: drag to move it,
// the corner button removes it
class Card : public Node {
public:
    Card(size_t index, desktop::Board* board) : m_index(index), m_board(board) {}

    Node* content = nullptr;
    Button* remove = nullptr;

    bool interactive() const override { return true; }
    Node* hitTest(float x, float y, bool any) override {
        if (!shown() || !rect().contains(x, y)) return nullptr;
        if (remove && remove->rect().contains(x, y)) return remove->hitTest(x, y, any);
        return this; // the item itself doesn't get clicks while editing
    }
    void onPress(uint32_t button, float x, float y) override {
        if (button != BTN_LEFT) return;
        m_dragging = true;
        m_grabX = x - rect().x;
        m_grabY = y - rect().y;
        requestRedraw();
    }
    void onDrag(float x, float y) override {
        if (!m_dragging) return;
        const auto& area = m_board->rect();
        // Snapped to the grid, kept on the screen
        const float nx = std::round((x - m_grabX - area.x) / kGrid) * kGrid;
        const float ny = std::round((y - m_grabY - area.y) / kGrid) * kGrid;
        m_board->spots[m_index] = {std::clamp(nx / area.w, 0.0f, 1.0f), std::clamp(ny / area.h, 0.0f, 1.0f)};
        m_board->requestLayout();
    }
    void onRelease() override {
        if (!m_dragging) return;
        m_dragging = false;
        requestRedraw();
        auto items = desktop::load();
        if (m_index < items.size()) {
            items[m_index].x = m_board->spots[m_index].first;
            items[m_index].y = m_board->spots[m_index].second;
            desktop::save(items);
        }
    }

protected:
    SizeF doMeasure(SizeF available) override {
        const SizeF s = content ? content->measure(available) : SizeF{};
        return {s.w + 16, s.h + 16};
    }
    void doArrange() override {
        const auto& b = rect();
        if (content) content->arrange({b.x + 8, b.y + 8, b.w - 16, b.h - 16});
        if (remove) remove->arrange({b.right() - 30, b.y - 6, 36, 36});
    }
    void doPaint(Renderer& r) override {
        const auto& t = Theme::get();
        r.rect(rect(), t.color("surface").withAlpha(m_dragging ? 0.35f : 0.18f), Radii(20), 2,
               t.color(m_dragging ? "primary" : "outline").withAlpha(0.9f));
    }

private:
    size_t m_index;
    desktop::Board* m_board;
    bool m_dragging = false;
    float m_grabX = 0, m_grabY = 0;
};

// The screen: a scrim, the cards on a board, the toolbar and its picker
class EditorRoot : public Node {
public:
    EditorRoot() {
        m_board = add<desktop::Board>();
        m_picker = add<Flex>(Flex::Direction::Column);
        m_picker->setPadding(Insets(12))->setSpacing(4)->setAlignItems(Align::Stretch)->setRadius(Radii(28));
        m_picker->setBackground("surface_container", false);
        m_picker->setVisible(false);
        m_bar = add<Flex>(Flex::Direction::Row);
        pill(m_bar->setBackground("surface_container_high", false))->setPadding({10, 8})->setSpacing(6);
        auto tool = [this](const char* icon, const char* label, std::function<void()> fn) {
            auto* b = m_bar->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({14, 10})->setSpacing(8);
            b->add<Icon>(icon, 20)->setColor("on_surface", false);
            b->add<Label>(label)->setSize(14)->setWeight(500);
            b->onClicked = [fn = std::move(fn)](uint32_t button) {
                if (button == BTN_LEFT) fn();
            };
        };
        tool("widgets", "Widgets", [this] { pick(Picker::Widgets); });
        tool("apps", "Apps", [this] { pick(Picker::Apps); });
        tool("folder", "Files", [this] { pick(Picker::Files); });
        auto* done = m_bar->add<Button>(Flex::Direction::Row);
        pill(done->setBackground("primary", false))->setPadding({18, 10})->setSpacing(8);
        done->add<Icon>("check", 20)->setColor("on_primary", false);
        done->add<Label>("Done")->setSize(14)->setWeight(600)->setColor("on_primary", false);
        done->onClicked = [](uint32_t button) {
            if (button == BTN_LEFT) EventLoop::get().defer([] { close(); });
        };
        fill();
    }

    // The cards for the saved items
    void fill() {
        m_board->clearChildren();
        m_board->spots.clear();
        const auto items = desktop::load();
        for (size_t i = 0; i < items.size(); ++i) {
            auto* card = m_board->add<Card>(i, m_board);
            card->content = desktop::build(card, items[i], true);
            auto* x = card->add<Button>();
            pill(x->setBackground("error", false))->setJustify(Align::Center);
            x->fixedWidth = x->fixedHeight = 36;
            x->add<Icon>("close", 20)->setColor("on_error", false);
            x->tooltip = "Remove " + desktop::nameOf(items[i]);
            x->onClicked = [this, i](uint32_t button) {
                if (button != BTN_LEFT) return;
                auto list = desktop::load();
                if (i < list.size()) list.erase(list.begin() + static_cast<long>(i));
                desktop::save(list);
                EventLoop::get().defer([this] { fill(); });
            };
            card->remove = x;
            m_board->spots.push_back({items[i].x, items[i].y});
        }
        requestLayout();
    }

protected:
    void doArrange() override {
        const auto& b = rect();
        // Moved up and left by a card's frame, so each item sits exactly
        // where it does on the desktop
        m_board->arrange({b.x - 8, b.y - 8, b.w, b.h});
        const SizeF bar = m_bar->measure({b.w, 80});
        const float barY = b.bottom() - bar.h - 36;
        m_bar->arrange({b.x + (b.w - bar.w) / 2, barY, bar.w, bar.h});
        if (m_picker->visible()) {
            const SizeF p = m_picker->measure({420, b.h * 0.6f});
            const float w = std::max(p.w, 360.0f);
            m_picker->arrange({b.x + (b.w - w) / 2, barY - p.h - 12, w, p.h});
        }
    }
    void doPaint(Renderer& r) override {
        // The desktop shows through, dimmed: the cards are what's being edited
        r.rect(rect(), Theme::get().color("scrim").withAlpha(0.45f));
    }

private:
    enum class Picker { None, Widgets, Apps, Files };

    void addItem(const std::string& kind, const std::string& id) {
        auto list = desktop::load();
        // Near the middle, a little apart from the last one added
        const float shift = static_cast<float>(list.size() % 6) * 0.02f;
        list.push_back({kind, id, 0.4f + shift, 0.35f + shift});
        desktop::save(list);
        m_open = Picker::None;
        m_picker->setVisible(false);
        EventLoop::get().defer([this] { fill(); });
    }

    void pick(Picker what) {
        m_open = m_open == what ? Picker::None : what;
        m_picker->clearChildren();
        m_picker->setVisible(m_open != Picker::None);
        if (m_open == Picker::None) return requestLayout();
        auto item = [this](Node* list, const std::string& icon, const std::string& label, const std::string& sub,
                           std::function<void()> fn, bool themeIcon) {
            auto* b = list->add<Button>(Flex::Direction::Row);
            b->setPadding({12, 8})->setSpacing(12)->setRadius(Radii(16));
            if (themeIcon) {
                b->add<ImageView>(28, [icon](float scale, int& w, int& h) -> GLuint {
                    auto* t = Images::get().icon(icon, static_cast<int>(std::lround(28 * scale)));
                    if (!t) return 0;
                    w = t->width;
                    h = t->height;
                    return t->id;
                });
            } else {
                b->add<Icon>(icon, 22)->setColor("primary", false);
            }
            auto* texts = b->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start);
            texts->grow = true;
            texts->add<Label>(label)->setSize(14)->setWeight(500);
            if (!sub.empty()) texts->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false);
            b->onClicked = [fn = std::move(fn)](uint32_t button) {
                if (button == BTN_LEFT) fn();
            };
        };
        auto* title = m_picker->add<Label>(m_open == Picker::Widgets ? "Add a widget" : m_open == Picker::Apps ? "Add an app" : "Add a file or folder");
        title->setSize(14)->setWeight(600)->setColor("primary", false);
        auto* scroll = m_picker->add<ScrollView>();
        scroll->maxHeight = 320;
        auto* list = scroll->add<Flex>(Flex::Direction::Column);
        list->setAlignItems(Align::Stretch)->setSpacing(2);
        if (m_open == Picker::Widgets) {
            for (const auto& w : desktop::widgets()) {
                const std::string id = w.id;
                item(list, w.icon, w.name, w.about, [this, id] { addItem("widget", id); }, false);
            }
        } else if (m_open == Picker::Apps) {
            std::vector<const Apps::App*> apps;
            for (const auto& a : Apps::get().all()) apps.push_back(&a);
            std::ranges::sort(apps, [](const auto* a, const auto* b) { return a->name < b->name; });
            for (const auto* a : apps) {
                const std::string id = a->id;
                item(list, a->icon, a->name, a->generic, [this, id] { addItem("app", id); }, true);
            }
        } else {
            // The file chooser (zenity), as the shell's other pickers
            for (const auto& [label, flag, icon] : {std::tuple{"A file", "", "description"}, {"A folder", " --directory", "folder"}}) {
                const std::string f = flag;
                item(list, icon, label, "Choose it", [this, f] {
                    run({"sh", "-c", "zenity --file-selection" + f + " 2>/dev/null"}, [this](int code, const std::string& out) {
                        std::string path = out;
                        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
                        if (code == 0 && !path.empty() && g_surface) addItem("file", path);
                    });
                }, false);
            }
        }
        requestLayout();
    }

    desktop::Board* m_board;
    Flex* m_bar;
    Flex* m_picker;
    Picker m_open = Picker::None;
};


std::unique_ptr<wl::LayerSurface> g_menu;

// The right-click menu: a card at the pointer; a click anywhere else, or
// Escape, puts it away
class MenuRoot : public Node {
public:
    MenuRoot(wl::Output* output, float x, float y) : m_x(x), m_y(y) {
        m_card = add<Flex>(Flex::Direction::Column);
        m_card->setPadding(Insets(6))->setSpacing(1)->setAlignItems(Align::Stretch)->setRadius(Radii(18));
        m_card->setBackground("surface_container", false);
        m_card->minWidth = 250;
        auto entry = [this](const char* icon, const char* label, std::function<void()> fn) {
            auto* b = m_card->add<Button>(Flex::Direction::Row);
            b->setPadding({12, 9})->setSpacing(14)->setRadius(Radii(12));
            b->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
            b->add<Label>(label)->setSize(14);
            b->onClicked = [fn = std::move(fn)](uint32_t button) {
                if (button != BTN_LEFT) return;
                // The menu goes first, then what was chosen happens
                EventLoop::get().defer([fn] {
                    g_menu.reset();
                    fn();
                });
            };
        };
        auto line = [this] {
            auto* l = m_card->add<Node>();
            l->fixedHeight = 1;
            l->minHeight = 1;
        };
        entry("dashboard_customize", "Edit widgets and shortcuts", [output] { open(output); });
        entry("wallpaper", "Change wallpaper", [] {
            if (panels::open) panels::open("wallpapers");
        });
        entry("skip_next", "Next wallpaper", [] { Wallpaper::get().next(1); });
        line();
        entry("terminal", "Open a terminal", [] { spawn("${TERMINAL:-kitty}"); });
        line();
        entry("desktop_windows", "Display settings", [] {
            if (panels::open) panels::open("hyprsettings hypr-monitors");
        });
        entry("palette", "Personalise", [] {
            if (panels::open) panels::open("settings appearance");
        });
        entry("settings", "Material3UI Settings", [] {
            if (panels::open) panels::open("settings");
        });
        entry("tune", "Hyprland Settings", [] {
            if (panels::open) panels::open("hyprsettings");
        });
    }

    bool interactive() const override { return true; }
    void onClick(uint32_t) override {
        EventLoop::get().defer([] { g_menu.reset(); });
    }

protected:
    void doArrange() override {
        const auto& b = rect();
        const SizeF s = m_card->measure({b.w, b.h});
        // At the pointer, turned back from the screen's edges
        const float x = m_x + s.w > b.right() ? m_x - s.w : m_x;
        const float y = m_y + s.h > b.bottom() ? m_y - s.h : m_y;
        m_card->arrange({std::max(b.x, x), std::max(b.y, y), s.w, s.h});
    }

private:
    float m_x, m_y;
    Flex* m_card;
};

} // namespace

void menu(wl::Output* output, float x, float y) {
    wl::LayerSurface::Options o;
    o.ns = "material3ui-desktop-menu";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1;
    o.keyboard = wl::LayerSurface::Keyboard::OnDemand;
    g_menu = std::make_unique<wl::LayerSurface>(output, o);
    auto root = std::make_unique<MenuRoot>(output, x, y);
    root->setOpacity(0, false);
    root->setOpacity(1);
    g_menu->setRoot(std::move(root));
    g_menu->onKey = [](const KeyEvent& e) {
        if (e.sym != XKB_KEY_Escape) return false;
        EventLoop::get().defer([] { g_menu.reset(); });
        return true;
    };
}

Signal<>& changed() {
    static Signal<> s;
    return s;
}

void open(wl::Output* output) {
    if (g_surface) return;
    g_menu.reset();
    wl::LayerSurface::Options o;
    o.ns = "material3ui-desktop-editor";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Bottom | wl::LayerSurface::Left | wl::LayerSurface::Right;
    o.exclusiveZone = -1;
    o.keyboard = wl::LayerSurface::Keyboard::OnDemand;
    g_surface = std::make_unique<wl::LayerSurface>(output, o);
    g_surface->setRoot(std::make_unique<EditorRoot>());
    g_surface->onKey = [](const KeyEvent& e) {
        if (e.sym != XKB_KEY_Escape) return false;
        EventLoop::get().defer([] { close(); });
        return true;
    };
    changed().emit();
}

void close() {
    if (!g_surface) return;
    g_surface.reset();
    changed().emit();
}

bool isOpen() {
    return g_surface != nullptr;
}

} // namespace m3::shell::desktopeditor
