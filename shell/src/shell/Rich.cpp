// Phase 6 panels: dashboard, system monitor, wallpaper picker, overview
#include "render/Images.h"
#include "services/Hyprland.h"
#include "services/Mpris.h"
#include "services/Search.h"
#include "services/SystemStats.h"
#include "services/Weather.h"
#include "services/Wallpaper.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// ── Small widgets ──

// Keeps SystemStats sampling while the panel is open
struct StatsLease : Node {
    SystemStats::Lease lease = SystemStats::get().lease();
};

// A history graph: rounded bars, newest on the right
class Graph : public Node {
public:
    std::deque<double> values;
    double max = 100;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 72}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Theme::get().color("surface_container_highest"), Radii(16));
        const int n = 60;
        const float gap = 2, bw = (b.w - 16 - gap * (n - 1)) / n;
        const Color c = Theme::get().color("primary");
        for (size_t i = 0; i < values.size(); ++i) {
            const float h = static_cast<float>(std::clamp(values[i] / max, 0.0, 1.0)) * (b.h - 16);
            const float x = b.x + 8 + static_cast<float>(n - static_cast<int>(values.size()) + static_cast<int>(i)) * (bw + gap);
            if (h > 0.5f) r.rect({x, b.bottom() - 8 - h, bw, h}, c.withAlpha(0.35f + 0.65f * static_cast<float>(i) / n), Radii(std::min(bw, h) / 2));
        }
    }
};

// Usage bar with an eased value
class Meter : public Node {
public:
    Tween value{0, motion::mediumMs, Easing::standard()};
    float height = 10;

protected:
    SizeF doMeasure(SizeF available) override { return {std::max(60.0f, available.w), height}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Theme::get().color("secondary_container"), Radii(height / 2));
        const float v = std::clamp(value.value(), 0.0f, 1.0f);
        if (v > 0.002f) r.rect({b.x, b.y, std::max(height, b.w * v), b.h}, Theme::get().color(v > 0.9f ? "error" : "primary"), Radii(height / 2));
        if (value.running()) requestRedraw();
    }
    bool doAnimating() const override { return value.running(); }
};

struct MeterRow {
    Label* value;
    Meter* meter;
};

MeterRow meterRow(Node* parent, const std::string& icon, const std::string& name) {
    auto* col = parent->add<Flex>(Flex::Direction::Column);
    col->setAlignItems(Align::Stretch)->setSpacing(6);
    auto* top = col->add<Flex>(Flex::Direction::Row);
    top->setSpacing(8);
    top->add<Icon>(icon, 18)->setColor("primary", false);
    auto* l = top->add<Label>(name);
    l->setSize(14)->setWeight(500);
    l->grow = true;
    auto* v = top->add<Label>();
    v->setSize(13)->setColor("on_surface_variant", false);
    auto* m = col->add<Meter>();
    return {v, m};
}

Flex* card(Node* parent) {
    auto* c = parent->add<Flex>(Flex::Direction::Column);
    c->setPadding(Insets(16))->setSpacing(14)->setAlignItems(Align::Stretch)->setRadius(Radii(24));
    c->setBackground("surface_container_high", false);
    return c;
}

std::string duration(double seconds) {
    const long s = static_cast<long>(seconds);
    if (s >= 86400) return std::format("{}d {}h", s / 86400, (s % 86400) / 3600);
    return std::format("{}h {}m", s / 3600, (s % 3600) / 60);
}

