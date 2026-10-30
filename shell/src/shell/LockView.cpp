#include "shell/LockView.h"

#include "core/Config.h"
#include "core/Paths.h"
#include "render/Images.h"
#include "services/Alarms.h"
#include "services/Auth.h"
#include "services/Mpris.h"
#include "services/Network.h"
#include "services/Notifications.h"
#include "services/Weather.h"
#include "shell/ClockFaces.h"
#include "shell/LockLayout.h"
#include "shell/WidgetStyles.h"
#include "shell/PanelKit.h"
#include "shell/Ringer.h"
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

// Clicks anywhere outside an open menu land here and close it
class Scrim : public Node {
public:
    std::function<void()> onClicked;
    bool interactive() const override { return true; }
    void onClick(uint32_t) override {
        if (onClicked) onClicked();
    }
};

// One choice in a greeter menu: icon, title, optional subtitle and a check
// A session's compositor logo (Hyprland's, niri's, from the shell's data,
// which the greeter user can read), by its command or name; else ""
std::string sessionLogo(const std::string& exec, const std::string& label) {
    std::string both = exec + " " + label;
    for (auto& ch : both) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (both.find("hyprland") != std::string::npos) return paths::dataFile("icons/hyprland-logo.svg");
    if (both.find("niri") != std::string::npos) return paths::dataFile("icons/niri-logo.svg");
    return {};
}

// An image file drawn at `size`
ImageView::Provider logoOf(const std::string& path, float size) {
    return [path, size](float scale, int& w, int& h) -> GLuint {
        if (path.empty()) return 0;
        auto* t = Images::get().icon(path, static_cast<int>(std::lround(size * scale)));
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    };
}

// A row of a menu: the glyph, or `logo` (an image file) in its place
Button* menuItem(Flex* menu, const char* icon, const std::string& title, const std::string& sub, bool selected,
                 std::function<void()> fn, const std::string& logo = {}) {
    auto* b = menu->add<Button>(Flex::Direction::Row);
    b->setPadding({16, 12})->setSpacing(16)->setRadius(Radii(20));
    b->alignSelf = Align::Stretch;
    b->pressedRadius = 12;
    if (selected) b->setBackground("secondary_container", false);
    if (!logo.empty()) b->add<ImageView>(22, logoOf(logo, 22));
    else b->add<Icon>(icon, 22)->setColor(selected ? "on_secondary_container" : "on_surface_variant", false);
    auto* texts = b->add<Flex>(Flex::Direction::Column);
    texts->setAlignItems(Align::Start)->setSpacing(1);
    texts->grow = true;
    texts->add<Label>(title)->setSize(15)->setWeight(selected ? 600 : 500)->setColor(selected ? "on_secondary_container" : "on_surface", false);
    if (!sub.empty()) texts->add<Label>(sub)->setSize(13)->setColor(selected ? "on_secondary_container" : "on_surface_variant", false);
    if (selected) b->add<Icon>("check", 22)->setColor("on_secondary_container", false);
    b->onClicked = [fn = std::move(fn)](uint32_t button) {
        if (button == BTN_LEFT) fn();
    };
    return b;
}

