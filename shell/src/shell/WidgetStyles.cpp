#include "shell/WidgetStyles.h"

#include "core/EventLoop.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/Mpris.h"
#include "services/Weather.h"
#include "shell/ClockFaces.h"
#include "shell/PanelKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cmath>
#include <ctime>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::tm localNow(long offsetDays = 0) {
    const std::time_t t = std::time(nullptr) + offsetDays * 86400;
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

std::string format(const char* fmt, const std::tm& tm) {
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

// A translucent card that reads on any wallpaper
Flex* card(Node* parent, Flex::Direction dir = Flex::Direction::Column) {
    auto* c = parent->add<Flex>(dir);
    c->setBackground(ColorRef("surface_container_high", 0.8f), false)->setRadius(Radii(28))->setPadding({18, 16})->setSpacing(10);
    c->blurBehind = true;
    return c;
}

// A widget that rebuilds when its services change, and each minute (the date
// turning over)
class Styled : public Live {
public:
    Styled(std::string id, bool greeter, Align align) : m_id(std::move(id)), m_greeter(greeter) {
        setAlignItems(align)->setSpacing(0);
        sync = [this] { build(); };
        if (m_id.starts_with("weather")) watch(Weather::get().changed);
        if (m_id.starts_with("music")) {
            watch(Mpris::get().changed);
            // The progress moves on by itself
            m_ticker = std::make_unique<SecondTicker>([this] { progress(); });
        }
        minute();
        build();
    }
    ~Styled() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

private:
    void minute() {
        m_timer = EventLoop::get().addTimer((60 - localNow().tm_sec) * 1000 + 50, [this] {
            m_timer = 0;
            syncSoon();
            minute();
        });
    }
    void build();
    void calendarDay();
    void calendarWeek();
    void calendarMonth();
    void musicDisc(const Mpris::Player& p);
    void musicBar(const Mpris::Player& p);
    void weatherBig();
    void weatherCard();
    Flex* controls(Node* parent, const Mpris::Player& p, float big);
    void progress() {
        const auto* p = Mpris::get().active();
        if (!m_progress || !p || p->length <= 0) return;
        m_progress->set(static_cast<float>(static_cast<double>(p->positionNow()) / static_cast<double>(p->length)), p->status == "Playing");
    }

    std::string m_id;
    bool m_greeter;
    EventLoop::TimerId m_timer = 0;
    std::unique_ptr<SecondTicker> m_ticker;
    WavyProgress* m_progress = nullptr;
};

// The cover, rounded (a circle for the disc); a note when there's none
class ArtView : public Flex {
public:
    ArtView(std::string path, float size, float radius) : Flex(Direction::Row), m_path(std::move(path)), m_radius(radius) {
        fixedWidth = fixedHeight = size;
        setJustify(Align::Center)->setRadius(Radii(radius))->setBackground("secondary_container", false);
        if (m_path.empty()) add<Icon>("music_note", size * 0.4f)->setColor("on_secondary_container", false);
    }
    ~ArtView() override { *m_alive = false; }

protected:
    void doPaint(Renderer& r) override {
        Flex::doPaint(r);
        if (m_path.empty()) return;
        const auto& b = rect();
        const int px = static_cast<int>(std::lround(b.w * r.scale()));
        if (auto* t = Images::get().picture(m_path, px, px, true, [this, alive = m_alive] {
                if (*alive) requestRedraw();
            }))
            r.image(t->id, b, 1, Radii(std::min(m_radius, b.w / 2)));
    }

private:
    std::string m_path;
    float m_radius;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

void Styled::build() {
    clearChildren();
    m_progress = nullptr;
    setVisible(true);
    if (m_id == "cal-day") return calendarDay();
    if (m_id == "cal-week") return calendarWeek();
    if (m_id == "cal-month") return calendarMonth();
    if (m_id.starts_with("music")) {
        // Never on the login screen: nobody's music plays there
        const auto* p = m_greeter ? nullptr : Mpris::get().active();
        if (!p) return setVisible(false);
        return m_id == "music-disc" ? musicDisc(*p) : musicBar(*p);
    }
    if (!Weather::get().ready()) return setVisible(false);
    if (m_id == "weather-big") return weatherBig();
    weatherCard();
}

// ── Calendars ──

// Today as a tile: the weekday, a big day number and the month
void Styled::calendarDay() {
    const std::tm tm = localNow();
    auto* tile = add<Flex>(Flex::Direction::Column);
    tile->setBackground(ColorRef("primary_container", 0.92f), false)->setRadius(Radii(32))->setPadding({26, 16});
    tile->setAlignItems(Align::Center)->setSpacing(-8);
    tile->blurBehind = true;
    tile->add<Label>(format("%A", tm))->setSize(16)->setWeight(600)->setColor("on_primary_container", false);
    tile->add<Label>(format("%-d", tm))->setSize(80)->setWeight(700)->setColor("on_primary_container", false);
    tile->add<Label>(format("%B", tm))->setSize(15)->setWeight(500)->setColor("on_primary_container", false);
}

// This week, Monday to Sunday, today in a filled circle
void Styled::calendarWeek() {
    const std::tm today = localNow();
    const int fromMonday = (today.tm_wday + 6) % 7;
    auto* row = card(this, Flex::Direction::Row);
    row->setSpacing(4)->setPadding({12, 12});
    for (int i = 0; i < 7; ++i) {
        const std::tm d = localNow(i - fromMonday);
        const bool isToday = i == fromMonday;
        auto* col = row->add<Flex>(Flex::Direction::Column);
        col->setAlignItems(Align::Center)->setSpacing(6);
        col->fixedWidth = 44;
        col->add<Label>(format("%a", d).substr(0, 1))->setSize(12)->setWeight(600)->setColor(isToday ? "primary" : "on_surface_variant", false);
        auto* num = col->add<Flex>(Flex::Direction::Row);
        pill(num)->setJustify(Align::Center);
        num->fixedWidth = num->fixedHeight = 36;
        if (isToday) num->setBackground("primary", false);
        num->add<Label>(format("%-d", d))->setSize(15)->setWeight(isToday ? 700 : 500)->setColor(isToday ? "on_primary" : "on_surface", false);
    }
}

// The month as a grid, today picked out
void Styled::calendarMonth() {
    const std::tm today = localNow();
    auto* c = card(this);
    c->setSpacing(4)->setPadding({16, 14});
    c->add<Label>(format("%B %Y", today))->setSize(16)->setWeight(600)->setColor("primary", false);
    auto cell = [](Node* parent, const std::string& text, bool header, bool isToday, bool dim) {
        auto* f = parent->add<Flex>(Flex::Direction::Row);
        pill(f)->setJustify(Align::Center);
        f->fixedWidth = 36;
        f->fixedHeight = header ? 22 : 32;
        if (isToday) f->setBackground("primary", false);
        f->add<Label>(text)
            ->setSize(header ? 11 : 13)
            ->setWeight(header || isToday ? 600 : 400)
            ->setColor(isToday ? "on_primary" : header || dim ? "on_surface_variant" : "on_surface", false);
    };
    auto* head = c->add<Flex>(Flex::Direction::Row);
    for (const char* d : {"M", "T", "W", "T", "F", "S", "S"}) cell(head, d, true, false, false);
    // From the Monday on or before the 1st, six weeks at most
    std::tm first = today;
    first.tm_mday = 1;
    first.tm_hour = 12;
    std::mktime(&first);
    const int lead = (first.tm_wday + 6) % 7;
    const int month = today.tm_mon;
    for (int week = 0; week < 6; ++week) {
        if (week > 0) {
            std::tm probe = first;
            probe.tm_mday = 1 - lead + week * 7;
            std::mktime(&probe);
            if (probe.tm_mon != month) break; // past the month's end
        }
        auto* row = c->add<Flex>(Flex::Direction::Row);
        for (int i = 0; i < 7; ++i) {
            std::tm d = first;
            d.tm_mday = 1 - lead + week * 7 + i;
            std::mktime(&d);
            const bool in = d.tm_mon == month;
            cell(row, in ? std::to_string(d.tm_mday) : "", false, in && d.tm_mday == today.tm_mday, !in);
        }
    }
}

// ── Music ──

Flex* Styled::controls(Node* parent, const Mpris::Player& p, float big) {
    auto* row = parent->add<Flex>(Flex::Direction::Row);
    row->setSpacing(8)->setAlignItems(Align::Center);
    auto button = [&](const char* icon, bool main, std::function<void()> fn) {
        auto* b = row->add<Button>();
        pill(b)->setJustify(Align::Center);
        b->fixedWidth = b->fixedHeight = main ? big : big * 0.75f;
        if (main) b->setBackground("primary", false);
        b->add<Icon>(icon, main ? big * 0.5f : big * 0.42f)->setColor(main ? "on_primary" : "on_surface", false);
        b->onClicked = [fn = std::move(fn)](uint32_t button) {
            if (button == BTN_LEFT) fn();
        };
    };
    button("skip_previous", false, [] { Mpris::get().previous(); });
    button(p.status == "Playing" ? "pause" : "play_arrow", true, [] { Mpris::get().playPause(); });
    button("skip_next", false, [] { Mpris::get().next(); });
    return row;
}

// The cover as a disc in the wavy progress ring, the song and controls under it
void Styled::musicDisc(const Mpris::Player& p) {
    auto* col = add<Flex>(Flex::Direction::Column);
    col->setAlignItems(Align::Center)->setSpacing(8);
    auto* ring = col->add<WavyRing>(176);
    ring->add<ArtView>(Mpris::get().artPath(p), 144, 72);
    col->add<Label>(p.title.empty() ? p.identity : p.title)->setSize(18)->setWeight(600)->setMaxWidth(280)->setColor("on_surface", false);
    if (!p.artist.empty()) col->add<Label>(p.artist)->setSize(14)->setMaxWidth(280)->setColor("on_surface_variant", false);
    controls(col, p, 52);
}

// A slim bar: cover, song, wavy progress and play/pause beside it
void Styled::musicBar(const Mpris::Player& p) {
    auto* bar = card(this, Flex::Direction::Row);
    bar->setRadius(Radii(32))->setPadding({10, 10, 14, 10})->setSpacing(14)->setAlignItems(Align::Center);
    bar->fixedWidth = 440;
    bar->add<ArtView>(Mpris::get().artPath(p), 64, 20);
    auto* text = bar->add<Flex>(Flex::Direction::Column);
    text->setSpacing(2)->setAlignItems(Align::Stretch);
    text->grow = true;
    text->add<Label>(p.title.empty() ? p.identity : p.title)->setSize(15)->setWeight(600)->setMaxWidth(220)->setColor("on_surface", false);
    text->add<Label>(p.artist.empty() ? p.identity : p.artist)->setSize(13)->setMaxWidth(220)->setColor("on_surface_variant", false);
    m_progress = text->add<WavyProgress>();
    if (p.canSeek && p.length > 0) m_progress->onSeek = [](float f) { Mpris::get().seek(f); };
    progress();
    auto* row = bar->add<Flex>(Flex::Direction::Row);
    row->setSpacing(4);
    auto* play = row->add<Button>();
    pill(play)->setJustify(Align::Center)->setBackground("primary", false);
    play->fixedWidth = play->fixedHeight = 44;
    play->add<Icon>(p.status == "Playing" ? "pause" : "play_arrow", 24)->setColor("on_primary", false);
    play->onClicked = [](uint32_t b) {
        if (b == BTN_LEFT) Mpris::get().playPause();
    };
    auto* next = row->add<Button>();
    pill(next)->setJustify(Align::Center);
    next->fixedWidth = next->fixedHeight = 40;
    next->add<Icon>("skip_next", 22)->setColor("on_surface", false);
    next->onClicked = [](uint32_t b) {
        if (b == BTN_LEFT) Mpris::get().next();
    };
}

// ── Weather ──

// The temperature, large, beside the sky
void Styled::weatherBig() {
    auto& w = Weather::get();
    auto* row = add<Flex>(Flex::Direction::Row);
    row->setSpacing(16)->setAlignItems(Align::Center);
    row->add<Icon>(w.icon(), 84)->setFill(true)->setColor("primary", false);
    auto* col = row->add<Flex>(Flex::Direction::Column);
    col->setSpacing(-4);
    col->add<Label>(w.temp(w.temperature()))->setSize(72)->setWeight(400)->setColor("on_surface", false);
    std::string line = Weather::describe(w.code());
    if (!w.days().empty()) line += "  ·  " + w.temp(w.days()[0].max) + " / " + w.temp(w.days()[0].min);
    col->add<Label>(line)->setSize(15)->setWeight(500)->setColor("on_surface_variant", false);
}

// Now and the next four days, on a card
void Styled::weatherCard() {
    auto& w = Weather::get();
    auto* c = card(this);
    c->fixedWidth = 360;
    c->setSpacing(12)->setPadding({20, 18})->setAlignItems(Align::Stretch);
    auto* top = c->add<Flex>(Flex::Direction::Row);
    top->setAlignItems(Align::Center);
    auto* now = top->add<Flex>(Flex::Direction::Column);
    now->setSpacing(0)->setAlignItems(Align::Start);
    now->grow = true;
    now->add<Label>(w.place().substr(0, w.place().find(',')))->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    now->add<Label>(w.temp(w.temperature()))->setSize(48)->setWeight(500)->setColor("on_surface", false);
    now->add<Label>(Weather::describe(w.code()))->setSize(14)->setColor("on_surface", false);
    top->add<Icon>(w.icon(), 64)->setFill(true)->setColor("primary", false);
    auto* days = c->add<Flex>(Flex::Direction::Row);
    days->setSpacing(6);
    for (size_t i = 1; i < std::min<size_t>(w.days().size(), 5); ++i) {
        const auto& d = w.days()[i];
        std::tm tm{};
        strptime(d.date.c_str(), "%Y-%m-%d", &tm);
        auto* day = days->add<Flex>(Flex::Direction::Column);
        day->setAlignItems(Align::Center)->setSpacing(4)->setPadding({0, 8})->setRadius(Radii(18));
        day->setBackground(ColorRef("surface_container_highest", 0.7f), false);
        day->grow = true;
        day->add<Label>(format("%a", tm))->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
        day->add<Icon>(Weather::icon(d.code, true), 22)->setColor("primary", false);
        day->add<Label>(w.temp(d.max))->setSize(13)->setWeight(600)->setColor("on_surface", false);
        day->add<Label>(w.temp(d.min))->setSize(12)->setColor("on_surface_variant", false);
    }
}

} // namespace

Node* addStyledWidget(Node* parent, const std::string& id, bool greeter, Align align) {
    static const char* ids[] = {"cal-day", "cal-week", "cal-month", "music-disc", "music-bar", "weather-big", "weather-card"};
    for (const char* known : ids)
        if (id == known) return parent->add<Styled>(id, greeter, align);
    return nullptr;
}

} // namespace m3::shell
