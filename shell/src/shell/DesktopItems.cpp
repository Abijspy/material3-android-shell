#include "shell/DesktopItems.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "quickshare/QuickShare.h"
#include "render/Images.h"
#include "services/Search.h"
#include "shell/ClockFaces.h"
#include "shell/LockView.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/bar/Widgets.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <linux/input-event-codes.h>

namespace m3::shell::desktop {

using namespace m3::ui;

namespace {

// A big clock and date, Material You style; formats from Settings →
// Wallpaper & style
class BigClock : public Flex {
public:
    BigClock() : Flex(Direction::Column) {
        setAlignItems(Align::Start)->setSpacing(-6);
        m_time = add<Label>();
        m_time->setSize(96)->setWeight(500)->setColor("primary", false);
        m_date = add<Label>();
        m_date->setSize(22)->setWeight(500)->setColor("on_surface", false);
        m_config = Config::get().changed.connect([this] {
            if (m_timer) EventLoop::get().cancel(m_timer);
            m_timer = 0;
            tick();
        });
        tick();
    }
    ~BigClock() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

private:
    void tick() {
        auto& c = Config::get();
        const auto time = c.value<std::string>("/desktop/timeFormat", "hh:mm");
        m_time->setText(Clock::sample(time));
        m_date->setText(Clock::sample(c.value<std::string>("/desktop/dateFormat", "dddd, d MMMM")));
        m_date->setVisible(c.value<bool>("/desktop/showDate", true));
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        // Seconds tick every second, the rest on the minute
        m_timer = EventLoop::get().addTimer(time.find('s') != std::string::npos ? 1000 : (60 - tm.tm_sec) * 1000 + 20, [this] {
            m_timer = 0;
            tick();
        });
    }
    Label *m_time, *m_date;
    EventLoop::TimerId m_timer = 0;
    Connection m_config;
};

// Hours over minutes, big and bold in two tones (Android's lock screen)
class StackedClock : public Flex {
public:
    StackedClock() : Flex(Direction::Column) {
        setAlignItems(Align::Start)->setSpacing(-38);
        m_hours = add<Label>();
        m_hours->setSize(132)->setWeight(700)->setColor("primary", false);
        m_minutes = add<Label>();
        m_minutes->setSize(132)->setWeight(700)->setColor("tertiary", false);
        // Clear of the minutes' descent, which the tight spacing overlaps
        m_dateRow = add<Flex>(Direction::Row);
        m_dateRow->setPadding({4, 34, 0, 0});
        m_date = m_dateRow->add<Label>();
        m_date->setSize(20)->setWeight(500)->setColor("on_surface", false);
        m_ticker = std::make_unique<kit::SecondTicker>([this] { sync(); });
        sync();
    }

private:
    void sync() {
        auto& c = Config::get();
        const auto time = c.value<std::string>("/desktop/timeFormat", "hh:mm");
        const bool twelve = time.find("AP") != std::string::npos || time.find("ap") != std::string::npos;
        m_hours->setText(Clock::sample(twelve ? "h" : "hh"));
        m_minutes->setText(Clock::sample("mm"));
        m_date->setText(Clock::sample(c.value<std::string>("/desktop/dateFormat", "dddd, d MMMM")));
        m_dateRow->setVisible(c.value<bool>("/desktop/showDate", true));
    }
    Label *m_hours, *m_minutes, *m_date;
    Flex* m_dateRow;
    std::unique_ptr<kit::SecondTicker> m_ticker;
};

// A face with the date under it, if the desktop clock shows one
class AnalogClock : public Flex {
public:
    explicit AnalogClock(kit::AnalogFace::Style style) : Flex(Direction::Column) {
        setAlignItems(Align::Center)->setSpacing(14);
        add<kit::AnalogFace>(style, 220 * kit::widgetScale());
        auto& c = Config::get();
        auto* date = add<Label>(Clock::sample(c.value<std::string>("/desktop/dateFormat", "dddd, d MMMM")));
        date->setSize(18)->setWeight(500)->setColor("on_surface", false);
        date->setVisible(c.value<bool>("/desktop/showDate", true));
    }
};

const Apps::App* appById(const std::string& id) {
    for (const auto& a : Apps::get().all())
        if (a.id == id) return &a;
    return nullptr;
}

// A theme icon for a file, by what it is
std::string fileIcon(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) return "folder";
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif" || ext == ".svg") return "image-x-generic";
    if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov") return "video-x-generic";
    if (ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav" || ext == ".m4a") return "audio-x-generic";
    if (ext == ".pdf") return "application-pdf";
    if (ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".xz" || ext == ".zst" || ext == ".7z") return "package-x-generic";
    if (ext == ".sh" || ext == ".py") return "text-x-script";
    return "text-x-generic";
}

std::string shellQuoted(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

// An app or file on the desktop: its icon, and its name on a soft pill so
// it reads on any wallpaper
Node* shortcut(Node* parent, const std::string& icon, const std::string& name, std::function<void()> open, bool editing) {
    auto* b = parent->add<Button>(Flex::Direction::Column);
    b->setPadding({6, 8})->setSpacing(6)->setRadius(Radii(18));
    b->pressedRadius = 24;
    const float k = kit::widgetScale();
    b->fixedWidth = 96 * k;
    b->add<ImageView>(52 * k, [icon, k](float scale, int& w, int& h) -> GLuint {
        auto* t = Images::get().icon(icon, static_cast<int>(std::lround(52 * k * scale)));
        if (!t) t = Images::get().icon("application-x-executable", static_cast<int>(std::lround(52 * k * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
    auto* tag = b->add<Flex>(Flex::Direction::Row);
    tag->setPadding({8, 2})->setRadius(Radii(10))->setBackground(ColorRef("surface", 0.55f), false);
    tag->add<Label>(name)->setSize(12)->setWeight(500)->setMaxWidth(80)->setMaxLines(2)->setCentered(true)->setColor(
        "on_surface", false);
    b->tooltip = name;
    if (!editing)
        b->onClicked = [open = std::move(open)](uint32_t button) {
            if (button == BTN_LEFT && open) open();
        };
    return b;
}

// A dashboard card at a comfortable width
Node* cardWidget(Node* parent, const std::string& id) {
    auto* holder = parent->add<Flex>(Flex::Direction::Column);
    holder->setAlignItems(Align::Stretch);
    holder->fixedWidth = 340 * kit::widgetScale();
    // Each card blurs the wallpaper behind it (Look & feel → Blur)
    Node* c = id == "player" ? holder->add<kit::MediaCard>() : panels::dashboardWidget(holder, id);
    if (c) c->blurBehind = true;
    return holder;
}

} // namespace

const std::vector<WidgetDef>& widgets() {
    static const std::string quickShare = quickshare::QuickShare::symbol(); // its own symbol, as a path
    static const std::vector<WidgetDef> all = {
        {"clock", "schedule", "Digital clock", "The time and date, large"},
        {"clock-stacked", "view_agenda", "Stacked clock", "Hours over minutes, bold, in two tones"},
        {"clock-analog", "nest_clock_farsight_analog", "Analog clock", "A Material You dial with rounded hands"},
        {"clock-dots", "radio_button_checked", "Minimal analog", "Slim hands over a ring of dots"},
        {"date", "calendar_today", "Date", "Today's date"},
        {"calendar", "calendar_month", "Calendar", "This month"},
        {"weather", "partly_cloudy_day", "Weather", "Now and the days ahead"},
        {"forecast", "rainy", "Forecast", "The next hours"},
        {"player", "music_note", "Music player", "What's playing, with its controls"},
        {"cal-day", "today", "Day tile", "Today as a big tile"},
        {"cal-week", "view_week", "Week", "This week, today circled"},
        {"cal-month", "calendar_view_month", "Month", "This month as a grid"},
        {"cal-agenda", "event_upcoming", "Agenda", "Your next events"},
        {"music-disc", "album", "Music disc", "The cover in a wavy progress ring"},
        {"music-bar", "graphic_eq", "Music bar", "A slim player with wavy progress"},
        {"weather-big", "thermostat", "Big weather", "The temperature, large"},
        {"weather-card", "partly_cloudy_day", "Weather card", "Now and the next four days"},
        {"system", "monitoring", "System", "CPU, memory, disk and uptime"},
        {"battery", "battery_full", "Battery", "Charge and status"},
        {"notifications", "notifications", "Notifications", "How many are waiting"},
        {"screentime", "digital_wellbeing", "Screen time", "Today's time, by app"},
        {"wellbeing-modes", "self_improvement", "Focus & Bedtime", "Switch Focus mode or Bedtime"},
        {"vpn", "vpn_key", "VPN", "The VPN in use, with a switch"},
        {"quickshare", quickShare.c_str(), "Quick Share", "Visible or not, and Send"},
    };
    return all;
}

std::vector<Item> load() {
    const auto j = Config::get().value<nlohmann::json>("/desktop/items", nlohmann::json());
    std::vector<Item> out;
    if (!j.is_array()) {
        // Before the editor: the clock, where it was (72, 140 on 1280 × 720)
        if (Config::get().value<bool>("/desktop/clock", true)) out.push_back({"widget", "clock", 0.056f, 0.19f});
        return out;
    }
    for (const auto& i : j)
        if (i.is_object())
            out.push_back({i.value("kind", "widget"), i.value("id", ""), i.value("x", 0.05f), i.value("y", 0.2f), i.value("scale", 1.0f)});
    return out;
}

void save(const std::vector<Item>& items) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& i : items) {
        nlohmann::json j = {{"kind", i.kind}, {"id", i.id}, {"x", i.x}, {"y", i.y}};
        if (std::abs(i.scale - 1) > 0.001f) j["scale"] = i.scale;
        out.push_back(j);
    }
    Config::get().set("/desktop/items", out);
}

std::string nameOf(const Item& item) {
    if (item.kind == "app") {
        const auto* a = appById(item.id);
        return a ? a->name : item.id;
    }
    if (item.kind == "file") {
        const auto name = std::filesystem::path(item.id).filename().string();
        return name.empty() ? item.id : name;
    }
    for (const auto& w : widgets())
        if (item.id == w.id) return w.name;
    return item.id;
}

namespace {

Node* buildItem(Node* parent, const Item& item, bool editing) {
    if (item.kind == "app") {
        const auto* a = appById(item.id);
        const std::string id = item.id;
        return shortcut(parent, a ? a->icon : "application-x-executable", nameOf(item),
                        [id] {
                            if (const auto* app = appById(id)) Apps::get().launch(*app);
                        },
                        editing);
    }
    if (item.kind == "file") {
        const std::string path = item.id;
        return shortcut(parent, fileIcon(path), nameOf(item), [path] { spawn("xdg-open " + shellQuoted(path)); }, editing);
    }
    if (item.id == "clock") return parent->add<BigClock>();
    if (item.id == "clock-stacked") return parent->add<StackedClock>();
    if (item.id == "clock-analog") return parent->add<AnalogClock>(kit::AnalogFace::Style::Cookie);
    if (item.id == "clock-dots") return parent->add<AnalogClock>(kit::AnalogFace::Style::Dots);
    if (item.id == "calendar" || item.id == "weather" || item.id == "system" || item.id == "player") return cardWidget(parent, item.id);
    // The lock screen's widgets: date, forecast, battery, notifications
    return addLockWidget(parent, item.id, false, 0);
}

} // namespace

Node* build(Node* parent, const Item& item, bool editing) {
    // Built at its size; its text and icons follow it too
    const kit::WidgetScaleScope scope(std::clamp(item.scale, 0.5f, 2.5f));
    Node* node = buildItem(parent, item, editing);
    if (node) node->textMul = std::clamp(item.scale, 0.5f, 2.5f);
    return node;
}

void Board::doArrange() {
    const auto& b = rect();
    const auto& kids = children();
    for (size_t i = 0; i < kids.size(); ++i) {
        const auto spot = i < spots.size() ? spots[i] : std::pair{0.05f, 0.2f};
        const SizeF s = kids[i]->measure({b.w, b.h});
        const float x = std::clamp(b.x + spot.first * b.w, b.x, std::max(b.x, b.right() - s.w));
        const float y = std::clamp(b.y + spot.second * b.h, b.y, std::max(b.y, b.bottom() - s.h));
        kids[i]->arrange({x, y, s.w, s.h});
    }
    if (onArranged) onArranged();
}

} // namespace m3::shell::desktop
