// A calendar event: made, changed or deleted (your own), or just shown
// (from a subscribed calendar)
#include "core/Config.h"
#include "core/EventLoop.h"
#include "services/Calendar.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <cstdio>
#include <ctime>
#include <format>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::time_t dayStart(int y, int m, int d) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

// "9:30", "09:30", "930" → minutes into the day; -1 if it isn't a time
int parseTime(const std::string& text) {
    int h = -1, m = 0;
    if (std::sscanf(text.c_str(), "%d:%d", &h, &m) < 1) return -1;
    if (text.find(':') == std::string::npos && h >= 100) {
        m = h % 100;
        h /= 100;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

std::string clock(std::time_t t) {
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M", &tm);
    return buf;
}

// A labelled line in the form
Flex* field(Node* card, const char* icon, const char* label) {
    auto* r = card->add<Flex>(Flex::Direction::Row);
    r->setSpacing(12)->setAlignItems(Align::Center)->setPadding({4, 2});
    r->add<Icon>(icon, 20)->setColor("on_surface_variant", false);
    auto* l = r->add<Label>(label);
    l->setSize(14)->setWeight(500);
    l->fixedWidth = 96;
    return r;
}

} // namespace

void eventEditor(Flex* card, Popup& popup, const std::string& arg) {
    auto& cal = m3::Calendar::get();
    // "new:YYYY-MM-DD", or an event's id
    m3::Calendar::Event e;
    bool editable = true, existing = false;
    if (arg.starts_with("new:")) {
        int y = 0, m = 0, d = 0;
        std::sscanf(arg.c_str() + 4, "%d-%d-%d", &y, &m, &d);
        // The next whole hour on that day, an hour long
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        e.start = dayStart(y, m, d) + (std::min(tm.tm_hour + 1, 23)) * 3600;
        e.end = e.start + 3600;
        e.remind = Config::get().value<int>("/calendar/remind", 10);
    } else if (const auto* found = cal.find(arg)) {
        e = *found;
        existing = true;
        editable = e.source.empty();
    } else {
        caption(card, "That event is gone");
        return;
    }
    auto ev = std::make_shared<m3::Calendar::Event>(e);

    auto* head = card->add<Flex>(Flex::Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Center);
    head->add<Icon>("event", 26)->setColor("primary", false);
    auto* title = head->add<Label>(!existing ? "New event" : editable ? "Event" : "Event (subscribed)");
    title->setSize(22)->setWeight(600);
    title->grow = true;
    iconButton(head, "close", [&popup] { popup.close(); });

    if (!editable) {
        // Subscribed calendars are read here, changed where they come from
        auto* box = card->add<Flex>(Flex::Direction::Column);
        box->setSpacing(6)->setAlignItems(Align::Start)->setPadding({4, 0});
        box->add<Label>(e.title.empty() ? "(No title)" : e.title)->setSize(20)->setWeight(600)->setMaxWidth(440)->setMaxLines(3);
        std::tm tm{};
        localtime_r(&e.start, &tm);
        char date[64];
        std::strftime(date, sizeof date, "%A %-d %B %Y", &tm);
        box->add<Label>(std::string(date) + (e.allDay ? " · All day" : " · " + clock(e.start) + " – " + clock(e.end)))
            ->setSize(14)
            ->setColor("on_surface_variant", false);
        if (!e.location.empty()) box->add<Label>(e.location)->setSize(14)->setMaxWidth(440);
        if (!e.notes.empty()) box->add<Label>(e.notes)->setSize(13)->setMaxWidth(440)->setMaxLines(10)->setColor("on_surface_variant", false);
        for (const auto& s : cal.sources())
            if (s.id == e.source) caption(card, "From " + s.name + ": change it there");
        return;
    }

    // The form
    auto* name = card->add<TextField>();
    name->setPlaceholder("Title");
    name->setText(e.title);
    popup.initialFocus = name;

    auto allDay = field(card, "schedule", "All day");
    allDay->add<Node>()->grow = true;
    auto* allSw = allDay->add<Switch>();
    allSw->setChecked(e.allDay, false);

    // The day: stepped back and forth
    auto day = std::make_shared<std::time_t>(dayStart(1900, 1, 1));
    {
        std::tm tm{};
        localtime_r(&e.start, &tm);
        *day = dayStart(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }
    auto dayRow = field(card, "calendar_today", "Date");
    auto* dayLabel = dayRow->add<Label>();
    dayLabel->setSize(14)->setWeight(500);
    dayLabel->grow = true;
    auto showDay = [day, dayLabel] {
        std::tm tm{};
        localtime_r(day.get(), &tm);
        char buf[64];
        std::strftime(buf, sizeof buf, "%a %-d %B %Y", &tm);
        dayLabel->setText(buf);
    };
    showDay();
    auto stepDay = [day, showDay](int by) {
        std::tm tm{};
        localtime_r(day.get(), &tm);
        tm.tm_mday += by;
        tm.tm_isdst = -1;
        *day = std::mktime(&tm);
        showDay();
    };
    iconButton(dayRow, "chevron_left", [stepDay] { stepDay(-1); });
    iconButton(dayRow, "chevron_right", [stepDay] { stepDay(1); });

    auto times = field(card, "access_time", "Time");
    auto* from = times->add<TextField>();
    from->fixedWidth = 110;
    from->setPlaceholder("10:00");
    from->setText(clock(e.start));
    times->add<Label>("to")->setSize(14)->setColor("on_surface_variant", false);
    auto* to = times->add<TextField>();
    to->fixedWidth = 110;
    to->setPlaceholder("11:00");
    to->setText(clock(e.end));
    times->setVisible(!e.allDay);
    allSw->onToggled = [times](bool on) { times->setVisible(!on); };

    auto place = field(card, "location_on", "Place");
    auto* where = place->add<TextField>();
    where->setPlaceholder("Optional");
    where->setText(e.location);
    where->grow = true;

    auto repeat = field(card, "repeat", "Repeat");
    repeat->add<Node>()->grow = true;
    connectedGroup<std::string>(repeat, e.repeat,
                                {{"", "Never"}, {"daily", "Daily"}, {"weekly", "Weekly"}, {"monthly", "Monthly"}, {"yearly", "Yearly"}},
                                [ev](std::string v) {
                                    ev->repeat = v;
                                    ev->byDay.clear(); // its own weekday, from the date
                                });
    auto remind = field(card, "notifications", "Reminder");
    remind->add<Node>()->grow = true;
    connectedGroup<int>(remind, e.remind, {{-1, "None"}, {0, "At start"}, {10, "10 min"}, {30, "30 min"}, {60, "1 h"}},
                        [ev](int v) { ev->remind = v; });

    auto* notes = card->add<TextField>();
    notes->setPlaceholder("Notes");
    notes->setText(e.notes);

    auto* problem = card->add<Label>("");
    problem->setSize(13)->setColor("error", false);
    problem->setVisible(false);

    auto* foot = card->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(8)->setAlignItems(Align::Center);
    if (existing) {
        const std::string id = e.id;
        pillButton(foot, "Delete", false, [id, &popup] {
            m3::Calendar::get().remove(id);
            popup.close();
        }, "delete");
    }
    foot->add<Node>()->grow = true;
    pillButton(foot, "Cancel", false, [&popup] { popup.close(); });
    pillButton(foot, "Save", true, [ev, name, allSw, day, from, to, where, notes, problem, &popup] {
        auto out = *ev;
        out.title = name->text();
        out.location = where->text();
        out.notes = notes->text();
        out.allDay = allSw->checked();
        if (out.allDay) {
            out.start = *day;
            std::tm tm{};
            localtime_r(day.get(), &tm);
            tm.tm_mday += 1;
            tm.tm_isdst = -1;
            out.end = std::mktime(&tm);
        } else {
            const int a = parseTime(from->text()), b = parseTime(to->text());
            if (a < 0 || b < 0) {
                problem->setText("Times look like 09:30 or 14:00");
                problem->setVisible(true);
                return;
            }
            out.start = *day + a * 60;
            // Ending before it starts: it ends the next day
            out.end = *day + b * 60 + (b < a ? 86400 : 0);
        }
        m3::Calendar::get().save(out);
        popup.close();
    }, "check");
}

} // namespace m3::shell::panels
