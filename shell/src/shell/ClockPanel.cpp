// Alarms and timers, as Android's Clock: the popup with a tab each, its alarm
// editor (the M3 time picker's input boxes, the days, a label), and
// Settings → Alarms & timers
#include "shell/ClockPanel.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "render/Renderer.h"
#include "services/Alarms.h"
#include "shell/ClockFaces.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

bool twelveHour() {
    return Alarms::timeText(13, 0).ends_with("PM");
}

// "In 3 h" → "in 3 h", to follow on from other words
std::string lowerFirst(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}

// Hours or minutes, as the M3 time picker's input boxes: a big number set by
// typing two digits, the arrows or the wheel; the one taking keys lit in
// the primary container
class TimeBox : public Button {
public:
    TimeBox(int value, int lo, int hi) : Button(Direction::Row), m_value(value), m_lo(lo), m_hi(hi) {
        setJustify(Align::Center)->setRadius(Radii(12));
        fixedWidth = 96;
        fixedHeight = 80;
        pressedRadius = 20;
        m_label = add<Label>();
        m_label->setSize(48);
        onScrolled = [this](int d) { step(-d); }; // the wheel up: more
        paint(false);
    }
    std::function<void(int)> onChanged;
    void step(int by) {
        const int span = m_hi - m_lo + 1;
        m_typed = -1;
        set(m_lo + ((m_value - m_lo + by) % span + span) % span);
    }
    bool focusable() const override { return true; }
    void onFocus(bool focused) override {
        m_focused = focused;
        m_typed = -1;
        paint(true);
    }
    bool onKey(const KeyEvent& e) override {
        if (e.sym == XKB_KEY_Up || e.sym == XKB_KEY_Down) {
            step(e.sym == XKB_KEY_Up ? 1 : -1);
            return true;
        }
        if (e.text.size() != 1 || e.text[0] < '0' || e.text[0] > '9') return false;
        // Two digits make the number; one that can't follow starts afresh
        const int d = e.text[0] - '0', two = m_typed * 10 + d;
        if (m_typed >= 0 && two >= m_lo && two <= m_hi) {
            m_typed = -1;
            set(two);
        } else {
            m_typed = d;
            if (d >= m_lo && d <= m_hi) set(d);
        }
        return true;
    }

private:
    void set(int v) {
        if (v == m_value) return;
        m_value = v;
        paint(false);
        if (onChanged) onChanged(v);
    }
    void paint(bool animate) {
        m_label->setText(std::format("{:02}", m_value));
        setBackground(m_focused ? ColorRef("primary_container") : ColorRef("surface_container_highest"), animate);
        m_label->setColor(m_focused ? "on_primary_container" : "on_surface", animate);
    }
    Label* m_label;
    int m_value, m_lo, m_hi, m_typed = -1;
    bool m_focused = false;
};

// ── Alarms ──

