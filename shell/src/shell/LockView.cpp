#include "shell/LockView.h"

#include "render/Images.h"
#include "core/Config.h"
#include "services/Auth.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Weather.h"
#include "ui/Theme.h"

#include <cmath>
#include <format>
#include <ctime>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

namespace {

Button* arrow(Node* parent, const char* icon, std::function<void()> fn) {
    auto* b = parent->add<Button>();
    pill(b)->setPadding(Insets(6));
    b->add<Icon>(icon, 22)->setColor("on_surface", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT) fn();
    };
    return b;
}

std::string initial(const std::string& name) {
    if (name.empty()) return "?";
    // First UTF-8 character, upper-cased if ASCII
    size_t n = 1;
    while (n < name.size() && (static_cast<unsigned char>(name[n]) & 0xC0) == 0x80) ++n;
    std::string s = name.substr(0, n);
    if (n == 1) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// One lock-screen widget: rebuilt whenever its service changes
class LockWidget : public Flex {
public:
    LockWidget(std::string id, bool greeter) : Flex(Direction::Row), m_id(std::move(id)), m_greeter(greeter) {
        setSpacing(10)->setJustify(Align::Center);
        auto watch = [this](Signal<>& sig) { m_conns.push_back(sig.connect([this] { sync(); })); };
        if (m_id == "weather" || m_id == "forecast") watch(Weather::get().changed);
        if (m_id == "media") watch(Mpris::get().changed);
        if (m_id == "battery") watch(Power::get().changed);
        if (m_id == "notifications") watch(Notifications::get().changed);
        if (m_id == "date") tick();
        else sync();
    }
    ~LockWidget() override {
        if (m_timer) EventLoop::get().cancel(m_timer);
    }

private:
    // A rounded, translucent chip so widgets read on any wallpaper
    Flex* chip() {
        auto* c = add<Flex>(Direction::Row);
        pill(c->setBackground(ColorRef("surface_container_high", 0.75f), false))->setPadding({16, 8})->setSpacing(8);
        return c;
    }
    void tick() {
        clearChildren();
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char buf[64];
        std::strftime(buf, sizeof buf, "%A, %-d %B", &tm);
        add<Label>(buf)->setSize(22)->setWeight(500)->setColor("on_surface", false);
        m_timer = EventLoop::get().addTimer((60 - tm.tm_sec) * 1000 + 50, [this] {
            m_timer = 0;
            tick();
        });
    }
    void sync() {
        clearChildren();
        auto& w = Weather::get();
        if (m_id == "weather") {
            if (!w.ready()) return setVisible(false);
            setVisible(true);
            add<Icon>(w.icon(), 30)->setFill(true)->setColor("primary", false);
            add<Label>(w.temp(w.temperature()))->setSize(26)->setWeight(500)->setColor("on_surface", false);
            add<Label>(Weather::describe(w.code()) + (w.place().empty() ? "" : "  ·  " + w.place().substr(0, w.place().find(','))))
                ->setSize(16)->setColor("on_surface_variant", false);
        } else if (m_id == "forecast") {
            if (w.days().size() < 4) return setVisible(false);
            setVisible(true);
            setSpacing(8);
            for (size_t i = 1; i < 4; ++i) {
                const auto& d = w.days()[i];
                std::tm tm{};
                strptime(d.date.c_str(), "%Y-%m-%d", &tm);
                char day[16];
                std::strftime(day, sizeof day, "%a", &tm);
                auto* c = chip();
                c->add<Label>(day)->setSize(14)->setWeight(500)->setColor("on_surface", false);
                c->add<Icon>(Weather::icon(d.code, true), 20)->setColor("primary", false);
                c->add<Label>(w.temp(d.max) + " / " + w.temp(d.min))->setSize(14)->setColor("on_surface_variant", false);
            }
        } else if (m_id == "media") {
            const auto* p = m_greeter ? nullptr : Mpris::get().active();
            if (!p) return setVisible(false);
            setVisible(true);
            auto* c = chip();
            c->setPadding({16, 6, 6, 6});
            c->add<Icon>("music_note", 20)->setColor("primary", false);
            c->add<Label>(p->title + (p->artist.empty() ? "" : "  ·  " + p->artist))->setSize(15)->setMaxWidth(380)->setColor("on_surface", false);
            for (auto [icon, fn] : {std::pair<const char*, int>{"skip_previous", 0}, {p->status == "Playing" ? "pause" : "play_arrow", 1}, {"skip_next", 2}}) {
                auto* b = c->add<Button>();
                pill(b)->setPadding(Insets(6));
                b->add<Icon>(icon, 20)->setColor("on_surface", false);
                b->onClicked = [fn](uint32_t) {
                    auto& m = Mpris::get();
                    if (fn == 0) m.previous();
                    else if (fn == 1) m.playPause();
                    else m.next();
                };
            }
        } else if (m_id == "battery") {
            auto& p = Power::get();
            if (!p.hasBattery()) return setVisible(false);
            setVisible(true);
            auto* c = chip();
            c->add<Icon>(p.icon(), 20)->setColor("primary", false);
            c->add<Label>(std::format("{}%{}", p.percent(), p.charging() ? " · charging" : ""))->setSize(15)->setColor("on_surface", false);
        } else if (m_id == "notifications") {
            // A count, never the content: anyone can see a lock screen
            const size_t n = m_greeter ? 0 : Notifications::get().history().size();
            if (n == 0) return setVisible(false);
            setVisible(true);
            auto* c = chip();
            c->add<Icon>("notifications", 20)->setColor("primary", false);
            c->add<Label>(n == 1 ? "1 notification" : std::format("{} notifications", n))->setSize(15)->setColor("on_surface", false);
        }
    }
    std::string m_id;
    bool m_greeter;
    std::vector<Connection> m_conns;
    EventLoop::TimerId m_timer = 0;
};

} // namespace