// A session's subtitle: what kind of session the command starts
std::string sessionNote(const std::string& exec) {
    if (exec.find("uwsm") != std::string::npos) return "Started through uwsm (systemd units)";
    if (exec.find("start-hyprland") != std::string::npos || exec.find("Hyprland") != std::string::npos)
        return "Material3UI's home";
    if (exec.find("niri") != std::string::npos) return "Scrollable tiling, with Material3UI";
    return exec;
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

// Lines of big digits pulled together by a fraction of their drawn size, so
// they stay tight when the text is scaled (the lock editor shrinks them)
class TightLines : public Flex {
public:
    explicit TightLines(float overlap) : Flex(Direction::Column), m_overlap(overlap) {}

protected:
    SizeF doMeasure(SizeF available) override {
        if (const auto* first = children().empty() ? nullptr : dynamic_cast<const Label*>(children().front().get()))
            m_spacing = -first->shownSize() * m_overlap;
        return Flex::doMeasure(available);
    }

private:
    float m_overlap;
};

// One lock-screen widget: rebuilt whenever its service changes
class LockWidget : public Flex {
public:
    LockWidget(std::string id, bool greeter, Align align, float clockScale)
        : Flex(Direction::Row), m_id(std::move(id)), m_greeter(greeter), m_align(align), m_clockScale(clockScale) {
        setSpacing(10)->setJustify(Align::Center);
        if (m_id == "clock") {
            clockTick();
            return;
        }
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
    // Hours over minutes like Android's lock screen (in one colour or two),
    // on one line, or an analog face (the desktop's Material You or minimal)
    void clockTick() {
        clearChildren();
        const auto& cfg = Config::get();
        const std::string style = cfg.value<std::string>("/lock/clock/style", "stacked");
        const float size = static_cast<float>(cfg.value<double>("/lock/clock/size", 150)) * m_clockScale;
        if (style == "analog" || style == "dots") {
            // About as tall as the stacked digits; it moves on its own each second
            add<kit::AnalogFace>(style == "analog" ? kit::AnalogFace::Style::Cookie : kit::AnalogFace::Style::Dots, size * 1.3f);
            return;
        }
        const bool stacked = style != "inline", twoTone = style == "twotone";
        auto* col = add<TightLines>(0.26f);
        col->setAlignItems(m_align);
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char buf[16];
        auto line = [&](const char* fmt, const char* colour = "primary") {
            std::strftime(buf, sizeof buf, fmt, &tm);
            col->add<Label>(buf)->setSize(size)->setWeight(cfg.value<int>("/lock/clock/weight", 400))->setColor(colour, false);
        };
        if (stacked) {
            line("%H");
            line("%M", twoTone ? "tertiary" : "primary");
        } else {
            line("%H:%M");
        }
        m_timer = EventLoop::get().addTimer((60 - tm.tm_sec) * 1000 + 50, [this] {
            m_timer = 0;
            clockTick();
        });
    }
    // A rounded, translucent chip so widgets read on any wallpaper
    Flex* chip() {
        auto* c = add<Flex>(Direction::Row);
        pill(c->setBackground(ColorRef("surface_container_high", 0.75f), false))->setPadding({16, 8})->setSpacing(8);
        c->blurBehind = true;
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
            const size_t n = m_greeter ? 0 : Notifications::get().current().size();
            if (n == 0) return setVisible(false);
            setVisible(true);
            auto* c = chip();
            c->add<Icon>("notifications", 20)->setColor("primary", false);
            c->add<Label>(n == 1 ? "1 notification" : std::format("{} notifications", n))->setSize(15)->setColor("on_surface", false);
        }
    }
    std::string m_id;
    bool m_greeter;
    Align m_align;
    float m_clockScale;
    std::vector<Connection> m_conns;
    EventLoop::TimerId m_timer = 0;
};

// Places widgets in a 3-column grid: columns are equal thirds (contents
// left, centred, right), rows take their tallest widget and empty rows
// collapse, so the block reads top to bottom like the editor's grid.
class LockGrid : public Node {
public:
    float spacing = 14, sidePadding = 96;
    void place(Node* child, int col, int row) { m_cells.push_back({child, col, row}); }

protected:
    SizeF doMeasure(SizeF available) override {
        float h = 0;
        int rows = 0;
        for (const float rh : rowHeights(available.w)) {
            if (rh <= 0) continue;
            h += rh;
            ++rows;
        }
        if (rows > 1) h += spacing * static_cast<float>(rows - 1);
        return {available.w, h};
    }
    void doArrange() override {
        const auto& r = rect();
        const float colW = (r.w - sidePadding * 2) / lockgrid::kCols;
        const auto heights = rowHeights(r.w);
        std::vector<float> rowY(heights.size());
        float y = r.y;
        for (size_t i = 0; i < heights.size(); ++i) {
            rowY[i] = y;
            if (heights[i] > 0) y += heights[i] + spacing;
        }
        for (const auto& c : m_cells) {
            const SizeF s = c.node->measure({colW, 1e6f});
            const float x0 = r.x + sidePadding + colW * static_cast<float>(c.col);
            const float x = c.col == 0 ? x0 : c.col == 1 ? x0 + (colW - s.w) / 2 : x0 + colW - s.w;
            const auto row = static_cast<size_t>(c.row);
            c.node->arrange({x, rowY[row] + (heights[row] - s.h) / 2, s.w, s.h});
        }
    }

private:
    struct Cell {
        Node* node;
        int col, row;
    };
    std::vector<float> rowHeights(float width) {
        std::vector<float> h(lockgrid::kRows, 0);
        const float colW = (width - sidePadding * 2) / lockgrid::kCols;
        for (const auto& c : m_cells)
            if (c.node->shown()) h[static_cast<size_t>(c.row)] = std::max(h[static_cast<size_t>(c.row)], c.node->measure({colW, 1e6f}).h);
        return h;
    }
    std::vector<Cell> m_cells;
};

// The M3 Expressive music player as a lock-screen widget of its own; shown
// while something plays, never on the login screen (no session to control)
class LockPlayer : public Flex {
public:
    explicit LockPlayer(bool greeter) : Flex(Direction::Column), m_greeter(greeter) {
        setAlignItems(Align::Stretch);
        if (m_greeter) {
            setVisible(false);
            return;
        }
        auto* card = add<kit::MediaCard>();
        card->fixedWidth = 380;
        card->blurBehind = true;
        m_conn = Mpris::get().changed.connect([this] { sync(); });
        sync();
    }

private:
    void sync() { setVisible(Mpris::get().active() != nullptr); }
    bool m_greeter;
    Connection m_conn;
};

} // namespace

