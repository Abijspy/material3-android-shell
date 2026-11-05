#include "shell/Notify.h"

#include "core/Config.h"
#include "render/Images.h"
#include "services/Search.h"
#include "shell/Panels.h"
#include "ui/Controls.h"
#include "ui/Theme.h"
#include "wayland/Display.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <format>
#include <linux/input-event-codes.h>

namespace m3::shell {

using namespace m3::ui;

namespace {

constexpr float kToastWidth = 400;
constexpr float kMargin = 12;

std::string ago(std::time_t t) {
    const auto d = std::time(nullptr) - t;
    if (d < 60) return "now";
    if (d < 3600) return std::format("{}m", d / 60);
    if (d < 86400) return std::format("{}h", d / 3600);
    return std::format("{}d", d / 86400);
}

// The best picture for a notification, as an ImageView provider
ImageView::Provider imageFor(const Notifications::Notification& n) {
    const auto id = n.id;
    if (!n.image.data.empty()) {
        auto img = n.image;
        return [img, id](float, int& w, int& h) -> GLuint {
            auto* t = Images::get().raw("notif" + std::to_string(id) + std::to_string(img.data.size()), img.width, img.height,
                                        img.rowstride, img.alpha, img.data);
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        };
    }
    std::string path = n.imagePath;
    if (path.starts_with("file://")) path = path.substr(7);
    if (!path.empty() && path.front() == '/') {
        return [path](float scale, int& w, int& h) -> GLuint {
            const int px = static_cast<int>(std::lround(40 * scale));
            auto* t = Images::get().picture(path, px, px, true);
            if (!t) return 0;
            w = t->width;
            h = t->height;
            return t->id;
        };
    }
    // App icon by name or path, else the desktop entry's icon
    std::string icon = n.appIcon.starts_with("file://") ? n.appIcon.substr(7) : n.appIcon;
    if (icon.empty() && !n.desktopEntry.empty())
        for (const auto& app : Apps::get().all())
            if (app.id == n.desktopEntry + ".desktop") icon = app.icon;
    if (icon.empty()) return {};
    return [icon](float scale, int& w, int& h) -> GLuint {
        const int px = static_cast<int>(std::lround(32 * scale));
        auto* t = Images::get().icon(icon, px);
        if (!t) return 0;
        w = t->width;
        h = t->height;
        return t->id;
    };
}

} // namespace

// ── HistoryItem ──

HistoryItem::HistoryItem(const Notifications::Notification& n) : Button(Direction::Row) {
    setPadding({14, 10, 8, 10})->setSpacing(12)->setAlignItems(Align::Center);
    setBackground("surface_container_highest", false);
    glideKey = "h:" + std::to_string(n.id) + ":" + std::to_string(n.time);
    if (auto provider = imageFor(n)) {
        auto* img = add<ImageView>(32, provider);
        img->cover = !n.imagePath.empty();
    } else {
        auto* bubble = add<Flex>(Direction::Row);
        pill(bubble->setBackground("primary_container", false))->setJustify(Align::Center);
        bubble->fixedWidth = bubble->fixedHeight = 32;
        bubble->add<Icon>("notifications", 18)->setColor("on_primary_container", false);
    }
    auto* texts = add<Flex>(Direction::Column);
    texts->setSpacing(1)->setAlignItems(Align::Start);
    texts->grow = true;
    // Today's and yesterday's say when; older ones the day too
    char when[32];
    std::tm tm{};
    localtime_r(&n.time, &tm);
    const bool recent = std::time(nullptr) - n.time < 2 * 86400;
    std::strftime(when, sizeof when, recent ? "%H:%M" : "%a %-d %b, %H:%M", &tm);
    texts->add<Label>(std::format("{} · {}", n.app.empty() ? "Notification" : n.app, when))
        ->setSize(12)
        ->setColor("on_surface_variant", false)
        ->setMaxWidth(300);
    texts->add<Label>(n.summary.empty() ? n.body : n.summary)->setSize(14)->setWeight(600)->setMaxWidth(300);
    if (!n.summary.empty() && !n.body.empty())
        texts->add<Label>(n.body)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(300);
    auto* remove = add<Button>();
    pill(remove)->setPadding(Insets(6));
    remove->add<Icon>("close", 16)->setColor("on_surface_variant", false);
    const uint32_t id = n.id;
    const std::time_t time = n.time;
    remove->onClicked = [id, time](uint32_t) { Notifications::get().forget(id, time); };
    // Its app, when one can be found
    const std::string entry = n.desktopEntry, app = n.app;
    onClicked = [entry, app](uint32_t button) {
        if (button != BTN_LEFT) return;
        for (const auto& a : Apps::get().all())
            if ((!entry.empty() && a.id == entry + ".desktop") || (!app.empty() && a.name == app)) return Apps::get().launch(a);
    };
}

// ── NotificationCard ──

NotificationCard::NotificationCard(const Notifications::Notification& n, bool toast, int more)
    : Button(Direction::Column), m_id(n.id) {
    squish = false; // it swipes instead
    setPadding(Insets(16))->setSpacing(10)->setAlignItems(Align::Stretch)->setRadius(Radii(24));
    if (toast) {
        // Toasts float over windows: translucent and blurred like panels
        setBackground(ColorRef("surface_container_high", Theme::get().panelOpacity()), false);
        blurBehind = true;
    } else {
        setBackground("surface_container_highest", false);
    }
    if (n.urgency == Notifications::Critical) setBorder(2, "error");
    fixedWidth = toast ? kToastWidth : std::optional<float>{};

    const uint32_t id = n.id;
    const bool hasDefault = std::ranges::any_of(n.actions, [](auto& a) { return a.key == "default"; });
    onClicked = [id, hasDefault](uint32_t button) {
        if (button == BTN_LEFT && hasDefault) Notifications::get().invoke(id, "default");
        else if (button == BTN_MIDDLE || button == BTN_LEFT) Notifications::get().dismiss(id);
    };

    auto* head = add<Flex>(Direction::Row);
    head->setSpacing(12)->setAlignItems(Align::Start);
    auto provider = imageFor(n);
    if (provider) {
        auto* img = head->add<ImageView>(40, provider);
        img->cover = !n.image.data.empty() || !n.imagePath.empty();
        m_avatar = img;
    } else {
        auto* bubble = head->add<Flex>(Direction::Row);
        pill(bubble->setBackground("primary_container", false))->setJustify(Align::Center);
        bubble->fixedWidth = bubble->fixedHeight = 40;
        bubble->add<Icon>("notifications", 22)->setColor("on_primary_container", false);
        m_avatar = bubble;
    }
    auto* texts = head->add<Flex>(Direction::Column);
    m_parts.push_back(texts);
    texts->setSpacing(2)->setAlignItems(Align::Start);
    texts->grow = true;
    auto* meta = texts->add<Label>(std::format("{} · {}", n.app.empty() ? "Notification" : n.app, ago(n.time)));
    meta->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(260);
    auto* summary = texts->add<Label>(n.summary);
    summary->setSize(15)->setWeight(600)->setMaxWidth(toast ? 280 : 300)->setMaxLines(2);
    // A pile's count: tap to spread it out
    if (more > 0) {
        auto* chip = head->add<Button>();
        pill(chip->setBackground("secondary_container", false))->setPadding({10, 4, 6, 4})->setSpacing(2);
        chip->add<Label>(std::format("+{}", more))->setSize(12)->setWeight(600)->setColor("on_secondary_container", false);
        chip->add<Icon>("expand_more", 18)->setColor("on_secondary_container", false);
        chip->onClicked = [this](uint32_t) {
            if (onExpand) onExpand();
        };
    }
    // Snooze: back later, as new
    auto* later = head->add<Button>();
    pill(later)->setPadding(Insets(4));
    later->add<Icon>("snooze", 18)->setColor("on_surface_variant", false);
    later->tooltip = "Snooze";
    later->onClicked = [this](uint32_t) {
        if (m_snooze) m_snooze->setVisible(!m_snooze->visible());
    };
    auto* close = head->add<Button>();
    pill(close)->setPadding(Insets(4));
    close->add<Icon>("close", 18)->setColor("on_surface_variant", false);
    close->onClicked = [this](uint32_t) { flyOff(1); };

    if (!n.body.empty()) {
        auto* body = add<Label>(n.body);
        body->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(toast ? kToastWidth - 32 : 340)->setMaxLines(4);
        body->alignSelf = Align::Start;
        m_parts.push_back(body);
    }

    std::vector<Notifications::Action> visible;
    for (const auto& a : n.actions)
        if (a.key != "default" && !a.label.empty()) visible.push_back(a);
    if (!visible.empty()) {
        auto* row = add<Flex>(Direction::Row);
        row->setSpacing(8)->setJustify(Align::End);
        m_parts.push_back(row);
        for (const auto& a : visible) {
            auto* b = row->add<Button>();
            pill(b->setBackground("secondary_container", false))->setPadding({16, 8});
            b->add<Label>(a.label)->setSize(13)->setWeight(500)->setColor("on_secondary_container", false);
            b->onClicked = [id, key = a.key](uint32_t) { Notifications::get().invoke(id, key); };
        }
    }

    // Snooze's choices, under it all until asked for
    m_snooze = add<Flex>(Direction::Row);
    m_snooze->setSpacing(8)->setJustify(Align::End);
    m_snooze->wrap = true;
    m_snooze->setVisible(false);
    m_snooze->add<Icon>("snooze", 18)->setColor("on_surface_variant", false);
    // 0: tomorrow at 9, worked out when chosen
    for (const auto& [label, minutes] : {std::pair{"15 min", 15}, std::pair{"1 hour", 60}, std::pair{"3 hours", 180}, std::pair{"Tomorrow 9:00", 0}}) {
        auto* b = m_snooze->add<Button>();
        pill(b->setBackground("secondary_container", false))->setPadding({12, 6});
        b->add<Label>(label)->setSize(12)->setWeight(500)->setColor("on_secondary_container", false);
        const int m = minutes;
        b->onClicked = [this, m](uint32_t) {
            int minutes = m;
            if (minutes == 0) {
                std::time_t now = std::time(nullptr);
                std::tm tm{};
                localtime_r(&now, &tm);
                tm.tm_mday += 1;
                tm.tm_hour = 9;
                tm.tm_min = tm.tm_sec = 0;
                tm.tm_isdst = -1;
                minutes = static_cast<int>((std::mktime(&tm) - now + 59) / 60);
            }
            flyOff(1, minutes);
        };
    }
}

void NotificationCard::onPress(uint32_t button, float x, float y) {
    Button::onPress(button, x, y);
    m_pressing = button == BTN_LEFT;
    m_x0 = x;
    m_dx = 0;
    m_swiping = m_swiped = false;
}

void NotificationCard::onDrag(float x, float) {
    if (!m_pressing) return;
    m_dx = x - m_x0;
    // A few pixels first, so a slightly moving click is still a click
    if (!m_swiping && std::abs(m_dx) < 8) return;
    if (!m_swiping) morphRadius(Radii(36)); // M3 Expressive: picked up, its corners round
    m_swiping = true;
    const float w = std::max(rect().w, 1.0f);
    setTranslateX(m_dx, false);
    setOpacity(std::clamp(1 - std::abs(m_dx) / w * 0.9f, 0.1f, 1.0f), false);
}

void NotificationCard::onRelease() {
    Button::onRelease();
    m_pressing = false;
    if (!m_swiping) return;
    m_swiped = true;
    if (std::abs(m_dx) > rect().w * 0.35f) return flyOff(m_dx < 0 ? -1 : 1);
    // Not far enough: back, with a little bounce, its corners as they were
    setTranslateX(0);
    setOpacity(1);
    morphRadius(Radii(24));
}

void NotificationCard::onClick(uint32_t button) {
    // The end of a swipe isn't a click
    if (m_swiped) {
        m_swiped = false;
        return;
    }
    Button::onClick(button);
}

void NotificationCard::flyOff(float direction, int snoozeMinutes) {
    setTranslateX(direction * (rect().w + 40));
    setOpacity(0);
    const uint32_t id = m_id;
    // By id: dismissed even if the card is rebuilt meanwhile
    EventLoop::get().addTimer(motion::ms(220), [id, snoozeMinutes] {
        if (snoozeMinutes > 0)
            Notifications::get().snooze(id, snoozeMinutes);
        else
            Notifications::get().dismiss(id);
    });
}

void NotificationCard::enter() {
    if (motion::scale <= 0) return;
    if (m_avatar) {
        m_avatar->setOpacity(0, false);
        m_avatar->setScale(0.3f, false);
        m_avatar->setOpacity(1, true, 70);
        m_avatar->setScale(1, true, 70);
    }
    int i = 0;
    for (auto* part : m_parts) {
        const int delay = 90 + 45 * i++;
        part->setMoveSpring(Spring::expressive());
        part->setOpacity(0, false);
        part->setTranslateX(24, false);
        part->setOpacity(1, true, delay);
        part->setTranslateX(0, true, delay);
    }
}

void NotificationCard::onHoverWithin(bool hovered) {
    // Over the Reply button still counts as over the card: keep it up
    if (onHoverChanged) onHoverChanged(hovered);
}

// ── Toasts ──

namespace {

// The surface's root: the column, and over it the toasts leaving, each
// left where it was (they arrange nothing: they only fly off)
class ToastRoot : public Node {
protected:
    SizeF doMeasure(SizeF available) override { return available; }
};
class Leaving : public Node {
protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }
    void doArrange() override {}
};

// Column of toasts that reports where they are, for the input region.
// When one comes or goes the others glide to their new places: each starts
// drawn where it was and springs the difference away.
class ToastColumn : public Flex {
public:
    std::function<void()> onArranged;
    ToastColumn() : Flex(Direction::Column) {}

protected:
    void doArrange() override {
        std::vector<std::pair<Node*, float>> before;
        for (const auto& c : children())
            if (c->rect().w > 0) before.emplace_back(c.get(), c->rect().y + c->translateY());
        Flex::doArrange();
        for (const auto& [node, y] : before) {
            const float dy = y - node->rect().y;
            if (std::abs(dy) < 0.5f) continue;
            node->setTranslateY(dy, false);
            node->setTranslateY(0);
        }
        if (onArranged) onArranged();
    }
};

} // namespace