void alarmEditor(Flex* body, Popup& popup, const std::string& id, bool focus, const std::function<void()>& done) {
    auto& al = Alarms::get();
    auto draft = std::make_shared<Alarms::Alarm>();
    const bool existing = al.alarm(id) != nullptr;
    if (existing) {
        *draft = *al.alarm(id);
    } else {
        // A new one at the next whole hour
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        draft->hour = (tm.tm_hour + 1) % 24;
        draft->minute = 0;
    }
    draft->enabled = true;

    auto* head = body->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8);
    iconButton(head, "arrow_back", done);
    auto* title = head->add<Label>(existing ? "Edit alarm" : "New alarm");
    title->setSize(20)->setWeight(500);
    title->grow = true;

    // The time, and when that is from now
    const bool twelve = twelveHour();
    auto* clock = body->add<Flex>(Flex::Direction::Row);
    clock->setJustify(Align::Center)->setSpacing(8)->setPadding({0, 8});
    auto until = std::make_shared<Label*>(nullptr); // the line under the time, made below it
    auto refresh = [draft, until] {
        if (*until) (*until)->setText(Alarms::daysText(*draft) + " · " + Alarms::untilText(Alarms::next(*draft, std::time(nullptr))));
    };
    // A box with its arrows above and below
    auto box = [clock](int value, int lo, int hi) {
        auto* col = clock->add<Flex>(Flex::Direction::Column);
        col->setSpacing(2);
        auto* up = iconButton(col, "keyboard_arrow_up", {}, 24);
        auto* b = col->add<TimeBox>(value, lo, hi);
        auto* down = iconButton(col, "keyboard_arrow_down", {}, 24);
        up->onClicked = [b](uint32_t) { b->step(1); };
        down->onClicked = [b](uint32_t) { b->step(-1); };
        return b;
    };
    auto* hours = box(twelve ? (draft->hour % 12 == 0 ? 12 : draft->hour % 12) : draft->hour, twelve ? 1 : 0, twelve ? 12 : 23);
    hours->onChanged = [draft, twelve, refresh](int v) {
        draft->hour = twelve ? v % 12 + (draft->hour >= 12 ? 12 : 0) : v;
        refresh();
    };
    clock->add<Label>(":")->setSize(48)->setColor("on_surface_variant", false);
    auto* minutes = box(draft->minute, 0, 59);
    minutes->onChanged = [draft, refresh](int v) {
        draft->minute = v;
        refresh();
    };
    if (twelve) {
        // AM over PM, a connected pair standing up
        auto* ampm = clock->add<Flex>(Flex::Direction::Column);
        ampm->setSpacing(2);
        auto paint = std::make_shared<std::function<void()>>();
        Button* half[2];
        for (int i = 0; i < 2; ++i) {
            auto* b = half[i] = ampm->add<Button>();
            b->setPadding({14, 0})->setJustify(Align::Center);
            b->fixedWidth = 56;
            b->fixedHeight = 39;
            b->add<Label>(i == 0 ? "AM" : "PM")->setSize(14)->setWeight(500);
            b->onClicked = [draft, i, paint, refresh](uint32_t) {
                draft->hour = draft->hour % 12 + (i == 1 ? 12 : 0);
                (*paint)();
                refresh();
            };
        }
        *paint = [draft, a = half[0], p = half[1]] {
            const bool pm = draft->hour >= 12;
            for (auto [b, on, top] : {std::tuple{a, !pm, true}, std::tuple{p, pm, false}}) {
                b->setRadius(top ? Radii{12, 12, 4, 4} : Radii{4, 4, 12, 12});
                b->setBackground(on ? ColorRef("tertiary_container") : ColorRef("surface_container_highest"));
                static_cast<Label*>(b->children()[0].get())->setColor(on ? "on_tertiary_container" : "on_surface_variant");
            }
        };
        (*paint)();
    }
    *until = body->add<Label>("");
    (*until)->setSize(13)->setColor("on_surface_variant", false);
    (*until)->alignSelf = Align::Center;
    refresh();
    if (focus) popup.initialFocus = hours;

    // The days it repeats on, from Monday; none: just once
    auto* daysHead = body->add<Label>("Repeat");
    daysHead->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    daysHead->alignSelf = Align::Start;
    auto* days = body->add<Flex>(Flex::Direction::Row);
    days->setSpacing(6)->setJustify(Align::Center);
    static const char* letters[] = {"S", "M", "T", "W", "T", "F", "S"};
    for (int d : {1, 2, 3, 4, 5, 6, 0}) {
        auto* chip = days->add<Button>();
        pill(chip)->setJustify(Align::Center);
        chip->fixedWidth = chip->fixedHeight = 44;
        auto* letter = chip->add<Label>(letters[d]);
        letter->setSize(15)->setWeight(600);
        auto paint = [draft, chip, letter, d](bool animate) {
            const bool on = std::ranges::find(draft->days, d) != draft->days.end();
            chip->setBackground(on ? ColorRef("primary") : ColorRef("surface_container_highest"), animate);
            letter->setColor(on ? "on_primary" : "on_surface", animate);
        };
        paint(false);
        chip->onClicked = [draft, d, paint, refresh](uint32_t) {
            if (std::erase(draft->days, d) == 0) draft->days.push_back(d);
            std::ranges::sort(draft->days);
            paint(true);
            refresh();
        };
    }

    auto* label = body->add<TextField>();
    label->setPlaceholder("Label");
    label->setText(draft->label);

    auto* foot = body->add<Flex>(Flex::Direction::Row);
    foot->setSpacing(8);
    if (existing)
        pillButton(foot, "Delete", false, [id, done] {
            Alarms::get().remove(id);
            done();
        }, "delete");
    foot->add<Node>()->grow = true;
    pillButton(foot, "Cancel", false, done);
    pillButton(foot, "Save", true, [draft, label, done] {
        draft->label = label->text();
        Alarms::get().save(*draft);
        done();
    }, "check");
}