// Month calendar with navigation; today as a filled pill
class Calendar : public Flex {
public:
    Calendar() : Flex(Direction::Column) {
        setAlignItems(Align::Stretch)->setSpacing(6);
        const std::time_t now = std::time(nullptr);
        localtime_r(&now, &m_today);
        m_year = m_today.tm_year + 1900;
        m_month = m_today.tm_mon;
        build();
    }

private:
    void build() {
        clearChildren();
        auto* head = add<Flex>(Direction::Row);
        head->setSpacing(4);
        std::tm first{};
        first.tm_year = m_year - 1900;
        first.tm_mon = m_month;
        first.tm_mday = 1;
        std::mktime(&first);
        char title[64];
        std::strftime(title, sizeof title, "%B %Y", &first);
        auto* t = head->add<Label>(title);
        t->setSize(16)->setWeight(500);
        t->grow = true;
        iconButton(head, "chevron_left", [this] { step(-1); });
        iconButton(head, "chevron_right", [this] { step(1); });

        // Monday-first week rows
        auto* names = add<Flex>(Direction::Row);
        for (const char* d : {"M", "T", "W", "T", "F", "S", "S"}) {
            auto* l = names->add<Label>(d);
            l->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
            l->fixedWidth = 40;
        }
        const int offset = (first.tm_wday + 6) % 7;
        std::tm last = first;
        last.tm_mon += 1;
        last.tm_mday = 0;
        std::mktime(&last);
        const int days = last.tm_mday;
        Flex* row = nullptr;
        for (int cell = 0; cell < offset + days; ++cell) {
            if (cell % 7 == 0) row = add<Flex>(Direction::Row);
            auto* box = row->add<Flex>(Direction::Row);
            box->fixedWidth = 40;
            box->fixedHeight = 36;
            box->setJustify(Align::Center);
            if (cell < offset) continue;
            const int day = cell - offset + 1;
            const bool today = day == m_today.tm_mday && m_month == m_today.tm_mon && m_year == m_today.tm_year + 1900;
            if (today) pill(box->setBackground("primary", false));
            box->add<Label>(std::to_string(day))->setSize(14)->setWeight(today ? 600 : 400)->setColor(today ? "on_primary" : "on_surface", false);
        }
    }

    void step(int d) {
        m_month += d;
        if (m_month < 0) m_month = 11, --m_year;
        if (m_month > 11) m_month = 0, ++m_year;
        // Rebuilding destroys the arrow being clicked: wait a turn
        EventLoop::get().defer([this, alive = m_alive] {
            if (*alive) build();
        });
    }

public:
    ~Calendar() override { *m_alive = false; }

private:
    std::tm m_today{};
    int m_year = 0, m_month = 0;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

ImageView::Provider artProvider(const std::string& url) {
    if (!url.starts_with("file://")) return {};
    const std::string path = url.substr(7);
    return [path](float scale, int& w, int& h) -> GLuint {
        const int px = static_cast<int>(std::lround(96 * scale));
        auto* t = Images::get().picture(path, px, px, true);
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    };
}

} // namespace

// ── Dashboard ──