Toasts::Toasts(std::function<wl::Output*()> output) : m_output(std::move(output)) {
    auto& n = Notifications::get();
    m_posted = n.posted.connect([this](uint32_t id) { show(id); });
    m_closed = n.closed.connect([this](uint32_t id) { hide(id); });
}

Toasts::~Toasts() = default;

void Toasts::ensureSurface() {
    if (m_surface) return;
    wl::Output* out = m_output();
    if (!out) return;
    wl::LayerSurface::Options o;
    o.ns = "material3ui-toasts";
    o.layer = wl::LayerSurface::Layer::Overlay;
    o.anchor = wl::LayerSurface::Top | wl::LayerSurface::Right | wl::LayerSurface::Bottom;
    o.width = static_cast<int>(kToastWidth + kMargin * 2);
    m_surface = std::make_unique<wl::LayerSurface>(out, o);
    auto root = std::make_unique<ToastRoot>();
    auto* column = root->add<ToastColumn>();
    column->setPadding(Insets(kMargin))->setSpacing(10)->setAlignItems(Align::End);
    column->onArranged = [this] { updateInput(); };
    m_column = column;
    m_leaving = root->add<Leaving>();
    m_surface->setInputRegion(std::vector<RectF>{}); // click-through until laid out
    m_surface->setRoot(std::move(root));
}