void alarmList(Flex* body, const std::function<void(const std::string&)>& edit) {
    auto* live = body->add<Live>();
    live->setSpacing(8);
    live->watch(Alarms::get().changed);
    live->sync = [live, edit] {
        live->clearChildren();
        auto& al = Alarms::get();
        if (al.alarms().empty()) {
            auto* none = live->add<Flex>(Flex::Direction::Column);
            none->setSpacing(8)->setPadding({0, 24})->setAlignItems(Align::Center);
            none->add<Icon>("alarm_off", 40)->setColor("on_surface_variant", false);
            none->add<Label>("No alarms")->setSize(15)->setWeight(500)->setColor("on_surface_variant", false);
        }
        for (const auto& a : al.alarms()) {
            auto* card = live->add<Button>(Flex::Direction::Row);
            card->setPadding({20, 12})->setSpacing(12)->setRadius(Radii(24));
            card->setBackground(ColorRef("surface_container_highest", 0.7f), false);
            card->pressedRadius = 16;
            auto* texts = card->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(0);
            texts->grow = true;
            texts->add<Label>(Alarms::timeText(a.hour, a.minute))->setSize(36)->setColor(a.enabled ? "on_surface" : "on_surface_variant", false);
            std::string about = a.enabled || !a.days.empty() ? Alarms::daysText(a) : "Not scheduled";
            if (!a.label.empty()) about += " · " + a.label;
            if (a.snoozed) {
                std::tm tm{};
                localtime_r(&a.snoozed, &tm);
                about = "Snoozed until " + Alarms::timeText(tm.tm_hour, tm.tm_min);
            }
            texts->add<Label>(about)->setSize(13)->setMaxWidth(260)->setColor(a.snoozed ? "primary" : "on_surface_variant", false);
            auto* sw = card->add<Switch>();
            sw->setChecked(a.enabled, false);
            const std::string id = a.id;
            sw->onToggled = [id](bool on) { Alarms::get().setEnabled(id, on); };
            card->onClicked = [edit, id](uint32_t b) {
                if (b == BTN_LEFT) edit(id);
            };
            card->tooltip = "Edit";
        }
        auto* foot = live->add<Flex>(Flex::Direction::Row);
        foot->setSpacing(8)->setPadding({4, 4, 0, 0});
        std::time_t at = 0;
        const auto* next = al.upcoming(&at);
        auto* when = foot->add<Label>(next ? "Next alarm " + lowerFirst(Alarms::untilText(at)) : "");
        when->setSize(13)->setColor("on_surface_variant", false);
        when->grow = true;
        pillButton(foot, "Add alarm", true, [edit] { edit(""); }, "add");
    };
    live->sync();
}

// ── Timers ──

