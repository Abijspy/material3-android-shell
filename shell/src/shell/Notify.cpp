#include "shell/Notify.h"

#include "core/Config.h"
#include "render/Images.h"
#include "services/Search.h"
#include "ui/Controls.h"
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

// ── NotificationCard ──

NotificationCard::NotificationCard(const Notifications::Notification& n, bool toast)
    : Button(Direction::Column), m_id(n.id) {
    setPadding(Insets(16))->setSpacing(10)->setAlignItems(Align::Stretch)->setRadius(Radii(24));
    setBackground(toast ? "surface_container_high" : "surface_container_highest", false);
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
    } else {
        auto* bubble = head->add<Flex>(Direction::Row);
        pill(bubble->setBackground("primary_container", false))->setJustify(Align::Center);
        bubble->fixedWidth = bubble->fixedHeight = 40;
        bubble->add<Icon>("notifications", 22)->setColor("on_primary_container", false);
    }
    auto* texts = head->add<Flex>(Direction::Column);
    texts->setSpacing(2)->setAlignItems(Align::Start);
    texts->grow = true;
    auto* meta = texts->add<Label>(std::format("{} · {}", n.app.empty() ? "Notification" : n.app, ago(n.time)));
    meta->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(260);
    auto* summary = texts->add<Label>(n.summary);
    summary->setSize(15)->setWeight(600)->setMaxWidth(toast ? 280 : 300)->setMaxLines(2);
    auto* close = head->add<Button>();
    pill(close)->setPadding(Insets(4));
    close->add<Icon>("close", 18)->setColor("on_surface_variant", false);
    close->onClicked = [id](uint32_t) { Notifications::get().dismiss(id); };

    if (!n.body.empty()) {
        auto* body = add<Label>(n.body);
        body->setSize(14)->setColor("on_surface_variant", false)->setMaxWidth(toast ? kToastWidth - 32 : 340)->setMaxLines(4);
        body->alignSelf = Align::Start;
    }

    std::vector<Notifications::Action> visible;
    for (const auto& a : n.actions)
        if (a.key != "default" && !a.label.empty()) visible.push_back(a);
    if (!visible.empty()) {
        auto* row = add<Flex>(Direction::Row);
        row->setSpacing(8)->setJustify(Align::End);
        for (const auto& a : visible) {
            auto* b = row->add<Button>();
            pill(b->setBackground("secondary_container", false))->setPadding({16, 8});
            b->add<Label>(a.label)->setSize(13)->setWeight(500)->setColor("on_secondary_container", false);
            b->onClicked = [id, key = a.key](uint32_t) { Notifications::get().invoke(id, key); };
        }
    }
}

void NotificationCard::onHoverWithin(bool hovered) {
    // Over the Reply button still counts as over the card: keep it up
    if (onHoverChanged) onHoverChanged(hovered);
}

// ── Toasts ──

namespace {

// Column of toasts that reports where they are, for the input region
class ToastColumn : public Flex {
public:
    std::function<void()> onArranged;
    ToastColumn() : Flex(Direction::Column) {}

protected:
    void doArrange() override {
        Flex::doArrange();
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
    auto root = std::make_unique<ToastColumn>();
    root->setPadding(Insets(kMargin))->setSpacing(10)->setAlignItems(Align::End);
    root->onArranged = [this] { updateInput(); };
    m_column = root.get();
    m_surface->setInputRegion(std::vector<RectF>{}); // click-through until laid out
    m_surface->setRoot(std::move(root));
}

void Toasts::updateInput() {
    if (!m_surface) return;
    std::vector<RectF> rects;
    for (const auto& t : m_toasts)
        if (!t.leaving) rects.push_back(t.card->rect());
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
    if (svc.doNotDisturb() && n->urgency != Notifications::Critical) return;
    ensureSurface();
    if (!m_column) return;

    // Replaced notifications update in place
    hide(id, false);
    auto card = std::make_unique<NotificationCard>(*n, true);
    NotificationCard* raw = card.get();
    m_column->adopt(std::move(card), 0); // newest on top
    raw->setOpacity(0, false);
    raw->setTranslateX(kToastWidth / 2, false);
    raw->setOpacity(1);
    raw->setTranslateX(0);
    m_toasts.insert(m_toasts.begin(), Toast{id, raw});

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
    if (it == m_toasts.end() || it->leaving) return;
    if (it->timer) EventLoop::get().cancel(it->timer);
    NotificationCard* card = it->card;
    auto remove = [this, card] {
        std::erase_if(m_toasts, [card](auto& t) { return t.card == card; });
        if (m_column) m_column->remove(card);
        if (m_toasts.empty()) {
            // No toasts: drop the surface entirely
            EventLoop::get().defer([this] {
                if (m_toasts.empty()) {
                    m_column = nullptr;
                    m_surface.reset();
                }
            });
        }
    };
    if (!animate) return remove();
    it->leaving = true;
    updateInput();
    card->setOpacity(0);
    card->setTranslateX(kToastWidth / 2);
    EventLoop::get().addTimer(ui::motion::mediumMs, remove);
}

} // namespace m3::shell
