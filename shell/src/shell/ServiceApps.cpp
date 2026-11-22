// Clock, Calendar and Weather as app windows of the shell's own. They live
// in the shell (not programs apart) because their services do: the alarms
// that ring, the reminders that come, the forecast fetched every quarter of
// an hour. A program of its own would run a second copy of each.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "services/Calendar.h"
#include "services/Weather.h"
#include "shell/ClockPanel.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <ctime>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// A window's content kept to a comfortable width, in the middle
Flex* centred(Flex* card, float width) {
    card->setPadding({16, 8, 16, 16})->setAlignItems(Align::Center);
    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    scroll->alignSelf = Align::Stretch;
    auto* col = scroll->add<Flex>(Flex::Direction::Column);
    col->setAlignItems(Align::Center);
    auto* inner = col->add<Flex>(Flex::Direction::Column);
    inner->setSpacing(16)->setAlignItems(Align::Stretch);
    inner->fixedWidth = width;
    return inner;
}

std::string dayName(const std::string& ymd, int index) {
    if (index == 0) return "Today";
    if (index == 1) return "Tomorrow";
    std::tm tm{};
    if (!strptime(ymd.c_str(), "%Y-%m-%d", &tm)) return ymd;
    char b[16];
    std::mktime(&tm);
    std::strftime(b, sizeof b, "%A", &tm);
    return b;
}

void tileText(Flex* t, const std::string& icon, const std::string& title, const std::string& value) {
    auto* h = t->add<Flex>(Flex::Direction::Row);
    h->setSpacing(8)->setAlignItems(Align::Center);
    h->add<Icon>(icon, 18)->setColor("on_surface_variant", false);
    h->add<Label>(title)->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
    t->add<Label>(value)->setSize(22)->setWeight(500);
}

} // namespace

