#include "shell/Dock.h"

#include "core/Config.h"
#include "render/Images.h"
#include "services/Hyprland.h"
#include "services/Search.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

namespace {

constexpr float kMargin = 8, kPad = 8, kIndicator = 6;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string bareId(std::string id) {
    if (id.ends_with(".desktop")) id.resize(id.size() - 8);
    return id;
}

// Does a window of class `cls` belong to this app?
bool owns(const Apps::App& a, const std::string& cls) {
    const std::string c = lower(cls), id = lower(bareId(a.id));
    return (!a.wmClass.empty() && lower(a.wmClass) == c) || id == c || id.ends_with("." + c);
}

const Apps::App* findApp(const std::string& id) {
    for (const auto& a : Apps::get().all())
        if (a.id == id || bareId(a.id) == id) return &a;
    return nullptr;
}

const Apps::App* appForClass(const std::string& cls) {
    for (const auto& a : Apps::get().all())
        if (owns(a, cls)) return &a;
    return nullptr;
}

} // namespace

// Centres the card on the bottom edge and takes the hover for autohide
class DockRoot : public Node {
public:
    std::function<void(bool)> hover;
    std::function<void()> arranged; // the card has its rect: input follows
    bool interactive() const override { return true; } // the reveal strip
    void onHoverWithin(bool h) override {
        if (hover) hover(h);
    }

protected:
    SizeF doMeasure(SizeF available) override {
        for (auto& c : children()) c->measure(available);
        return available;
    }
    void doArrange() override {
        if (children().empty()) return;
        auto& card = children().front();
        const SizeF s = card->measure({rect().w - kMargin * 2, rect().h});
        card->arrange({rect().x + (rect().w - s.w) / 2, rect().bottom() - kMargin - s.h, s.w, s.h});
        if (arranged) arranged();
    }
};

// One app: icon, running indicator, lift on hover
class DockIcon : public Button {
public:
    DockIcon(const std::string& icon, float size, int windows, bool focused) : Button(Direction::Column) {
        setPadding({6, 6, 6, 2})->setSpacing(2)->setRadius(Radii(16));
        pressedRadius = 24;
        const std::string name = icon;
        add<ImageView>(size, [name, size](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().icon(name.empty() ? "application-x-executable" : name, static_cast<int>(std::lround(size * scale)));
            if (!t) t = Images::get().icon("application-x-executable", static_cast<int>(std::lround(size * scale)));
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        });
        // Running: a dot; focused: a wider pill (M3 Expressive indicator)
        auto* dot = add<Flex>(Direction::Row);
        pill(dot->setBackground(focused ? "primary" : "on_surface_variant", false));
        dot->fixedHeight = 4;
        dot->fixedWidth = focused ? 16.0f : windows > 1 ? 10.0f : 4.0f;
        dot->setVisible(windows > 0);
        if (windows == 0) {
            auto* spacer = add<Node>();
            spacer->fixedHeight = 4;
        }
    }
    void onHover(bool h) override {
        Button::onHover(h);
        setTranslateY(h ? -4 : 0);
    }
};

Dock::Dock(wl::Output* output) : m_output(output) {
    LayerSurface::Options o;
    o.ns = "material3ui-dock";
    o.layer = LayerSurface::Layer::Top;
    o.anchor = LayerSurface::Bottom | LayerSurface::Left | LayerSurface::Right;
    o.height = panelHeight();
    o.exclusiveZone = autoHide() ? 0 : panelHeight();
    m_surface = std::make_unique<LayerSurface>(output, o);

    auto& h = Hyprland::get();
    m_events = h.event.connect([this](const std::string& name, const std::string&) {
        if (name == "openwindow" || name == "closewindow" || name == "activewindow" || name == "movewindow" ||
            name == "workspace" || name == "focusedmon") {
            if (m_refreshTimer) return;
            m_refreshTimer = EventLoop::get().addTimer(120, [this] {
                m_refreshTimer = 0;
                refresh();
            });
        }
    });
    // Pinned apps that were just installed (or removed)
    m_apps = Apps::get().changed.connect([this] { refresh(); });
    rebuild();
}

Dock::~Dock() {
    if (m_hideTimer) EventLoop::get().cancel(m_hideTimer);
    if (m_refreshTimer) EventLoop::get().cancel(m_refreshTimer);
}

bool Dock::autoHide() const {
    return Config::get().value<std::string>("/dock/mode", "autohide") == "autohide";
}