void Toasts::updateInput() {
    if (!m_surface) return;
    std::vector<RectF> rects;
    for (const auto& t : m_toasts) rects.push_back(t.card->rect());
    m_surface->setInputRegion(rects);
}

void Toasts::arm(Toast& t) {
    if (t.timer) EventLoop::get().cancel(t.timer);
    t.timer = 0;
    const auto* n = Notifications::get().find(t.id);
    if (!n || n->urgency == Notifications::Critical || n->timeoutMs == 0) return;
    // The app's timeout, else Settings → Notifications (0: until closed)
    const int configured = Config::get().value<int>("/notifications/timeout", 6);
    if (n->timeoutMs < 0 && configured <= 0) return;
    const int ms = n->timeoutMs > 0 ? n->timeoutMs : configured * 1000;
    const uint32_t id = t.id;
    t.timer = EventLoop::get().addTimer(ms, [this, id] {
        for (auto& x : m_toasts)
            if (x.id == id) x.timer = 0;
        hide(id);
    });
}

void Toasts::show(uint32_t id) {
    const auto* n = Notifications::get().find(id);
    if (!n) return;
    auto& svc = Notifications::get();
    // In Do not disturb: only critical ones, and the apps allowed through
    if (!svc.getsThrough(*n)) return;
    // The centre is open: it shows the new one already
    if (panels::notificationCentreShown() && n->urgency != Notifications::Critical) return;
    ensureSurface();
    if (!m_column) return;

    // Replaced notifications update in place
    hide(id, false);
    auto card = std::make_unique<NotificationCard>(*n, true);
    NotificationCard* raw = card.get();
    m_column->adopt(std::move(card), 0); // newest on top
    // M3 Expressive: in from the screen's edge on an expressive spring,
    // growing from that side a touch past full size; then its picture
    // pops and its words follow
    raw->setMoveSpring(Spring::expressive());
    raw->setScaleSpring(Spring::expressive());
    raw->scaleOriginX = 1;
    raw->setOpacity(0, false);
    raw->setTranslateX(kToastWidth * 0.7f, false);
    raw->setScale(0.88f, false);
    m_toasts.insert(m_toasts.begin(), Toast{id, raw});
    auto start = [this, raw] {
        // Still up (a notification can be replaced before the first frame)
        if (std::ranges::none_of(m_toasts, [raw](const Toast& t) { return t.card == raw; })) return;
        raw->setOpacity(1);
        raw->setTranslateX(0);
        raw->setScale(1);
        raw->enter();
    };
    // A surface just made: from its first frame
    if (m_surface->rendered()) {
        start();
    } else {
        auto before = std::move(m_surface->onFirstFrame);
        m_surface->onFirstFrame = [before, start] {
            if (before) before();
            start();
        };
    }

    raw->onHoverChanged = [this, id](bool hovered) {
        for (auto& t : m_toasts) {
            if (t.id != id) continue;
            if (hovered && t.timer) {
                EventLoop::get().cancel(t.timer);
                t.timer = 0;
            } else if (!hovered) {
                arm(t);
            }
        }
    };
    arm(m_toasts.front());

    // At most four on screen
    std::vector<uint32_t> extra;
    for (size_t i = 4; i < m_toasts.size(); ++i) extra.push_back(m_toasts[i].id);
    for (auto x : extra) hide(x);
}