void weatherApp(Flex* card, Popup& popup) {
    (void)popup;
    auto* c = centred(card, 640);
    auto alive = std::make_shared<bool>(true);
    struct Keeper : Node {
        std::shared_ptr<bool> a;
        Connection conn;
        explicit Keeper(std::shared_ptr<bool> x) : a(std::move(x)) { floating = true; }
        ~Keeper() override { *a = false; }
        SizeF doMeasure(SizeF) override { return {0, 0}; }
    };
    auto* keeper = card->add<Keeper>(alive);
    auto show = std::make_shared<std::function<void()>>();
    *show = [c] {
        c->clearChildren();
        auto& w = Weather::get();
        if (!w.enabled()) {
            caption(c, "Weather is off: Settings → Weather");
            return;
        }
        if (!w.ready()) {
            caption(c, w.error().empty() ? "Getting the weather…" : w.error());
            return;
        }
        // Now
        auto* now = c->add<Flex>(Flex::Direction::Row);
        now->setSpacing(20)->setAlignItems(Align::Center)->setPadding({24, 22})->setRadius(Radii(32))->setBackground("primary_container", false);
        now->add<Icon>(w.icon(), 96)->setFill(true)->setColor("on_primary_container", false);
        auto* t = now->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(2);
        t->grow = true;
        t->add<Label>(w.temp(w.temperature()))->setSize(64)->setWeight(400)->setColor("on_primary_container", false);
        t->add<Label>(Weather::describe(w.code()))->setSize(18)->setWeight(500)->setColor("on_primary_container", false);
        t->add<Label>(std::format("Feels like {}  ·  {}", w.temp(w.feelsLike()), w.place()))->setSize(13)->setColor("on_primary_container", false)->setMaxWidth(360);
        if (!w.days().empty()) {
            auto* hl = now->add<Flex>(Flex::Direction::Column);
            hl->setAlignItems(Align::End);
            hl->add<Label>("↑ " + w.temp(w.days().front().max))->setSize(18)->setWeight(500)->setColor("on_primary_container", false);
            hl->add<Label>("↓ " + w.temp(w.days().front().min))->setSize(18)->setColor("on_primary_container", false);
        }
        if (w.rainIn() >= 0)
            caption(c, w.rainIn() == 0 ? "Raining now" : std::format("Rain in about {} minutes", std::max(5, (w.rainIn() + 4) / 5 * 5)));
        // The next hours
        if (!w.hours().empty()) {
            auto* g = group(c, "The next hours");
            auto* row = g->add<Flex>(Flex::Direction::Row);
            row->setSpacing(4)->setPadding({8, 12});
            for (size_t i = 0; i < w.hours().size() && i < 12; i += 1) {
                const auto& h = w.hours()[i * 2 < w.hours().size() ? i * 2 : i];
                auto* col = row->add<Flex>(Flex::Direction::Column);
                col->setAlignItems(Align::Center)->setSpacing(6);
                col->grow = true;
                col->add<Label>(i == 0 ? "Now" : h.time)->setSize(12)->setColor("on_surface_variant", false);
                col->add<Icon>(Weather::icon(h.code, h.day), 24)->setColor("primary", false);
                col->add<Label>(w.temp(h.temp))->setSize(14)->setWeight(500);
                col->add<Label>(h.rainChance >= 20 ? std::format("{}%", h.rainChance) : " ")->setSize(11)->setColor("tertiary", false);
                if (i * 2 + 2 >= w.hours().size()) break;
            }
        }
        // The days
        if (!w.days().empty()) {
            auto* g = group(c, std::format("{} days", w.days().size()));
            double lo = 1e9, hi = -1e9;
            for (const auto& d : w.days()) lo = std::min(lo, d.min), hi = std::max(hi, d.max);
            for (size_t i = 0; i < w.days().size(); ++i) {
                const auto& d = w.days()[i];
                auto* r = g->add<Flex>(Flex::Direction::Row);
                r->setSpacing(14)->setAlignItems(Align::Center)->setPadding({16, 10});
                auto* name = r->add<Label>(dayName(d.date, static_cast<int>(i)));
                name->setSize(14)->setWeight(500);
                name->fixedWidth = 110;
                r->add<Icon>(Weather::icon(d.code, true), 22)->setColor("primary", false);
                auto* rain = r->add<Label>(d.rainChance >= 20 ? std::format("{}%", d.rainChance) : "");
                rain->setSize(12)->setColor("tertiary", false);
                rain->fixedWidth = 40;
                r->add<Label>(w.temp(d.min))->setSize(14)->setColor("on_surface_variant", false)->fixedWidth = 46;
                // The day's span on the week's scale
                auto* track = r->add<Flex>(Flex::Direction::Row);
                track->grow = true;
                track->fixedHeight = 6;
                track->setRadius(Radii(3))->setBackground("surface_container_highest", false);
                const float span = static_cast<float>(std::max(1.0, hi - lo));
                track->add<Node>()->fixedWidth = 220.0f * static_cast<float>(d.min - lo) / span;
                auto* fill = track->add<Flex>(Flex::Direction::Row);
                fill->fixedHeight = 6;
                fill->fixedWidth = std::max(8.0f, 220.0f * static_cast<float>(d.max - d.min) / span);
                fill->setRadius(Radii(3))->setBackground("primary", false);
                r->add<Label>(w.temp(d.max))->setSize(14)->setWeight(500)->fixedWidth = 46;
            }
        }
        // Today's details
        if (!w.days().empty()) {
            const auto& d = w.days().front();
            auto* grid = c->add<Flow>();
            grid->spacing = 12;
            auto tile = [&](const std::string& icon, const std::string& title, const std::string& value) {
                auto* t2 = grid->add<Flex>(Flex::Direction::Column);
                t2->setPadding({16, 14})->setSpacing(6)->setRadius(Radii(22))->setBackground("surface_container_high", false);
                t2->fixedWidth = 198;
                tileText(t2, icon, title, value);
            };
            tile("humidity_percentage", "Humidity", std::format("{}%", w.humidity()));
            tile("air", "Wind", w.windText());
            tile("wb_sunny", "UV index", std::format("{:.0f}{}", d.uv, d.uv >= 8 ? "  very high" : d.uv >= 6 ? "  high" : d.uv >= 3 ? "  moderate" : "  low"));
            if (!d.sunrise.empty()) tile("wb_twilight", "Sunrise", d.sunrise);
            if (!d.sunset.empty()) tile("nights_stay", "Sunset", d.sunset);
            tile("umbrella", "Chance of rain", std::format("{}%", d.rainChance));
        }
        auto* foot = c->add<Flex>(Flex::Direction::Row);
        foot->setSpacing(8)->setAlignItems(Align::Center);
        char when[16] = "";
        if (w.updated()) {
            const std::time_t u = static_cast<std::time_t>(w.updated());
            std::strftime(when, sizeof when, "%H:%M", std::localtime(&u));
        }
        auto* up = foot->add<Label>(w.updated() ? std::string("Updated ") + when + "  ·  Open-Meteo" : "Open-Meteo");
        up->setSize(12)->setColor("on_surface_variant", false);
        up->grow = true;
        pillButton(foot, "Refresh", false, [] { Weather::get().refresh(); }, "refresh");
        pillButton(foot, "Change the place", false, [] {
            if (open) open("settings weather");
        }, "location_on");
    };
    keeper->conn = Weather::get().changed.connect([show, alive] {
        if (*alive) (*show)();
    });
    (*show)();
    if (Weather::get().enabled() && !Weather::get().hours().size()) Weather::get().refresh();
}

