// Settings → Digital Wellbeing, as Android's: today's screen time as a ring
// of apps with the total in it, unlocks and notifications, the week's bars,
// each app's time with its timer; Focus mode and Bedtime
#include "shell/WellbeingPage.h"

#include "core/Config.h"
#include "render/Images.h"
#include "services/Notifications.h"
#include "services/Wellbeing.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <memory>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::string duration(int seconds) {
    if (seconds < 60) return seconds > 0 ? "< 1 min" : "0 min";
    const int h = seconds / 3600, m = seconds / 60 % 60;
    if (h == 0) return std::format("{} min", m);
    return m ? std::format("{} h {} min", h, m) : std::format("{} h", h);
}

// The apps of a day, most used first
std::vector<std::pair<std::string, int>> ranked(const Wellbeing::Day& d) {
    std::vector<std::pair<std::string, int>> out(d.seconds.begin(), d.seconds.end());
    std::erase_if(out, [](const auto& kv) { return kv.second < 30 || kv.first.empty(); });
    std::ranges::sort(out, std::greater{}, &std::pair<std::string, int>::second);
    return out;
}

// The week: a bar a day, today's in the primary colour, the day under it
class WeekBars : public Node {
public:
    void set(std::vector<int> days, std::vector<std::string> labels) {
        m_days = std::move(days);
        m_labels = std::move(labels);
        requestRedraw();
    }

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 150}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        int most = 1;
        for (int d : m_days) most = std::max(most, d);
        const float n = static_cast<float>(std::max<size_t>(1, m_days.size()));
        const float slot = b.w / n, bar = std::min(36.0f, slot * 0.55f), chart = b.h - 26;
        const auto& t = Theme::get();
        TextStyle st;
        st.family = t.fontFamily();
        st.size = 12;
        for (size_t i = 0; i < m_days.size(); ++i) {
            const bool today = i + 1 == m_days.size();
            const float h = std::max(bar, chart * static_cast<float>(m_days[i]) / static_cast<float>(most));
            const float x = b.x + slot * static_cast<float>(i) + (slot - bar) / 2;
            r.rect({x, b.y + chart - h, bar, h}, t.color(today ? "primary" : m_days[i] ? "secondary_container" : "surface_container_highest"),
                   Radii(bar / 2));
            if (i < m_labels.size()) {
                const auto& tex = TextEngine::get().texture(m_labels[i], st, r.scale());
                r.mask(tex.id, {std::round(x + bar / 2 - tex.logical.width / 2), b.y + chart + 8, tex.logical.width, tex.logical.height},
                       t.color(today ? "primary" : "on_surface_variant"));
            }
        }
    }

private:
    std::vector<int> m_days;
    std::vector<std::string> m_labels;
};