// Running timers count down in place, a second at a time, without a rebuild
class TimerList : public Live {
public:
    TimerList() {
        setSpacing(8);
        watch(Alarms::get().changed);
        sync = [this] { build(); };
        build();
    }

private:
    struct Shown {
        std::string id;
        TimerRing* ring;
        Label* left;
    };
    void build() {
        clearChildren();
        m_shown.clear();
        auto& al = Alarms::get();
        const int64_t now = Alarms::nowMs();
        for (const auto& t : al.timers()) {
            const bool ringing = std::ranges::any_of(al.ringing(), [&](const auto& r) { return r.id == t.id; });
            auto* card = add<Flex>(Flex::Direction::Row);
            card->setPadding({12, 10})->setSpacing(14)->setRadius(Radii(24));
            card->setBackground(ColorRef("surface_container_highest", 0.7f), false);
            auto* ring = card->add<TimerRing>(76);
            auto* left = ring->add<Label>(Alarms::countdown(t.remaining(now)));
            left->setSize(t.remaining(now) >= 3600000 ? 14 : 18)->setWeight(500);
            m_shown.push_back({t.id, ring, left});

            auto* texts = card->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(4);
            texts->grow = true;
            texts->add<Label>(t.label.empty() ? "Timer" : t.label)->setSize(15)->setWeight(600)->setMaxWidth(170);
            texts->add<Label>(ringing ? "Time's up" : std::format("{} · {}", Alarms::countdown(t.length), t.running() ? "Running" : "Paused"))
                ->setSize(12)
                ->setColor(ringing ? "error" : "on_surface_variant", false);
            auto* tools = texts->add<Flex>(Flex::Direction::Row);
            tools->setSpacing(4);
            const std::string id = t.id;
            auto* more = tools->add<Button>();
            pill(more->setBackground("secondary_container", false))->setPadding({12, 6});
            more->add<Label>("+1:00")->setSize(13)->setWeight(600)->setColor("on_secondary_container", false);
            more->onClicked = [id](uint32_t) { Alarms::get().addTime(id, 60000); };
            more->tooltip = "A minute more";
            iconButton(tools, "replay", [id] { Alarms::get().reset(id); }, 18)->tooltip = "Reset";
            iconButton(tools, "delete", [id] { Alarms::get().removeTimer(id); }, 18)->tooltip = "Delete";

            // Start and pause; Stop while it rings
            auto* go = card->add<Button>();
            pill(go->setBackground(ringing ? "error" : "primary", false))->setJustify(Align::Center);
            go->fixedWidth = go->fixedHeight = 56;
            go->add<Icon>(ringing ? "stop" : t.running() ? "pause" : "play_arrow", 28)->setFill(true)->setColor(ringing ? "on_error" : "on_primary", false);
            go->tooltip = ringing ? "Stop" : t.running() ? "Pause" : "Start";
            go->onClicked = [id, ringing, running = t.running()](uint32_t) {
                if (ringing) Alarms::get().stop(id);
                else if (running) Alarms::get().pause(id);
                else Alarms::get().resume(id);
            };
        }
        tick();
        if (al.anyRunning() && !m_ticker) m_ticker = std::make_unique<SecondTicker>([this] { tick(); });
        else if (!al.anyRunning()) m_ticker.reset();
    }
    void tick() {
        const int64_t now = Alarms::nowMs();
        for (const auto& s : m_shown) {
            const auto& ts = Alarms::get().timers();
            const auto it = std::ranges::find(ts, s.id, &Alarms::Timer::id);
            if (it == ts.end()) continue;
            const int64_t left = it->remaining(now);
            s.left->setText(Alarms::countdown(left));
            s.ring->set(it->length > 0 ? static_cast<float>(static_cast<double>(left) / static_cast<double>(it->length)) : 0, it->running());
        }
    }
    std::vector<Shown> m_shown;
    std::unique_ptr<SecondTicker> m_ticker;
};