LockView::LockView(Options options) : Flex(Direction::Column), m_options(std::move(options)) {
    setJustify(Align::Center)->setAlignItems(Align::Center)->setSpacing(0);
    if (m_options.users.empty()) m_options.users.push_back({m_options.user, auth::realName(m_options.user)});
    for (size_t i = 0; i < m_options.users.size(); ++i)
        if (m_options.users[i].id == m_options.user) m_userIndex = i;

    // Clock and widgets, as set in Settings → Lock screen (shared with the
    // greeter through its synced copy of config.json)
    const auto& cfg = Config::get();
    m_stacked = cfg.value<std::string>("/lock/clock/style", "stacked") == "stacked";
    const bool left = cfg.value<std::string>("/lock/clock/align", "center") == "left";
    const float size = static_cast<float>(cfg.value<double>("/lock/clock/size", 150));
    auto* top = add<Flex>(Direction::Column);
    top->alignSelf = Align::Stretch;
    top->setAlignItems(left ? Align::Start : Align::Center)->setSpacing(12)->setPadding({left ? 120.0f : 0.0f, 0});

    // Clock: hours over minutes like Android's lock screen, or on one line
    auto* clock = top->add<Flex>(Direction::Column);
    clock->setSpacing(-size * 0.26f)->setAlignItems(left ? Align::Start : Align::Center);
    m_hours = clock->add<Label>();
    if (m_stacked) m_minutes = clock->add<Label>();
    for (auto* l : {m_hours, m_minutes})
        if (l) l->setSize(size)->setWeight(cfg.value<int>("/lock/clock/weight", 400))->setColor("primary", false);

    auto ids = cfg.value<nlohmann::json>("/lock/widgets", nlohmann::json());
    if (!ids.is_array()) ids = {"date", "weather", "media"};
    for (const auto& id : ids)
        if (id.is_string()) top->add<LockWidget>(id.get<std::string>(), m_options.greeter)->alignSelf = left ? Align::Start : Align::Center;

    auto* spacer = add<Node>();
    spacer->minHeight = 72;

    // Avatar and name (with pickers in greeter mode)
    auto* who = add<Flex>(Direction::Row);
    who->setSpacing(12);
    if (m_options.greeter && m_options.users.size() > 1)
        arrow(who, "chevron_left", [this] { cycle(m_options.users, m_userIndex, -1, m_name); });
    auto* avatar = who->add<Flex>(Direction::Row);
    pill(avatar->setBackground("primary_container", false))->setJustify(Align::Center);
    avatar->fixedWidth = avatar->fixedHeight = 56;
    m_avatarLetter = avatar->add<Label>();
    m_avatarLetter->setSize(24)->setWeight(600)->setColor("on_primary_container", false);
    m_name = who->add<Label>(m_options.users[m_userIndex].label);
    m_name->setSize(20)->setWeight(500)->setColor("on_surface", false);
    if (m_options.greeter && m_options.users.size() > 1)
        arrow(who, "chevron_right", [this] { cycle(m_options.users, m_userIndex, 1, m_name); });
    m_avatarLetter->setText(initial(m_options.users[m_userIndex].label));

    auto* gap = add<Node>();
    gap->minHeight = 20;

    // Password pill: field plus submit arrow
    m_pill = add<Flex>(Direction::Row);
    m_pill->setSpacing(8);
    m_field = m_pill->add<TextField>();
    m_field->setSecret(true)->setPlaceholder("Password");
    m_field->minWidth = 320;
    m_field->onSubmit = [this](const std::string& text) {
        if (!m_busy && onSubmit) onSubmit(text);
    };
    auto* go = m_pill->add<Button>();
    pill(go->setBackground("primary", false))->setJustify(Align::Center);
    go->fixedWidth = go->fixedHeight = 48;
    go->add<Icon>("arrow_forward", 24)->setColor("on_primary", false);
    go->onClicked = [this](uint32_t) {
        if (!m_busy && onSubmit) onSubmit(m_field->text());
    };

    m_status = add<Label>(" ");
    m_status->setSize(14)->setColor("on_surface_variant", false);
    m_status->minHeight = 32;

    if (m_options.greeter) {
        // Session picker and power buttons along the bottom
        auto* bottom = add<Flex>(Direction::Row);
        bottom->setSpacing(8)->setPadding({0, 24});
        if (!m_options.sessions.empty()) {
            arrow(bottom, "chevron_left", [this] { cycle(m_options.sessions, m_sessionIndex, -1, m_session); });
            m_session = bottom->add<Label>(m_options.sessions.front().label);
            m_session->setSize(15)->setColor("on_surface", false);
            arrow(bottom, "chevron_right", [this] { cycle(m_options.sessions, m_sessionIndex, 1, m_session); });
        }
        auto* sep = bottom->add<Node>();
        sep->minWidth = 32;
        for (auto [icon, action] : {std::pair{"bedtime", "suspend"}, {"restart_alt", "reboot"}, {"power_settings_new", "poweroff"}})
            arrow(bottom, icon, [this, a = std::string(action)] {
                if (onPower) onPower(a);
            });
    }

    tick();
}