// An app's icon, for rows
std::unique_ptr<ImageView> appIcon(const std::string& cls, float size) {
    const std::string icon = Wellbeing::iconOf(cls);
    return std::make_unique<ImageView>(size, [icon, size](float scale, int& w, int& h) -> GLuint {
        auto* t = Images::get().icon(icon, static_cast<int>(std::lround(size * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    });
}

// A number with its name under it (unlocks, notifications)
void stat(Flex* parent, const std::string& icon, Label*& value, const std::string& name) {
    auto* box = parent->add<Flex>(Flex::Direction::Column);
    box->setPadding({16, 14})->setSpacing(4)->setAlignItems(Align::Start)->setRadius(Radii(24));
    box->setBackground("surface_container_highest", false);
    box->grow = true;
    box->add<Icon>(icon, 22)->setColor("primary", false);
    value = box->add<Label>("0");
    value->setSize(24)->setWeight(600);
    box->add<Label>(name)->setSize(13)->setColor("on_surface_variant", false);
}

std::vector<std::pair<std::string, std::string>> halfHours(int from, int to) {
    std::vector<std::pair<std::string, std::string>> out;
    for (int m = from; m != to; m = (m + 30) % (24 * 60)) {
        const std::string t = std::format("{:02}:{:02}", m / 60, m % 60);
        out.push_back({t, t});
    }
    return out;
}

} // namespace

void wellbeingPage(Flex* page, Popup&) {
    // Today: the ring, the total in it, the top apps beside it as a legend
    auto* head = group(page, "Today");
    auto* top = head->add<Flex>(Flex::Direction::Row);
    top->setPadding({20, 18})->setSpacing(24)->setAlignItems(Align::Center);
    auto* ringBox = top->add<Flex>(Flex::Direction::Column);
    auto* ring = ringBox->add<UsageRing>(180);
    auto* legend = top->add<Flex>(Flex::Direction::Column);
    legend->setSpacing(8)->setAlignItems(Align::Start);
    legend->grow = true;
    auto* total = legend->add<Label>();
    total->setSize(32)->setWeight(500);
    legend->add<Label>("Screen time today")->setSize(13)->setColor("on_surface_variant", false);
    auto* keys = legend->add<Flex>(Flex::Direction::Column);
    keys->setSpacing(6)->setAlignItems(Align::Start)->setPadding({0, 8, 0, 0});
    auto* stats = head->add<Flex>(Flex::Direction::Row);
    stats->setPadding({20, 0, 20, 18})->setSpacing(10);
    Label *unlocks = nullptr, *notes = nullptr, *average = nullptr;
    stat(stats, "lock_open", unlocks, "Unlocks");
    stat(stats, "notifications_active", notes, "Notifications");
    stat(stats, "bar_chart", average, "Daily average");

    auto* week = group(page, "This week");
    auto* barsBox = week->add<Flex>(Flex::Direction::Column);
    barsBox->setPadding({20, 18})->setAlignItems(Align::Stretch);
    auto* bars = barsBox->add<WeekBars>();

    // The lists: rebuilt only when what's in them changes (an app, a
    // timer, Focus, Bedtime), so an open dropdown stays open; between,
    // only the times move
    struct Lists {
        std::string shape;                       // what they were built from
        std::map<std::string, Label*> subtitles; // an app's time, by class
    };
    auto lists = std::make_shared<Lists>();
    auto* live = page->add<Live>();
    live->setSpacing(0)->setAlignItems(Align::Stretch);
    live->sync = [=] {
        auto& w = Wellbeing::get();
        const auto& today = w.today();
        const auto apps = ranked(today);
        // The ring: the top four, then the rest together
        // ("Other": everything they don't cover, the briefly used too)
        std::vector<std::pair<std::string, int>> parts;
        int shown = 0;
        for (size_t i = 0; i < apps.size() && i < 4; ++i) {
            parts.push_back(apps[i]);
            shown += apps[i].second;
        }
        if (const int rest = today.total() - shown; rest > 0) parts.push_back({"", rest});
        ring->set(parts, today.total());
        total->setText(duration(today.total()));
        keys->clearChildren();
        for (size_t i = 0; i < parts.size(); ++i) {
            auto* k = keys->add<Flex>(Flex::Direction::Row);
            k->setSpacing(8)->setAlignItems(Align::Center);
            auto* swatch = k->add<Flex>(Flex::Direction::Row);
            pill(swatch->setBackground(i < 4 ? UsageRing::kRoles[i] : "outline_variant", false));
            swatch->fixedWidth = swatch->fixedHeight = 10;
            k->add<Label>(std::format("{}  {}", parts[i].first.empty() ? "Other" : Wellbeing::nameOf(parts[i].first), duration(parts[i].second)))
                ->setSize(13)
                ->setMaxWidth(260);
        }
        int noteCount = 0;
        for (const auto& [_, c] : today.notifications) noteCount += c;
        unlocks->setText(std::to_string(today.unlocks));
        notes->setText(std::to_string(noteCount));
        // The week, oldest first
        std::vector<int> days;
        std::vector<std::string> labels;
        int sum = 0, counted = 0;
        for (int ago = 6; ago >= 0; --ago) {
            const int t = w.day(ago).total();
            days.push_back(t);
            sum += t;
            counted += t > 0;
            const std::time_t when = std::time(nullptr) - static_cast<std::time_t>(ago) * 86400;
            std::tm tm{};
            localtime_r(&when, &tm);
            char buf[8];
            std::strftime(buf, sizeof buf, "%a", &tm);
            labels.push_back(buf);
        }
        bars->set(days, labels);
        average->setText(duration(counted ? sum / counted : 0));

        const auto weekApps = [&] {
            // Today's apps first, then the week's others
            auto out = apps;
            for (int ago = 1; ago <= 6; ++ago)
                for (const auto& kv : ranked(w.day(ago)))
                    if (std::ranges::find(out, kv.first, &std::pair<std::string, int>::first) == out.end()) out.push_back({kv.first, 0});
            if (out.size() > 14) out.resize(14);
            return out;
        }();
        auto subtitle = [&](const std::string& cls, int secs) {
            const int lim = w.limit(cls);
            return w.timeUp(cls) ? std::string("Paused: its time is up for today")
                   : lim         ? std::format("{} of {} today", duration(secs), duration(lim * 60))
                                 : duration(secs) + " today";
        };
        auto& cfg = Config::get();
        std::string shape = std::format("{}|{}|{}|{}|{}|{}|{}", w.focus(), w.bedtime(), cfg.value<bool>("/wellbeing/bedtime/enabled", false),
                                        cfg.value<std::string>("/wellbeing/bedtime/start", ""), cfg.value<std::string>("/wellbeing/bedtime/end", ""),
                                        cfg.value<bool>("/wellbeing/bedtime/grayscale", true), cfg.value<bool>("/wellbeing/bedtime/dnd", true));
        for (const auto& d : w.focusApps()) shape += "|f:" + d;
        auto& notif = Notifications::get();
        shape += std::format("|{}|{}|{}", w.breakEvery(), w.goal(), notif.doNotDisturb());
        // The week's notifying apps, and which are muted
        std::map<std::string, int> sent;
        for (int ago = 0; ago <= 6; ++ago)
            for (const auto& [app, c] : w.day(ago).notifications) sent[app] += c;
        for (const auto& [app, _] : sent) shape += std::format("|n:{}:{}", app, notif.muted(app));
        for (const auto& [cls, _] : weekApps) shape += std::format("|{}:{}:{}", cls, w.limit(cls), w.timeUp(cls));
        if (shape == lists->shape) {
            for (const auto& [cls, secs] : weekApps)
                if (auto it = lists->subtitles.find(cls); it != lists->subtitles.end()) it->second->setText(subtitle(cls, secs));
            return;
        }
        lists->shape = shape;
        lists->subtitles.clear();
        live->clearChildren();
        // Each app, with its timer
        auto* list = group(live, "App timers", "When an app's time is up it closes for the rest of the day; a minute's warning first");
        if (weekApps.empty()) {
            auto r = row(list, "hourglass", "Nothing yet", "Apps show up here as you use them");
            r.title->setColor("on_surface_variant", false);
        }
        for (const auto& [cls, secs] : weekApps) {
            auto* item = list->add<Flex>(Flex::Direction::Column);
            item->setAlignItems(Align::Stretch);
            const int lim = w.limit(cls);
            auto r = row(item, "", Wellbeing::nameOf(cls), subtitle(cls, secs));
            lists->subtitles[cls] = r.subtitle;
            if (w.timeUp(cls)) r.subtitle->setColor("error", false);
            r.row->adopt(appIcon(cls, 28), 0);
            const std::string c = cls;
            dropdown(r.row, item, lim ? duration(lim * 60) : "No timer",
                     {{"0", "No timer"}, {"15", "15 min"}, {"30", "30 min"}, {"60", "1 h"}, {"120", "2 h"}, {"180", "3 h"}},
                     [c](const std::string& v) { Wellbeing::get().setLimit(c, std::atoi(v.c_str())); });
        }

        // Focus mode
        auto* focus = group(live, "Focus mode", "Pause distracting apps: while it's on, they close when opened");
        auto f = row(focus, "self_improvement", "Focus mode", w.focus() ? "On" : "Off");
        auto* fsw = f.row->add<Switch>();
        fsw->setChecked(w.focus(), false);
        fsw->onToggled = [](bool on) { Wellbeing::get().setFocus(on); };
        const auto distracting = w.focusApps();
        auto candidates = weekApps;
        for (const auto& d : distracting)
            if (std::ranges::find(candidates, d, &std::pair<std::string, int>::first) == candidates.end()) candidates.push_back({d, 0});
        for (const auto& [cls, _] : candidates) {
            const bool on = std::ranges::find(distracting, cls) != distracting.end();
            auto r = row(focus, "", Wellbeing::nameOf(cls), on ? "Distracting: paused in Focus mode" : "");
            r.row->adopt(appIcon(cls, 24), 0);
            auto* sw = r.row->add<Switch>();
            sw->setChecked(on, false);
            const std::string c = cls;
            sw->onToggled = [c](bool x) { Wellbeing::get().setFocusApp(c, x); };
        }

        // Bedtime
        auto* bed = group(live, "Bedtime mode", "Winds the evening down: the screen in greyscale and Do not disturb, on a schedule");
        auto now = row(bed, "bedtime", "Bedtime now", w.bedtime() ? "On" : "Off");
        pillButton(now.row, w.bedtime() ? "Stop" : "Start now", !w.bedtime(), [on = w.bedtime()] { Wellbeing::get().setBedtimeNow(!on); },
                   w.bedtime() ? "wb_sunny" : "bedtime");
        auto sched = row(bed, "schedule", "On a schedule", "");
        auto* ssw = sched.row->add<Switch>();
        ssw->setChecked(cfg.value<bool>("/wellbeing/bedtime/enabled", false), false);
        ssw->onToggled = [](bool on) { Config::get().set("/wellbeing/bedtime/enabled", on); };
        auto* startItem = bed->add<Flex>(Flex::Direction::Column);
        startItem->setAlignItems(Align::Stretch);
        auto start = row(startItem, "bedtime", "Starts at", "");
        dropdown(start.row, startItem, cfg.value<std::string>("/wellbeing/bedtime/start", "22:00"), halfHours(19 * 60, 3 * 60),
                 [](const std::string& v) { Config::get().set("/wellbeing/bedtime/start", v); });
        auto* endItem = bed->add<Flex>(Flex::Direction::Column);
        endItem->setAlignItems(Align::Stretch);
        auto end = row(endItem, "wb_twilight", "Ends at", "");
        dropdown(end.row, endItem, cfg.value<std::string>("/wellbeing/bedtime/end", "07:00"), halfHours(4 * 60, 11 * 60),
                 [](const std::string& v) { Config::get().set("/wellbeing/bedtime/end", v); });
        auto grey = row(bed, "filter_b_and_w", "Greyscale at bedtime",
                        w.bedtime() ? "Colours fade while Bedtime is on" : "Colours fade during Bedtime: Start now to see it");
        auto* gsw = grey.row->add<Switch>();
        gsw->setChecked(cfg.value<bool>("/wellbeing/bedtime/grayscale", true), false);
        gsw->onToggled = [](bool on) { Config::get().set("/wellbeing/bedtime/grayscale", on); };
        auto dnd = row(bed, "do_not_disturb_on", "Do not disturb at bedtime", "Only critical notifications pop up");
        auto* dsw = dnd.row->add<Switch>();
        dsw->setChecked(cfg.value<bool>("/wellbeing/bedtime/dnd", true), false);
        dsw->onToggled = [](bool on) { Config::get().set("/wellbeing/bedtime/dnd", on); };

        // Reminders
        auto* rem = group(live, "Screen time reminders", "A nudge to look away, and a daily goal");
        auto* breakItem = rem->add<Flex>(Flex::Direction::Column);
        breakItem->setAlignItems(Align::Stretch);
        auto brk = row(breakItem, "self_care", "Take a break", "After this long on screen without one");
        auto minutes = [](int m) { return m == 0 ? std::string("Off") : m % 60 ? std::format("{} min", m) : std::format("{} h", m / 60); };
        dropdown(brk.row, breakItem, minutes(w.breakEvery()), {{"0", "Off"}, {"20", "20 min"}, {"30", "30 min"}, {"45", "45 min"}, {"60", "1 h"}, {"90", "1 h 30 min"}},
                 [](const std::string& v) { Config::get().set("/wellbeing/breakEvery", std::atoi(v.c_str())); });
        auto* goalItem = rem->add<Flex>(Flex::Direction::Column);
        goalItem->setAlignItems(Align::Stretch);
        auto gl = row(goalItem, "flag", "Daily screen time goal",
                      w.goal() ? std::format("{} so far", duration(today.total())) : "Told once when you pass it");
        dropdown(gl.row, goalItem, minutes(w.goal()), {{"0", "Off"}, {"120", "2 h"}, {"180", "3 h"}, {"240", "4 h"}, {"360", "6 h"}, {"480", "8 h"}},
                 [](const std::string& v) { Config::get().set("/wellbeing/goal", std::atoi(v.c_str())); });

        // Fewer interruptions: Do not disturb, and each app's notifications
        auto* calm = group(live, "Reduce interruptions", "Do not disturb, and which apps may notify you");
        auto d = row(calm, "do_not_disturb_on", "Do not disturb", notif.doNotDisturb() ? "On: only critical notifications pop up" : "Off");
        auto* dndSw = d.row->add<Switch>();
        dndSw->setChecked(notif.doNotDisturb(), false);
        dndSw->onToggled = [](bool on) { Notifications::get().setDoNotDisturb(on); };
        std::vector<std::pair<std::string, int>> senders(sent.begin(), sent.end());
        std::ranges::sort(senders, std::greater{}, &std::pair<std::string, int>::second);
        for (const auto& [app, count] : senders) {
            const bool muted = notif.muted(app);
            auto r = row(calm, muted ? "notifications_off" : "notifications", app,
                         muted ? "Muted: its notifications don't show" : std::format("{} this week", count));
            r.title->setMaxWidth(300);
            auto* sw = r.row->add<Switch>();
            sw->setChecked(!muted, false);
            const std::string a = app;
            sw->onToggled = [a](bool allowed) { Notifications::get().setMuted(a, !allowed); };
        }
        if (senders.empty()) {
            auto r = row(calm, "notifications", "No notifications this week", "Apps that notify you show up here, to mute");
            r.title->setColor("on_surface_variant", false);
        }
    };
    live->watch(Notifications::get().changed);
    live->watch(Wellbeing::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