void dashboard(Flex* card0, Popup&) {
    card0->add<StatsLease>();
    auto* columns = card0->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(16)->setAlignItems(Align::Start);

    auto* left = columns->add<Flex>(Flex::Direction::Column);
    left->setAlignItems(Align::Stretch)->setSpacing(12);
    auto* clock = left->add<Label>();
    clock->setSize(56)->setWeight(500)->setColor("primary", false);
    auto* date = left->add<Label>();
    date->setSize(16)->setColor("on_surface_variant", false);
    card(left)->add<Calendar>();

    auto* right = columns->add<Live>();
    right->fixedWidth = 340;

    // Weather: now and the next days; opens its Settings page
    auto* wx = right->add<Button>(Flex::Direction::Column);
    wx->setPadding({18, 14})->setSpacing(10)->setAlignItems(Align::Stretch)->setRadius(Radii(24));
    wx->setBackground("primary_container", false);
    wx->pressedRadius = 16;
    wx->onClicked = [](uint32_t) {
        if (open) open("settings weather");
    };
    auto* wxTop = wx->add<Flex>(Flex::Direction::Row);
    wxTop->setSpacing(12);
    auto* wxIcon = wxTop->add<Icon>("cloud", 40);
    wxIcon->setFill(true)->setColor("on_primary_container", false);
    auto* wxTexts = wxTop->add<Flex>(Flex::Direction::Column);
    wxTexts->setAlignItems(Align::Start);
    wxTexts->grow = true;
    auto* wxTemp = wxTexts->add<Label>();
    wxTemp->setSize(30)->setWeight(600)->setColor("on_primary_container", false);
    auto* wxSky = wxTexts->add<Label>();
    wxSky->setSize(13)->setMaxWidth(220)->setColor("on_primary_container", false);
    auto* wxDays = wx->add<Flex>(Flex::Direction::Row);
    wxDays->setSpacing(4)->setAlignItems(Align::Stretch);

    // Now playing
    auto* media = card(right);
    auto* mediaRow = media->add<Flex>(Flex::Direction::Row);
    mediaRow->setSpacing(14);
    auto* art = mediaRow->add<ImageView>(72, ImageView::Provider{});
    art->cover = true;
    auto* texts = mediaRow->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(2);
    texts->grow = true;
    auto* title = texts->add<Label>();
    title->setSize(16)->setWeight(600)->setMaxWidth(200)->setMaxLines(2);
    auto* artist = texts->add<Label>();
    artist->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(200);
    auto* controls = media->add<Flex>(Flex::Direction::Row);
    controls->setSpacing(8)->setJustify(Align::Center);
    iconButton(controls, "skip_previous", [] { Mpris::get().previous(); }, 26);
    auto* play = iconButton(controls, "play_arrow", [] { Mpris::get().playPause(); }, 30);
    iconButton(controls, "skip_next", [] { Mpris::get().next(); }, 26);

    // System at a glance
    auto* sys = card(right);
    auto cpu = meterRow(sys, "memory", "CPU");
    auto mem = meterRow(sys, "memory_alt", "Memory");
    auto disk = meterRow(sys, "hard_drive", "Disk /");
    auto* up = sys->add<Label>();
    up->setSize(13)->setColor("on_surface_variant", false);
    up->alignSelf = Align::Start;

    auto lastArt = std::make_shared<std::string>("\x01");
    right->sync = [=] {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char buf[64];
        std::strftime(buf, sizeof buf, "%H:%M", &tm);
        clock->setText(buf);
        std::strftime(buf, sizeof buf, "%A, %-d %B %Y", &tm);
        date->setText(buf);

        const auto* p = Mpris::get().active();
        title->setText(p ? p->title : "Nothing playing");
        artist->setText(p ? (p->artist.empty() ? p->identity : p->artist) : "Start music in any player");
        static_cast<Icon*>(play->children()[0].get())->setText(p && p->status == "Playing" ? "pause" : "play_arrow");
        const std::string url = p ? p->artUrl : "";
        if (url != *lastArt) {
            *lastArt = url;
            auto provider = artProvider(url);
            art->setVisible(static_cast<bool>(provider));
            art->setProvider(std::move(provider));
        }

        auto& s = SystemStats::get();
        cpu.value->setText(std::format("{:.0f}%{}", s.cpu(), s.temperature() > 0 ? std::format(" · {:.0f}°C", s.temperature()) : ""));
        cpu.meter->value.set(static_cast<float>(s.cpu() / 100));
        mem.value->setText(std::format("{} of {}", formatBytes(s.memUsed()), formatBytes(s.memTotal())));
        mem.meter->value.set(s.memTotal() ? static_cast<float>(s.memUsed()) / static_cast<float>(s.memTotal()) : 0);
        const auto& disks = s.disks();
        auto root = std::ranges::find(disks, std::string("/"), &SystemStats::Disk::mount);
        disk.meter->parentNode()->setVisible(root != disks.end());
        if (root != disks.end()) {
            disk.value->setText(std::format("{} free", formatBytes(root->total - root->used)));
            disk.meter->value.set(static_cast<float>(root->used) / static_cast<float>(root->total));
        }
        up->setText("Up " + duration(s.uptime()));

        auto& w = Weather::get();
        wx->setVisible(w.enabled());
        wxIcon->setText(w.ready() ? w.icon() : "cloud_off");
        wxTemp->setText(w.ready() ? w.temp(w.temperature()) : "–");
        wxSky->setText(w.ready() ? Weather::describe(w.code()) + "  ·  " + w.place().substr(0, w.place().find(','))
                                 : (w.error().empty() ? "Getting the weather…" : w.error()));
        wxDays->clearChildren();
        for (size_t i = 1; i < w.days().size() && i < 6; ++i) {
            const auto& d = w.days()[i];
            std::tm dt{};
            strptime(d.date.c_str(), "%Y-%m-%d", &dt);
            char day[8];
            std::strftime(day, sizeof day, "%a", &dt);
            auto* col = wxDays->add<Flex>(Flex::Direction::Column);
            col->setSpacing(2)->setPadding({2, 6})->setRadius(Radii(14))->setBackground(ColorRef("surface", 0.3f), false);
            col->grow = true;
            col->add<Label>(day)->setSize(12)->setWeight(600)->setColor("on_primary_container", false);
            col->add<Icon>(Weather::icon(d.code, true), 18)->setColor("on_primary_container", false);
            col->add<Label>(w.temp(d.max))->setSize(12)->setColor("on_primary_container", false);
        }
    };
    right->watch(Weather::get().changed);
    right->watch(Mpris::get().changed);
    right->watch(SystemStats::get().changed);
    right->sync();
}