LockView::~LockView() {
    if (m_clock) EventLoop::get().cancel(m_clock);
}

void LockView::tick() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[64];
    if (m_stacked) {
        std::strftime(buf, sizeof buf, "%H", &tm);
        m_hours->setText(buf);
        std::strftime(buf, sizeof buf, "%M", &tm);
        m_minutes->setText(buf);
    } else {
        std::strftime(buf, sizeof buf, "%H:%M", &tm);
        m_hours->setText(buf);
    }
    m_clock = EventLoop::get().addTimer(static_cast<int>((60 - tm.tm_sec) * 1000 + 20), [this] {
        m_clock = 0;
        tick();
    });
}

void LockView::cycle(std::vector<Choice>& list, size_t& index, int step, Label* label) {
    if (list.empty()) return;
    index = (index + list.size() + static_cast<size_t>(step + static_cast<int>(list.size()))) % list.size();
    if (label) label->setText(list[index].label);
    if (&list == &m_options.users) {
        m_avatarLetter->setText(initial(list[index].label));
        if (onUserChanged) onUserChanged(list[index].id);
    }
}

const std::string& LockView::user() const {
    return m_options.users[m_userIndex].id;
}

const std::string& LockView::session() const {
    static const std::string none;
    return m_options.sessions.empty() ? none : m_options.sessions[m_sessionIndex].id;
}

void LockView::setBusy(bool busy) {
    m_busy = busy;
    m_status->setColor("on_surface_variant");
    if (busy) m_status->setText("Checking…");
}

void LockView::fail(const std::string& message) {
    m_busy = false;
    m_field->setText("");
    m_status->setText(message);
    m_status->setColor("error");
    // A quick jolt sideways that eases back
    m_pill->setTranslateX(24, false);
    m_pill->setTranslateX(0);
}

void LockView::setPrompt(const std::string& text, bool secret) {
    m_busy = false;
    m_field->setText("");
    m_field->setSecret(secret);
    m_field->setPlaceholder(text);
}

void LockView::setInfo(const std::string& text) {
    m_status->setText(text);
    m_status->setColor("on_surface_variant");
}

void LockView::doPaint(Renderer& r) {
    const auto& b = rect();
    r.rect(b, Theme::get().color("surface"));
    if (!m_options.wallpaper.empty()) {
        const int w = static_cast<int>(std::lround(b.w * r.scale())), h = static_cast<int>(std::lround(b.h * r.scale()));
        if (auto* t = Images::get().picture(m_options.wallpaper, w, h, true, [this] { requestRedraw(); })) {
            const float s = std::max(b.w / static_cast<float>(t->width), b.h / static_cast<float>(t->height));
            const float dw = static_cast<float>(t->width) * s, dh = static_cast<float>(t->height) * s;
            r.image(t->id, {b.x + (b.w - dw) / 2, b.y + (b.h - dh) / 2, dw, dh});
        }
    }
    // Scrim keeps the clock and field readable on any wallpaper
    r.rect(b, Theme::get().color("surface").withAlpha(0.55f));
}

} // namespace m3::shell
