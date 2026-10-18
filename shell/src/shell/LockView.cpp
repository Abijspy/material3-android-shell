#include "shell/LockView.h"

#include "render/Images.h"
#include "services/Auth.h"
#include "ui/Theme.h"

#include <cmath>
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

} // namespace

LockView::LockView(Options options) : Flex(Direction::Column), m_options(std::move(options)) {
    setJustify(Align::Center)->setAlignItems(Align::Center)->setSpacing(0);
    if (m_options.users.empty()) m_options.users.push_back({m_options.user, auth::realName(m_options.user)});
    for (size_t i = 0; i < m_options.users.size(); ++i)
        if (m_options.users[i].id == m_options.user) m_userIndex = i;

    // Clock: hours over minutes, like Android's lock screen
    auto* clock = add<Flex>(Direction::Column);
    clock->setSpacing(-28);
    m_hours = clock->add<Label>();
    m_minutes = clock->add<Label>();
    for (auto* l : {m_hours, m_minutes}) l->setSize(150)->setWeight(400)->setColor("primary", false);
    m_date = add<Label>();
    m_date->setSize(22)->setWeight(500)->setColor("on_surface", false);

    auto* spacer = add<Node>();
    spacer->minHeight = 96;

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
    std::strftime(buf, sizeof buf, "%H", &tm);
    m_hours->setText(buf);
    std::strftime(buf, sizeof buf, "%M", &tm);
    m_minutes->setText(buf);
    std::strftime(buf, sizeof buf, "%A, %-d %B", &tm);
    m_date->setText(buf);
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