ui::Node* addLockWidget(ui::Node* parent, const std::string& id, bool greeter, int col, float clockScale) {
    if (id == "player") return parent->add<LockPlayer>(greeter);
    const Align align = col == 0 ? Align::Start : col == 1 ? Align::Center : Align::End;
    if (auto* styled = addStyledWidget(parent, id, greeter, align)) return styled;
    return parent->add<LockWidget>(id, greeter, align, clockScale);
}

LockView::LockView(Options options) : Flex(Direction::Column), m_options(std::move(options)) {
    textArea = Theme::AreaLock; // the login screen's too
    setJustify(Align::Center)->setAlignItems(Align::Center)->setSpacing(0);
    if (m_options.users.empty()) m_options.users.push_back({m_options.user, auth::realName(m_options.user)});
    for (size_t i = 0; i < m_options.users.size(); ++i)
        if (m_options.users[i].id == m_options.user) m_userIndex = i;

    // The greeter's bottom bar takes the last row; grow spacers keep the
    // face centred above it
    if (m_options.greeter) add<Node>()->grow = true;

    // Clock and widgets on the grid set in Settings → Lock screen (shared
    // with the greeter through its synced copy of config.json)
    auto* grid = add<LockGrid>();
    grid->alignSelf = Align::Stretch;
    for (const auto& it : lockgrid::load()) {
        auto* w = addLockWidget(grid, it.id, m_options.greeter, it.col);
        // The clock keeps its own size (Settings → Lock screen → Clock)
        if (it.id != "clock") w->textArea = Theme::AreaWidgets;
        else w->ownTextSize = true;
        grid->place(w, it.col, it.row);
    }

    // An alarm or a timer going off: stopped or snoozed from here, locked,
    // in the widgets' place while it rings (Android's full-screen alarm)
    if (!m_options.greeter) {
        addRingingCards(this, 440);
        auto ringing = [grid] { grid->setVisible(m3::Alarms::get().ringing().empty()); };
        m_alarms = m3::Alarms::get().changed.connect(ringing);
        ringing();
    }

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
    auto* go = m_go = m_pill->add<Button>();
    pill(go->setBackground("primary", false))->setJustify(Align::Center);
    go->fixedWidth = go->fixedHeight = 48;
    m_goArrow = go->add<Icon>("arrow_forward", 24);
    m_goArrow->setColor("on_primary", false);
    m_goLoader = go->add<kit::WavyLoader>(34);
    m_goLoader->setVisible(false);
    go->onClicked = [this](uint32_t) {
        if (!m_busy && onSubmit) onSubmit(m_field->text());
    };

    m_status = add<Label>(" ");
    m_status->setSize(14)->setColor("on_surface_variant", false);
    m_status->minHeight = 32;

    if (m_options.greeter) buildBottomBar();
}