void timers(Flex* body) {
    body->add<TimerList>();

    auto* add = body->add<Flex>(Flex::Direction::Column);
    add->setSpacing(10)->setPadding({14, 12})->setRadius(Radii(24))->setAlignItems(Align::Stretch);
    add->setBackground(ColorRef("surface_container_highest", 0.4f), false);
    auto* head = add->add<Label>("New timer");
    head->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    head->alignSelf = Align::Start;
    auto* presets = add->add<Flow>();
    for (int minutes : {1, 5, 10, 15, 30, 60}) {
        auto* b = presets->add<Button>();
        pill(b->setBackground("secondary_container", false))->setPadding({12, 8});
        b->add<Label>(minutes == 60 ? std::string("1 h") : std::format("{} min", minutes))
            ->setSize(13)
            ->setWeight(600)
            ->setColor("on_secondary_container", false);
        b->onClicked = [minutes](uint32_t) { Alarms::get().startTimer(minutes * 60000LL); };
    }
    auto* custom = add->add<Flex>(Flex::Direction::Row);
    custom->setSpacing(8);
    auto* length = custom->add<TextField>();
    length->setPlaceholder("25, 1:30, 1h 20m");
    length->grow = true;
    auto* name = custom->add<TextField>();
    name->setPlaceholder("Label");
    name->fixedWidth = 110;
    auto* problem = add->add<Label>("A length like 25 (minutes), 1:30 or 1h 20m");
    problem->setSize(12)->setColor("error", false);
    problem->setVisible(false);
    auto start = [length, name, problem] {
        const int64_t ms = Alarms::parseLength(length->text());
        problem->setVisible(ms <= 0);
        if (ms <= 0) return;
        Alarms::get().startTimer(ms, name->text());
        length->setText("");
        name->setText("");
    };
    length->onSubmit = [start](const std::string&) { start(); };
    name->onSubmit = [start](const std::string&) { start(); };
    pillButton(custom, "Start", true, start, "play_arrow");
}

} // namespace

void clock(Flex* card, Popup& popup, const std::string& arg) {
    auto& al = Alarms::get();
    header(card, "alarm", "Clock");
    // The tab asked for; else Timers while one runs or rings, else the last
    int start = Config::get().value<int>("/clock/tab", 0);
    if (arg == "alarms" || arg == "new" || arg.starts_with("edit:")) start = 0;
    else if (arg == "timers" || al.anyRunning() || std::ranges::any_of(al.ringing(), [](const auto& r) { return r.timer; })) start = 1;
    auto tab = std::make_shared<int>(start);

    auto* tabs = card->add<Flex>(Flex::Direction::Row);
    tabs->setJustify(Align::Center);
    auto* holder = card->add<Flex>(Flex::Direction::Column);
    holder->setSpacing(12)->setAlignItems(Align::Stretch);
    // "": the tab; else an alarm's editor ("new" for a new one)
    auto fill = std::make_shared<std::function<void(const std::string&, bool)>>();
    *fill = [holder, tabs, tab, fill, &popup](const std::string& editing, bool focus) {
        holder->clearChildren();
        tabs->setVisible(editing.empty());
        auto again = [fill](const std::string& what) {
            // Not inside the click: the rebuild replaces what was clicked
            EventLoop::get().defer([fill, what] { (*fill)(what, false); });
        };
        if (!editing.empty()) alarmEditor(holder, popup, editing == "new" ? "" : editing, focus, [again] { again(""); });
        else if (*tab == 0) alarmList(holder, [again](const std::string& id) { again(id.empty() ? "new" : id); });
        else timers(holder);
        holder->requestLayout();
    };
    connectedGroup<int>(tabs, *tab, {{0, "Alarms"}, {1, "Timers"}}, [tab, fill](int v) {
        *tab = v;
        Config::get().set("/clock/tab", v);
        EventLoop::get().defer([fill] { (*fill)("", false); });
    });
    (*fill)(arg == "new" ? "new" : arg.starts_with("edit:") ? arg.substr(5) : "", true);
}