int Dock::panelHeight() const {
    const float icon = static_cast<float>(Config::get().value<double>("/dock/iconSize", 44));
    // card padding, icon padding, indicator, margin, and room for the lift
    return static_cast<int>(icon + kPad * 2 + 8 + kIndicator + kMargin + 6);
}

std::vector<std::string> Dock::pinned() {
    const auto j = Config::get().value<nlohmann::json>("/dock/pinned", nlohmann::json());
    std::vector<std::string> out;
    if (j.is_array()) {
        for (const auto& v : j)
            if (v.is_string()) out.push_back(v.get<std::string>());
        // Both settings apps join the dock once (v5); unpinned later, they stay off
        if (!Config::get().value<bool>("/dock/settingsAppsPinned", false)) {
            for (const char* id : {"material3ui-settings", "material3ui-hyprland"})
                if (std::ranges::find(out, id) == out.end()) out.push_back(id);
            Config::get().set("/dock/settingsAppsPinned", true);
            Config::get().set("/dock/pinned", out);
        }
        return out;
    }
    // First run: a browser, the files and a terminal, whichever are installed
    for (const char* id : {"firefox", "librewolf", "chromium", "org.gnome.Nautilus", "nemo", "thunar", "Alacritty",
                           "kitty", "org.wezfurlong.wezterm", "foot"})
        if (findApp(id) && out.size() < 4) out.push_back(bareId(findApp(id)->id));
    // And the two settings apps
    out.push_back("material3ui-settings");
    out.push_back("material3ui-hyprland");
    Config::get().set("/dock/settingsAppsPinned", true);
    return out;
}

void Dock::setPinned(const std::vector<std::string>& ids) {
    Config::get().set("/dock/pinned", ids);
}

void Dock::rebuild() {
    auto root = std::make_unique<DockRoot>();
    m_root = root.get();
    m_root->hover = [this](bool h) {
        m_hovered = h;
        if (h) reveal(true);
        else if (autoHide()) {
            if (m_hideTimer) EventLoop::get().cancel(m_hideTimer);
            m_hideTimer = EventLoop::get().addTimer(450, [this] {
                m_hideTimer = 0;
                if (!m_hovered && !m_held) refresh(); // decides whether to stay out
            });
        }
    };
    m_root->arranged = [this] { updateInput(); };
    m_card = root->add<Flex>(Flex::Direction::Row);
    m_card->setPadding(Insets(kPad))->setSpacing(4)->setRadius(Radii(28));
    m_card->setBackground(ColorRef("surface_container", Theme::get().panelOpacity()), false);
    m_card->blurBehind = true;
    m_surface->setRoot(std::move(root));
    m_surface->setSize(0, panelHeight());
    m_surface->setExclusiveZone(autoHide() ? 0 : panelHeight());
    refresh();
}