LockView::~LockView() = default;

void LockView::buildBottomBar() {
    auto* bar = add<Flex>(Direction::Row);
    bar->alignSelf = Align::Stretch;
    bar->setPadding({32, 0, 32, 28})->setSpacing(12);

    if (!m_options.sessions.empty()) {
        // Remembered session first (the greeter sorts it there)
        m_sessionChip = bar->add<Button>(Direction::Row);
        pill(m_sessionChip->setBackground(ColorRef("surface_container_high", 0.8f), false))->setPadding({16, 10, 12, 10})->setSpacing(10);
        m_sessionChip->blurBehind = true;
        m_sessionGlyph = m_sessionChip->add<Icon>("desktop_windows", 20);
        m_sessionGlyph->setColor("on_surface_variant", false);
        m_sessionLogo = m_sessionChip->add<ImageView>(20, logoOf({}, 20));
        m_session = m_sessionChip->add<Label>(m_options.sessions[m_sessionIndex].label);
        m_session->setSize(15)->setWeight(500)->setColor("on_surface", false);
        if (m_options.sessions.size() > 1) {
            m_sessionExpand = m_sessionChip->add<Icon>("expand_more", 20);
            m_sessionExpand->setColor("on_surface_variant", false);
        }
        m_sessionChip->onClicked = [this](uint32_t button) {
            if (button != BTN_LEFT || m_options.sessions.size() < 2) return;
            m_open == Menu::Session ? closeMenu() : openMenu(Menu::Session);
        };
        showSession();
    }
    bar->add<Node>()->grow = true;
    m_powerButton = bar->add<Button>(Direction::Row);
    pill(m_powerButton->setBackground(ColorRef("surface_container_high", 0.8f), false))->setJustify(Align::Center);
    m_powerButton->blurBehind = true;
    m_powerButton->fixedWidth = m_powerButton->fixedHeight = 48;
    m_powerButton->add<Icon>("power_settings_new", 22)->setColor("on_surface", false);
    m_powerButton->onClicked = [this](uint32_t button) {
        if (button != BTN_LEFT) return;
        m_open == Menu::Power ? closeMenu() : openMenu(Menu::Power);
    };

    auto* scrim = add<Scrim>();
    scrim->floating = true;
    scrim->setVisible(false);
    scrim->onClicked = [this] { closeMenu(); };
    m_scrim = scrim;
}

void LockView::showSession() {
    if (!m_sessionChip || m_sessionIndex >= m_options.sessions.size()) return;
    const auto& s = m_options.sessions[m_sessionIndex];
    const std::string logo = sessionLogo(s.id, s.label);
    m_sessionLogo->setProvider(logoOf(logo, 20));
    m_sessionLogo->setVisible(!logo.empty());
    m_sessionGlyph->setVisible(logo.empty());
}