void clockPage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->watch(Alarms::get().changed);
    live->sync = [live] {
        live->clearChildren();
        auto& al = Alarms::get();
        std::time_t at = 0;
        const auto* next = al.upcoming(&at);
        auto* g = group(live, "Alarms", next ? "The next one rings " + lowerFirst(Alarms::untilText(at)) : "None on");
        for (const auto& a : al.alarms()) {
            std::string about = a.enabled || !a.days.empty() ? Alarms::daysText(a) : "Not scheduled";
            if (!a.label.empty()) about += " · " + a.label;
            auto r = row(g, a.enabled ? "alarm_on" : "alarm_off", Alarms::timeText(a.hour, a.minute), about);
            const std::string id = a.id;
            iconButton(r.row, "edit", [id] {
                if (open) open("clock edit:" + id);
            })->tooltip = "Edit";
            auto* sw = r.row->add<Switch>();
            sw->setChecked(a.enabled, false);
            sw->onToggled = [id](bool on) { Alarms::get().setEnabled(id, on); };
        }
        auto add = row(g, "add_alarm", al.alarms().empty() ? "No alarms yet" : "Another alarm", "At a time, once or on the days you pick");
        pillButton(add.row, "Add alarm", true, [] {
            if (open) open("clock new");
        }, "add");

        auto* t = group(live, "Timers", "Started from the clock: the bar's widget, or the desktop's");
        size_t running = 0;
        for (const auto& x : al.timers()) running += x.running();
        auto tr = row(t, "timer", "Timers",
                      al.timers().empty() ? "None" : std::format("{} running, {} in all", running, al.timers().size()));
        pillButton(tr.row, "Open", false, [] {
            if (open) open("clock timers");
        }, "open_in_new");
    };
    live->sync();

    // How they ring: in a Live of its own, rebuilt as a choice is made
    auto* how = page->add<Live>();
    how->setSpacing(0);
    how->sync = [how] {
        how->clearChildren();
        auto& c = Config::get();
        auto* g = group(how, "When one goes off", "It rings, even with Do not disturb on, until you stop or snooze it");
        auto* box = g->add<Flex>(Flex::Direction::Column);
        box->setAlignItems(Align::Stretch);
        const std::string chosen = c.value<std::string>("/clock/sound", "");
        std::string shown = Alarms::sounds().empty() ? "None found" : Alarms::sounds().front().second;
        for (const auto& [file, name] : Alarms::sounds())
            if (file == chosen) shown = name;
        auto sound = row(box, "music_note", "Sound", "Played over and over while it rings");
        iconButton(sound.row, "play_arrow", [] { Alarms::get().preview(true); })->tooltip = "Play it";
        dropdown(sound.row, box, shown, Alarms::sounds(), [how](const std::string& file) {
            Config::get().set("/clock/sound", file);
            Alarms::get().preview(true);
            how->syncSoon();
        });
        auto rise = row(g, "volume_up", "Gradually louder", "Starts quiet and grows over the first half minute");
        auto* sw = rise.row->add<Switch>();
        sw->setChecked(c.value<bool>("/clock/rise", true), false);
        sw->onToggled = [](bool on) { Config::get().set("/clock/rise", on); };
        auto snooze = row(g, "snooze", "Snooze for");
        connectedGroup<int>(snooze.row, c.value<int>("/clock/snooze", 10), {{5, "5 min"}, {10, "10 min"}, {15, "15 min"}, {20, "20 min"}},
                            [](int v) { Config::get().set("/clock/snooze", v); });
        auto quiet = row(g, "notifications_paused", "Silence after", "Nobody about: it stops, and says it was missed");
        connectedGroup<int>(quiet.row, c.value<int>("/clock/silenceAfter", 10), {{5, "5 min"}, {10, "10 min"}, {20, "20 min"}, {0, "Never"}},
                            [](int v) { Config::get().set("/clock/silenceAfter", v); });
    };
    how->sync();
}

} // namespace m3::shell::panels