void Dock::refresh() {
    if (!m_card) return;
    m_card->clearChildren();
    const float size = static_cast<float>(Config::get().value<double>("/dock/iconSize", 44));
    const auto clients = Hyprland::get().clients(true); // minimised ones too: a click brings them back
    const std::string activeClass = lower(Hyprland::get().activeWindowClass());

    struct Entry {
        std::string id, icon, cls;
        const Apps::App* app = nullptr;
        std::vector<Hyprland::Client> windows;
        bool pinned = false;
    };
    std::vector<Entry> entries;
    for (const auto& id : pinned()) {
        Entry e{id, "", "", findApp(id), {}, true};
        if (e.app) e.icon = e.app->icon;
        else e.icon = id;
        entries.push_back(e);
    }
    std::vector<Entry> running;
    for (const auto& c : clients) {
        if (c.cls.empty()) continue;
        auto it = std::ranges::find_if(entries, [&](const Entry& e) {
            return (e.app && owns(*e.app, c.cls)) || lower(e.cls) == lower(c.cls) || lower(bareId(e.id)) == lower(c.cls);
        });
        if (it != entries.end()) {
            it->windows.push_back(c);
            continue;
        }
        if (!Config::get().value<bool>("/dock/showRunning", true)) continue;
        auto r = std::ranges::find_if(running, [&](const Entry& e) { return lower(e.cls) == lower(c.cls); });
        if (r != running.end()) {
            r->windows.push_back(c);
            continue;
        }
        const auto* app = appForClass(c.cls);
        running.push_back({app ? bareId(app->id) : c.cls, app ? app->icon : Apps::get().iconForClass(c.cls), c.cls, app, {c}, false});
    }

    // Launcher first, then pinned, a divider, then the rest
    auto* apps = m_card->add<Button>(Flex::Direction::Column);
    apps->setPadding({6, 6, 6, 2})->setSpacing(2)->setRadius(Radii(16))->setJustify(Align::Center);
    apps->pressedRadius = 24;
    auto* bubble = apps->add<Flex>(Flex::Direction::Row);
    bubble->setJustify(Align::Center)->setRadius(Radii(size * 0.32f))->setBackground("primary_container", false);
    bubble->fixedWidth = bubble->fixedHeight = size;
    bubble->add<Icon>("apps", size * 0.5f)->setColor("on_primary_container", false);
    auto* gap = apps->add<Node>();
    gap->fixedHeight = 4;
    apps->tooltip = "All apps";
    apps->onClicked = [](uint32_t) {
        if (panels::open) panels::open("launcher");
    };

    auto add = [&](const Entry& e) {
        bool focused = false;
        for (const auto& w : e.windows) focused = focused || lower(w.cls) == activeClass;
        auto* icon = m_card->add<DockIcon>(e.icon, size, static_cast<int>(e.windows.size()), focused);
        // Its name (the app's, else its window class), and how many windows
        std::string name = e.app ? e.app->name : !e.cls.empty() ? e.cls : e.id;
        if (e.windows.size() > 1) name += std::format(" · {} windows", e.windows.size());
        icon->tooltip = name;
        const Entry copy = e;
        icon->onClicked = [copy, focused, icon](uint32_t button) {
            // Right click: the menu (open, pin or unpin, close) above the icon
            if (button == BTN_RIGHT) {
                const std::string cls = copy.windows.empty() ? copy.cls : copy.windows.front().cls;
                if (panels::openAboveDock) panels::openAboveDock("dockmenu", copy.id + "|" + cls, icon->rect().x + icon->rect().w / 2);
                return;
            }
            if (button == BTN_MIDDLE || copy.windows.empty()) {
                // Look it up again: the app list reloads when apps are (un)installed
                if (const auto* app = copy.app ? findApp(copy.id) : nullptr) Apps::get().launch(*app);
                return;
            }
            // Focus its window; when it already has focus, the next one
            size_t next = 0;
            if (focused && copy.windows.size() > 1) {
                for (size_t i = 0; i < copy.windows.size(); ++i)
                    if (lower(copy.windows[i].cls) == lower(Hyprland::get().activeWindowClass()) &&
                        copy.windows[i].title == Hyprland::get().activeWindowTitle())
                        next = (i + 1) % copy.windows.size();
            }
            Hyprland::get().focusWindow(copy.windows[next].address);
        };
    };
    for (const auto& e : entries) add(e);
    if (!running.empty()) {
        auto* div = m_card->add<Flex>(Flex::Direction::Row);
        div->setBackground("outline_variant", false)->setRadius(Radii(1));
        div->fixedWidth = 2;
        div->fixedHeight = size * 0.6f;
        for (const auto& e : running) add(e);
    }

    // Autohide stays out on an empty workspace
    bool empty = true;
    const int ws = Hyprland::get().activeWorkspace(m_output->name);
    for (const auto& w : Hyprland::get().workspaces())
        if (w.id == ws) empty = w.windows == 0;
    reveal(!autoHide() || m_hovered || m_held || empty);
}

void Dock::setHeld(bool held) {
    if (held == m_held) return;
    m_held = held;
    refresh();
}

void Dock::reveal(bool show) {
    if (m_hideTimer && show) {
        EventLoop::get().cancel(m_hideTimer);
        m_hideTimer = 0;
    }
    m_shown = show;
    m_card->setTranslateY(show ? 0 : static_cast<float>(panelHeight()));
    m_card->setOpacity(show ? 1 : 0);
    updateInput();
}

void Dock::updateInput() {
    const float h = static_cast<float>(panelHeight());
    const float w = m_surface->size().w;
    const RectF& c = m_card->rect();
    if (m_shown && c.w > 0) {
        m_surface->setInputRegion(std::vector<RectF>{{c.x - 4, 0, c.w + 8, h}});
    } else {
        // A thin strip on the edge brings it back
        m_surface->setInputRegion(std::vector<RectF>{{0, h - 3, w > 0 ? w : 10000, 3}});
    }
}

} // namespace m3::shell