// ── System monitor ──

void systemMonitor(Flex* card0, Popup&) {
    card0->add<StatsLease>();
    auto* live = card0->add<Live>();
    header(live, "monitoring", "System monitor");

    auto* cpuCard = card(live);
    auto* cpuHead = cpuCard->add<Flex>(Flex::Direction::Row);
    cpuHead->setSpacing(8);
    cpuHead->add<Icon>("memory", 20)->setColor("primary", false);
    auto* cpuTitle = cpuHead->add<Label>("CPU");
    cpuTitle->setSize(15)->setWeight(500);
    cpuTitle->grow = true;
    auto* cpuValue = cpuHead->add<Label>();
    cpuValue->setSize(14)->setColor("on_surface_variant", false);
    auto* graph = cpuCard->add<Graph>();

    auto* memCard = card(live);
    auto mem = meterRow(memCard, "memory_alt", "Memory");
    auto swap = meterRow(memCard, "swap_horiz", "Swap");

    auto* diskCard = card(live);
    auto* diskList = diskCard->add<Flex>(Flex::Direction::Column);
    diskList->setAlignItems(Align::Stretch)->setSpacing(12);
    auto diskRows = std::make_shared<std::vector<MeterRow>>();

    auto* procCard = card(live);
    procCard->setSpacing(4);
    auto* procHead = procCard->add<Flex>(Flex::Direction::Row);
    for (auto [text, width] : {std::pair{"Process", 0.0f}, {"PID", 70.0f}, {"CPU", 70.0f}, {"Memory", 90.0f}}) {
        auto* l = procHead->add<Label>(text);
        l->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
        if (width > 0) l->fixedWidth = width; else l->grow = true;
    }
    auto* procList = procCard->add<Flex>(Flex::Direction::Column);
    procList->setAlignItems(Align::Stretch)->setSpacing(2);

    live->sync = [=] {
        auto& s = SystemStats::get();
        cpuValue->setText(std::format("{:.0f}% · {} cores{}", s.cpu(), s.cores(),
                                      s.temperature() > 0 ? std::format(" · {:.0f}°C", s.temperature()) : ""));
        graph->values = s.cpuHistory();
        graph->requestRedraw();
        mem.value->setText(std::format("{} of {}", formatBytes(s.memUsed()), formatBytes(s.memTotal())));
        mem.meter->value.set(s.memTotal() ? static_cast<float>(s.memUsed()) / static_cast<float>(s.memTotal()) : 0);
        swap.value->setText(s.swapTotal() ? std::format("{} of {}", formatBytes(s.swapUsed()), formatBytes(s.swapTotal())) : "None");
        swap.meter->value.set(s.swapTotal() ? static_cast<float>(s.swapUsed()) / static_cast<float>(s.swapTotal()) : 0);

        if (diskRows->size() != s.disks().size()) {
            diskList->clearChildren();
            diskRows->clear();
            for (const auto& d : s.disks()) diskRows->push_back(meterRow(diskList, "hard_drive", d.mount));
        }
        for (size_t i = 0; i < diskRows->size(); ++i) {
            const auto& d = s.disks()[i];
            (*diskRows)[i].value->setText(std::format("{} of {}", formatBytes(d.used), formatBytes(d.total)));
            (*diskRows)[i].meter->value.set(static_cast<float>(d.used) / static_cast<float>(d.total));
        }

        procList->clearChildren();
        for (const auto& p : s.processes()) {
            auto* row = procList->add<Flex>(Flex::Direction::Row);
            auto* name = row->add<Label>(p.name);
            name->setSize(13)->setMaxWidth(260);
            name->grow = true;
            for (auto [text, width] : {std::pair{std::to_string(p.pid), 70.0f}, {std::format("{:.1f}%", p.cpu), 70.0f},
                                       {formatBytes(p.rss), 90.0f}}) {
                auto* l = row->add<Label>(text);
                l->setSize(13)->setColor("on_surface_variant", false);
                l->fixedWidth = width;
            }
        }
    };
    live->watch(SystemStats::get().changed);
    live->sync();
}

