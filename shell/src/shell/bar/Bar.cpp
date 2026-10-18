#include "shell/bar/Bar.h"

#include "core/Config.h"
#include "services/Network.h"
#include "shell/Panels.h"
#include "shell/bar/Island.h"
#include "shell/bar/Widgets.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <algorithm>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;
using wl::LayerSurface;

Bar::Bar(wl::Output* output, OpenPopup openPopup) : m_output(output), m_openPopup(std::move(openPopup)) {
    LayerSurface::Options opts;
    opts.ns = "material3ui-bar";
    opts.layer = LayerSurface::Layer::Top;
    opts.anchor = anchor();
    opts.height = height();
    opts.exclusiveZone = height();
    m_surface = std::make_unique<LayerSurface>(output, opts);
    rebuild();
}

uint32_t Bar::anchor() {
    const bool bottom = Config::get().value<std::string>("/statusBar/position", "top") == "bottom";
    return (bottom ? LayerSurface::Bottom : LayerSurface::Top) | LayerSurface::Left | LayerSurface::Right;
}

int Bar::height() const {
    return static_cast<int>(Config::get().value<double>("/statusBar/height", 40));
}

namespace {

const char* kZones[] = {"left", "center", "right"};

// Live download/upload for the bar
class NetSpeed : public Flex {
public:
    NetSpeed() : Flex(Direction::Row) {
        setSpacing(6);
        m_down = add<Label>();
        m_up = add<Label>();
        for (Label* l : {m_down, m_up}) {
            l->setSize(12)->setWeight(500);
            l->minWidth = 64;
        }
        m_lease = Network::get().watchTraffic();
        m_conn = Network::get().detailsChanged.connect([this] { sync(); });
        sync();
    }

private:
    void sync() {
        const auto& d = Network::get().details();
        m_down->setText("↓ " + panels::formatRate(d.rxRate));
        m_up->setText("↑ " + panels::formatRate(d.txRate));
    }
    Label *m_down, *m_up;
    Network::Lease m_lease;
    Connection m_conn;
};

} // namespace

const std::vector<Bar::Module>& Bar::modules() {
    static const std::vector<Module> all = {
        {"launcher", "apps", "Launcher"},       {"workspaces", "view_week", "Workspaces"},
        {"title", "title", "Window title"},     {"island", "radio_button_checked", "Island"},
        {"clock", "schedule", "Clock"},         {"netspeed", "swap_vert", "Network speed"},
        {"tray", "more_horiz", "System tray"},  {"clipboard", "content_paste", "Clipboard"},
        {"screenshot", "screenshot_region", "Screenshot"}, {"status", "wifi", "Status icons"},
        {"battery", "battery_full", "Battery"}, {"power", "power_settings_new", "Power"},
    };
    return all;
}

Bar::Layout Bar::layout() {
    Layout out;
    const auto j = Config::get().value<nlohmann::json>("/statusBar/layout", nlohmann::json());
    if (j.is_object()) {
        for (int z = 0; z < 3; ++z)
            if (auto it = j.find(kZones[z]); it != j.end() && it->is_array())
                for (const auto& id : *it)
                    if (id.is_string()) out[static_cast<size_t>(z)].push_back(id.get<std::string>());
        return out;
    }
    // Default, minus anything switched off with the older per-module toggles
    auto on = [](const char* id) { return Config::get().value<bool>(std::string("/statusBar/modules/") + id, true); };
    for (const char* id : {"workspaces", "title"})
        if (on(id)) out[0].push_back(id);
    out[1] = {"island"};
    for (const char* id : {"tray", "status", "battery"})
        if (on(id) || std::string(id) == "status") out[2].push_back(id);
    return out;
}

void Bar::setLayout(const Layout& layout) {
    nlohmann::json j = nlohmann::json::object();
    for (int z = 0; z < 3; ++z) j[kZones[z]] = layout[static_cast<size_t>(z)];
    Config::get().set("/statusBar/layout", j);
}

void Bar::addModule(Flex* zone, const std::string& id, bool floating) {
    const float alpha = Theme::get().barOpacity();
    if (id == "workspaces") {
        auto* ws = zone->add<Workspaces>(m_output->name);
        if (floating) {
            pill(ws->setBackground(ColorRef("surface_container_high", alpha), false));
            ws->blurBehind = true;
        }
    } else if (id == "title") {
        capsule(zone, floating)->add<WindowTitle>();
    } else if (id == "island") {
        zone->add<Island>(m_openPopup);
    } else if (id == "clock") {
        capsule(zone, floating)->add<Clock>();
    } else if (id == "netspeed") {
        capsule(zone, floating)->add<NetSpeed>();
    } else if (id == "tray") {
        auto* c = capsule(zone, floating);
        c->setPadding({4, 0});
        c->add<TrayIcons>(m_openPopup);
    } else if (id == "status") {
        auto* c = capsule(zone, floating);
        c->setPadding({4, 0});
        c->add<StatusIcons>(m_openPopup);
    } else if (id == "battery") {
        capsule(zone, floating)->add<BatteryStatus>();
    } else {
        // One-tap buttons that open a panel
        const auto it = std::ranges::find_if(modules(), [&](const Module& m) { return id == m.id; });
        if (it == modules().end()) return;
        auto* c = capsule(zone, floating);
        c->setPadding({4, 0});
        auto* b = c->add<Button>();
        pill(b)->setPadding(Insets(5));
        b->pressedRadius = 6;
        b->add<Icon>(it->icon, 20)->setColor("on_surface", false);
        b->onClicked = [this, b, id](uint32_t button) {
            if (button == BTN_LEFT) m_openPopup(id, b->rect().x + b->rect().w / 2);
        };
    }
}

void Bar::rebuild() {
    // backgroundStyle 0 floats each module in its own capsule; anything else
    // draws one bar-wide surface
    const bool floating = Config::get().value<int>("/statusBar/backgroundStyle", 0) == 0;

    auto root = std::make_unique<BarLayout>();
    auto* bg = root.get();
    const auto zones = layout();
    for (size_t z = 0; z < 3; ++z) {
        auto* zone = bg->add<Flex>(Flex::Direction::Row);
        zone->setSpacing(8);
        for (const auto& id : zones[z]) addModule(zone, id, floating);
    }

    auto* surface = m_surface.get();
    if (!floating) {
        // Full background: paint it under everything via a backdrop node
        class Backdrop : public Node {
        protected:
            void doPaint(Renderer& r) override {
                r.rect(rect(), ColorRef("surface_container", Theme::get().barOpacity()).resolve());
            }
        };
        auto wrapper = std::make_unique<Node>();
        wrapper->add<Backdrop>()->blurBehind = true;
        wrapper->adopt(std::move(root));
        surface->setRoot(std::move(wrapper));
    } else {
        surface->setRoot(std::move(root));
    }
    m_surface->setAnchor(anchor());
    m_surface->setSize(0, height());
    m_surface->setExclusiveZone(height());
}

void Bar::setVisible(bool visible) {
    m_surface->setVisible(visible);
}

} // namespace m3::shell