void calendarApp(Flex* card, Popup& popup, const std::string& arg) {
    (void)popup;
    (void)arg;
    card->setPadding(Insets(14))->setSpacing(0);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(16)->setAlignItems(Align::Stretch);
    columns->grow = true;
    auto* left = columns->add<Flex>(Flex::Direction::Column);
    left->setSpacing(10)->setAlignItems(Align::Stretch)->setPadding({8, 4});
    left->fixedWidth = 380;
    auto* scroll = columns->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    auto* right = scroll->add<Flex>(Flex::Direction::Column);
    right->setSpacing(10)->setAlignItems(Align::Stretch)->setPadding({4, 4, 12, 12});

    struct S {
        int year, month, day;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
        std::function<void()> show;
    };
    auto st = std::make_shared<S>();
    {
        const std::time_t now = std::time(nullptr);
        const std::tm tm = *std::localtime(&now);
        st->year = tm.tm_year + 1900, st->month = tm.tm_mon + 1, st->day = tm.tm_mday;
    }
    struct Keeper : Node {
        std::shared_ptr<S> s;
        Connection conn;
        explicit Keeper(std::shared_ptr<S> x) : s(std::move(x)) { floating = true; }
        ~Keeper() override {
            *s->alive = false;
            s->show = nullptr;
        }
        SizeF doMeasure(SizeF) override { return {0, 0}; }
    };
    auto* keeper = card->add<Keeper>(st);

    auto dayStart = [](int y, int m, int d) {
        std::tm tm{};
        tm.tm_year = y - 1900, tm.tm_mon = m - 1, tm.tm_mday = d, tm.tm_isdst = -1;
        return std::mktime(&tm);
    };
    st->show = [st, left, right, dayStart] {
        auto& cal = Calendar::get();
        left->clearChildren();
        right->clearChildren();
        // The month
        auto* head = left->add<Flex>(Flex::Direction::Row);
        head->setSpacing(4)->setAlignItems(Align::Center);
        std::tm first{};
        first.tm_year = st->year - 1900, first.tm_mon = st->month - 1, first.tm_mday = 1, first.tm_isdst = -1;
        std::mktime(&first);
        char title[32];
        std::strftime(title, sizeof title, "%B %Y", &first);
        auto* tl = head->add<Label>(title);
        tl->setSize(24)->setWeight(500);
        tl->grow = true;
        auto step = [st](int by) {
            st->month += by;
            if (st->month < 1) st->month = 12, --st->year;
            if (st->month > 12) st->month = 1, ++st->year;
            st->day = 1;
            EventLoop::get().defer([st] {
                if (*st->alive && st->show) st->show();
            });
        };
        iconButton(head, "chevron_left", [step] { step(-1); }, 22);
        iconButton(head, "today", [st] {
            const std::time_t now = std::time(nullptr);
            const std::tm tm = *std::localtime(&now);
            st->year = tm.tm_year + 1900, st->month = tm.tm_mon + 1, st->day = tm.tm_mday;
            EventLoop::get().defer([st] {
                if (*st->alive && st->show) st->show();
            });
        }, 20)->tooltip = "Today";
        iconButton(head, "chevron_right", [step] { step(1); }, 22);
        const bool monday = Config::get().value<bool>("/calendar/mondayFirst", true);
        auto* names = left->add<Flex>(Flex::Direction::Row);
        const char* wk[] = {"S", "M", "T", "W", "T", "F", "S"};
        for (int i = 0; i < 7; ++i) {
            auto* l = names->add<Label>(wk[(i + (monday ? 1 : 0)) % 7]);
            l->setSize(12)->setWeight(600)->setColor("on_surface_variant", false)->setCentered(true);
            l->fixedWidth = 48;
        }
        const int lead = (first.tm_wday - (monday ? 1 : 0) + 7) % 7;
        std::tm last = first;
        last.tm_mon += 1, last.tm_mday = 0;
        std::mktime(&last);
        const int days = last.tm_mday;
        const auto busy = cal.busyDays(st->year, st->month);
        const auto hol = cal.holidays(st->year, st->month);
        const std::time_t nowT = std::time(nullptr);
        const std::tm today = *std::localtime(&nowT);
        Flex* row = nullptr;
        for (int cell = 0; cell < lead + days; ++cell) {
            if (cell % 7 == 0) {
                row = left->add<Flex>(Flex::Direction::Row);
            }
            if (cell < lead) {
                row->add<Node>()->fixedWidth = 48;
                continue;
            }
            const int d = cell - lead + 1;
            const bool isToday = today.tm_year + 1900 == st->year && today.tm_mon + 1 == st->month && today.tm_mday == d;
            const bool picked = d == st->day;
            auto* b = row->add<Button>(Flex::Direction::Column);
            b->fixedWidth = b->fixedHeight = 48;
            b->setRadius(Radii(24))->setAlignItems(Align::Center)->setJustify(Align::Center)->setSpacing(2);
            if (picked) b->setBackground("primary", false);
            else if (isToday) b->setBorder(1.5f, ColorRef("primary"));
            b->add<Label>(std::to_string(d))->setSize(14)->setWeight(isToday || picked ? 600 : 400)->setColor(picked ? "on_primary" : hol.contains(d) ? "error" : "on_surface", false);
            auto* dot = b->add<Flex>(Flex::Direction::Row);
            dot->fixedWidth = dot->fixedHeight = 5;
            dot->setRadius(Radii(2.5f))->setBackground(busy.contains(d) ? (picked ? ColorRef("on_primary") : ColorRef("tertiary")) : ColorRef::none(), false);
            b->onClicked = [st, d](uint32_t btn) {
                if (btn != BTN_LEFT) return;
                st->day = d;
                EventLoop::get().defer([st] {
                    if (*st->alive && st->show) st->show();
                });
            };
        }
        pillButton(left, "New event", true, [st] {
            if (open) open(std::format("event new:{:04}-{:02}-{:02}", st->year, st->month, st->day));
        }, "add");
        caption(left, "Holidays in red · Settings → Calendar subscribes to more");

        // The day, and what's next
        auto list = [&](Flex* into, std::time_t from, std::time_t to, bool withDay) {
            const auto occ = cal.between(from, to);
            if (occ.empty()) return false;
            for (const auto& o : occ) {
                auto* b = into->add<Button>(Flex::Direction::Row);
                b->setPadding({14, 10})->setSpacing(14)->setRadius(Radii(18))->setAlignItems(Align::Center)->setBackground("surface_container_high", false);
                const bool hol = Calendar::isHoliday(*o.event);
                auto* bar = b->add<Flex>(Flex::Direction::Row);
                bar->fixedWidth = 4;
                bar->fixedHeight = 36;
                bar->setRadius(Radii(2))->setBackground(hol ? "error" : o.event->source.empty() ? "primary" : "tertiary", false);
                auto* t = b->add<Flex>(Flex::Direction::Column);
                t->setAlignItems(Align::Start);
                t->grow = true;
                t->add<Label>(o.event->title.empty() ? "(no title)" : o.event->title)->setSize(14)->setWeight(500)->setMaxWidth(420);
                char when[64];
                const std::tm s = *std::localtime(&o.start), e = *std::localtime(&o.end);
                if (o.event->allDay) std::strftime(when, sizeof when, withDay ? "%a %d %b · all day" : "All day", &s);
                else {
                    char a[32], z[16];
                    std::strftime(a, sizeof a, withDay ? "%a %d %b, %H:%M" : "%H:%M", &s);
                    std::strftime(z, sizeof z, "%H:%M", &e);
                    std::snprintf(when, sizeof when, "%s–%s", a, z);
                }
                std::string sub = when;
                if (!o.event->location.empty()) sub += "  ·  " + o.event->location;
                t->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420);
                const std::string id = o.event->id;
                const bool mine = o.event->source.empty();
                b->onClicked = [id, mine](uint32_t btn) {
                    if (btn == BTN_LEFT && mine && open) open("event " + id);
                };
            }
            return true;
        };
        char dt[48];
        std::tm picked{};
        picked.tm_year = st->year - 1900, picked.tm_mon = st->month - 1, picked.tm_mday = st->day, picked.tm_isdst = -1;
        std::mktime(&picked);
        std::strftime(dt, sizeof dt, "%A, %d %B", &picked);
        right->add<Label>(dt)->setSize(22)->setWeight(500);
        const std::time_t s0 = dayStart(st->year, st->month, st->day);
        if (!list(right, s0, s0 + 86400, false)) caption(right, "Nothing on this day");
        right->add<Node>()->fixedHeight = 8;
        right->add<Label>("The next two weeks")->setSize(16)->setWeight(600)->setColor("on_surface_variant", false);
        const std::time_t t0 = dayStart(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
        if (!list(right, t0, t0 + 14 * 86400, true)) caption(right, "Nothing coming up");
    };
    keeper->conn = Calendar::get().changed.connect([st] {
        if (*st->alive && st->show) st->show();
    });
    st->show();
}

void clockApp(Flex* card, Popup& popup, const std::string& arg) {
    auto* c = centred(card, 520);
    panels::clock(c, popup, arg);
}

} // namespace m3::shell::panels