// ── Wallpaper picker ──

void wallpapers(Flex* card0, Popup& popup) {
    auto* head = header(card0, "wallpaper", "Wallpapers");
    iconButton(head, "shuffle", [] {
        const auto all = Wallpaper::get().list();
        if (!all.empty()) Wallpaper::get().set(all[static_cast<size_t>(std::rand()) % all.size()]);
    });
    caption(card0, Wallpaper::get().directory());

    const auto files = Wallpaper::get().list();
    if (files.empty()) {
        caption(card0, "No images found. Put some in this folder, or set background.autoCycleDirectory.");
        return;
    }
    auto* scroll = card0->add<ScrollView>();
    scroll->maxHeight = 520;
    auto* grid = scroll->add<Flex>(Flex::Direction::Column);
    grid->setSpacing(12)->setAlignItems(Align::Start);
    const std::string current = Wallpaper::get().path();
    Flex* row = nullptr;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i % 3 == 0) {
            row = grid->add<Flex>(Flex::Direction::Row);
            row->setSpacing(12);
        }
        const std::string path = files[i];
        auto* tile = row->add<Button>();
        tile->setPadding(Insets(4))->setRadius(Radii(18));
        if (path == current) tile->setBorder(3, "primary");
        auto* img = tile->add<ImageView>(252, 142, [path](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().picture(path, static_cast<int>(252 * scale), static_cast<int>(142 * scale), true);
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        });
        img->cover = true;
        tile->onClicked = [path, &popup](uint32_t button) {
            if (button != BTN_LEFT) return;
            Wallpaper::get().set(path);
            popup.close();
        };
    }
}

// ── Overview ──