void LockView::openMenu(Menu menu) {
    if (m_menu) remove(m_menu);
    m_open = menu;
    m_menu = add<Flex>(Direction::Column);
    m_menu->floating = true;
    m_menu->blurBehind = true;
    m_menu->setBackground(ColorRef("surface_container", 0.92f), false)->setRadius(Radii(28))->setPadding(Insets(8))->setSpacing(2);
    m_menu->setAlignItems(Align::Stretch);
    m_menu->minWidth = 300;
    auto* head = m_menu->add<Flex>(Direction::Row);
    head->setPadding({16, 10, 16, 6});
    head->add<Label>(menu == Menu::Session ? "Session" : "Power")->setSize(14)->setWeight(600)->setColor("primary", false);
    if (menu == Menu::Session) {
        for (size_t i = 0; i < m_options.sessions.size(); ++i) {
            const auto& s = m_options.sessions[i];
            menuItem(
                m_menu, "desktop_windows", s.label, sessionNote(s.id), i == m_sessionIndex,
                [this, i] {
                    m_sessionIndex = i;
                    if (m_session) m_session->setText(m_options.sessions[i].label);
                    showSession();
                    closeMenu();
                },
                sessionLogo(s.id, s.label));
        }
    } else {
        for (auto [icon, label, sub, action] : {std::tuple{"bedtime", "Sleep", "Keeps everything in memory", "suspend"},
                                                {"restart_alt", "Restart", "Turns the computer off and on again", "reboot"},
                                                {"power_settings_new", "Shut down", "Turns the computer off", "poweroff"}})
            menuItem(m_menu, icon, label, sub, false, [this, a = std::string(action)] {
                closeMenu();
                if (onPower) onPower(a);
            });
    }
    m_scrim->setVisible(true);
    if (m_sessionExpand) m_sessionExpand->setText(menu == Menu::Session ? "expand_less" : "expand_more");
    // Rises into place from the bar
    m_menu->setOpacity(0, false);
    m_menu->setTranslateY(16, false);
    m_menu->setOpacity(1);
    m_menu->setTranslateY(0);
    requestLayout();
}

void LockView::closeMenu() {
    if (m_menu) remove(m_menu);
    m_menu = nullptr;
    m_open = Menu::None;
    if (m_scrim) m_scrim->setVisible(false);
    if (m_sessionExpand) m_sessionExpand->setText("expand_more");
    requestLayout();
}

void LockView::doArrange() {
    Flex::doArrange();
    if (m_scrim) m_scrim->arrange(rect());
    if (!m_menu) return;
    // Above the button that opened it, lined up with its outer edge
    const auto* anchor = m_open == Menu::Session ? static_cast<Node*>(m_sessionChip) : m_powerButton;
    const RectF a = anchor->rect();
    const SizeF s = m_menu->measure({std::min(440.0f, rect().w - 32), rect().h});
    const float w = std::max(s.w, 300.0f);
    const float x = m_open == Menu::Session ? a.x : a.x + a.w - w;
    m_menu->arrange({std::clamp(x, rect().x + 16, rect().x + rect().w - 16 - w), a.y - 12 - s.h, w, s.h});
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

void LockView::showChecking(bool on) {
    // The arrow gives way to M3 Expressive's wavy loader on a softer fill
    m_goArrow->setVisible(!on);
    m_goLoader->setVisible(on);
    m_go->setBackground(on ? "primary_container" : "primary");
}

void LockView::setBusy(bool busy) {
    m_busy = busy;
    showChecking(busy);
    m_status->setColor("on_surface_variant");
    if (busy) m_status->setText("Checking…");
}

void LockView::fail(const std::string& message) {
    m_busy = false;
    showChecking(false);
    m_field->setText("");
    m_status->setText(message);
    m_status->setColor("error");
    // A quick jolt sideways that eases back
    m_pill->setTranslateX(24, false);
    m_pill->setTranslateX(0);
}

void LockView::setPrompt(const std::string& text, bool secret) {
    m_busy = false;
    showChecking(false);
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