void Toasts::hide(uint32_t id, bool animate) {
    auto it = std::ranges::find(m_toasts, id, &Toast::id);
    if (it == m_toasts.end()) return;
    if (it->timer) EventLoop::get().cancel(it->timer);
    NotificationCard* card = it->card;
    m_toasts.erase(it);
    if (!animate || !m_leaving || motion::scale <= 0) {
        if (m_column) m_column->remove(card);
        dropSurfaceIfDone();
        return;
    }
    // Out of the column now, so the rest glide up together while it flies
    // off over them: the way it was swiped, else back to the screen's edge,
    // quicker than it came and without a bounce
    auto node = m_column->release(card);
    if (!node) return dropSurfaceIfDone();
    m_leaving->adopt(std::move(node));
    updateInput();
    const float dir = card->translateX() < -1 ? -1.0f : 1.0f;
    card->setMoveSpring(Spring::spatialFast());
    card->setScaleSpring(Spring::spatialFast());
    card->setFadeSpring(Spring::effectsFast());
    card->setTranslateX(dir * (kToastWidth + kMargin * 2));
    card->setOpacity(0);
    card->setScale(0.92f);
    EventLoop::get().addTimer(ui::motion::ms(240), [this, card] {
        if (!m_leaving) return;
        m_leaving->remove(card);
        dropSurfaceIfDone();
    });
}

void Toasts::dropSurfaceIfDone() {
    if (!m_toasts.empty() || (m_leaving && !m_leaving->children().empty())) return;
    // No toasts: drop the surface entirely
    EventLoop::get().defer([this] {
        if (!m_toasts.empty() || (m_leaving && !m_leaving->children().empty())) return;
        m_column = nullptr;
        m_leaving = nullptr;
        m_surface.reset();
    });
}

} // namespace m3::shell