void overview(Flex* card0, Popup& popup) {
    card0->setBackground(ColorRef("surface_container", 0.92f), false);
    auto& h = Hyprland::get();
    if (!h.available()) {
        caption(card0, "The overview needs Hyprland");
        return;
    }
    const auto clients = h.clients();
    const auto monitors = h.monitorGeometry();
    const auto focused = std::ranges::find_if(h.monitors(), &Hyprland::Monitor::focused);
    const std::string monitorName = focused != h.monitors().end() ? focused->name : "";
    const int active = h.activeWorkspace(monitorName);
    Hyprland::MonitorGeometry mon{"", 0, 0, 1920, 1080, 1};
    for (const auto& m : monitors)
        if (m.name == monitorName) mon = m;

    int count = 5;
    for (const auto& w : h.workspaces()) count = std::max(count, w.id);
    count = std::min(count, 20);

    header(card0, "grid_view", "Workspaces");
    auto* grid = card0->add<Flex>(Flex::Direction::Column);
    grid->setSpacing(16)->setAlignItems(Align::Center);
    grid->grow = true;

    // A workspace tile that lays its windows out like the real monitor
    class Tile : public Button {
    public:
        Hyprland::MonitorGeometry mon;
        std::vector<std::pair<Node*, Hyprland::Client>> windows;
        Tile() : Button(Direction::Row) {}

    protected:
        void doArrange() override {
            const auto& r = rect();
            const float s = std::min(r.w / mon.w, r.h / mon.h);
            for (auto& [node, c] : windows)
                node->arrange({r.x + (c.x - mon.x) * s + 3, r.y + (c.y - mon.y) * s + 3, std::max(8.0f, c.w * s - 6),
                               std::max(8.0f, c.h * s - 6)});
            // The workspace number keeps its corner, drawn over the windows
            if (!children().empty()) children().back()->arrange({r.x + 12, r.y + 8, 40, 28});
        }
        bool clipsChildren() const override { return true; }
    };

    const int columns = std::min(count, count <= 6 ? 3 : 5);
    // Fit the row in the card: output minus popup margins (64) and padding (40)
    const float room = mon.w - 64 - 40 - 16 * static_cast<float>(columns - 1);
    const float tileW = std::min(400.0f, room / static_cast<float>(columns));
    const float tileH = tileW * mon.h / mon.w;
    Flex* row = nullptr;
    for (int id = 1; id <= count; ++id) {
        if ((id - 1) % columns == 0) {
            row = grid->add<Flex>(Flex::Direction::Row);
            row->setSpacing(16);
        }
        auto* tile = row->add<Tile>();
        tile->mon = mon;
        tile->fixedWidth = tileW;
        tile->fixedHeight = tileH;
        tile->setRadius(Radii(20))->setBackground(id == active ? "secondary_container" : "surface_container_high", false);
        if (id == active) tile->setBorder(3, "primary");
        tile->onClicked = [id, &popup](uint32_t button) {
            if (button != BTN_LEFT) return;
            Hyprland::get().focusWorkspace(id);
            popup.close();
        };
        for (const auto& c : clients) {
            if (c.workspace != id) continue;
            auto* win = tile->add<Button>(Flex::Direction::Column);
            win->setRadius(Radii(12))->setBackground("surface_container_highest", false)->setBorder(1, "outline_variant");
            win->setJustify(Align::Center)->setSpacing(4);
            const std::string icon = Apps::get().iconForClass(c.cls);
            win->add<ImageView>(32, [icon](float scale, int& w, int& h) -> GLuint {
                auto* t = Images::get().icon(icon, static_cast<int>(std::lround(32 * scale)));
                if (!t) return 0;
                w = t->width;
                h = t->height;
                return t->id;
            });
            win->add<Label>(c.title.empty() ? c.cls : c.title)->setSize(11)->setMaxWidth(c.w * tileW / mon.w - 16)
                ->setColor("on_surface_variant", false);
            const std::string address = c.address;
            win->onClicked = [address, &popup](uint32_t button) {
                if (button != BTN_LEFT) return;
                Hyprland::get().focusWindow(address);
                popup.close();
            };
            tile->windows.emplace_back(win, c);
        }
        tile->add<Label>(std::to_string(id))->setSize(18)->setWeight(600)->setColor("on_surface_variant", false);
    }
    caption(card0, "Click a window to focus it, or a workspace to switch · Esc closes");
}

} // namespace m3::shell::panels
