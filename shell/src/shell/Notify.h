#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
#include "services/Notifications.h"
#include "ui/Node.h"
#include "wayland/LayerSurface.h"

#include <functional>
#include <map>
#include <memory>

namespace m3::shell {

// One notification: icon or image, app and time, summary, wrapped body,
// action buttons, a close button. Clicking it runs the default action;
// swiped sideways it follows the pointer and, let go far enough, flies off
// and is dismissed (else it springs back). `more` > 0 shows a "+N" chip
// that expands a pile of the app's notifications (onExpand).
class NotificationCard : public ui::Button {
public:
    NotificationCard(const Notifications::Notification& n, bool toast, int more = 0);
    uint32_t id() const { return m_id; }
    std::function<void(bool hovered)> onHoverChanged;
    std::function<void()> onExpand;
    void onHoverWithin(bool hovered) override;
    void onPress(uint32_t button, float x, float y) override;
    void onDrag(float x, float y) override;
    void onRelease() override;
    void onClick(uint32_t button) override;
    // Off to the side, then dismissed
    void flyOff(float direction, int snoozeMinutes = 0); // snoozed (not just dismissed) when given
    // A toast arriving: its picture pops, then its words and buttons follow
    void enter();

private:
    uint32_t m_id;
    ui::Node* m_avatar = nullptr;
    ui::Flex* m_snooze = nullptr; // its choices, shown by the snooze button
    std::vector<ui::Node*> m_parts; // the words, the body, the buttons
    float m_x0 = 0, m_dx = 0;
    bool m_pressing = false, m_swiping = false, m_swiped = false;
};

// A dismissed notification in the history: its icon, app and time,
// summary and a line of the body, and a remove button. Opens its app.
class HistoryItem : public ui::Button {
public:
    explicit HistoryItem(const Notifications::Notification& n);
};

// Toasts in the top-right corner of one output.
class Toasts {
public:
    explicit Toasts(std::function<wl::Output*()> output);
    ~Toasts();

private:
    struct Toast {
        uint32_t id;
        NotificationCard* card;
        EventLoop::TimerId timer = 0;
    };
    void show(uint32_t id);
    void hide(uint32_t id, bool animate = true);
    void arm(Toast& t);
    void ensureSurface();
    void updateInput();

    std::function<wl::Output*()> m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    ui::Flex* m_column = nullptr;
    // Toasts leaving: out of the column (the rest close up at once), flying
    // off over it until they're gone
    ui::Node* m_leaving = nullptr;
    std::vector<Toast> m_toasts;
    void dropSurfaceIfDone();
    Connection m_posted, m_closed;
};

} // namespace m3::shell
