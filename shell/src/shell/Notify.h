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
// action buttons, a close button. Clicking it runs the default action.
class NotificationCard : public ui::Button {
public:
    NotificationCard(const Notifications::Notification& n, bool toast);
    uint32_t id() const { return m_id; }
    std::function<void(bool hovered)> onHoverChanged;
    void onHoverWithin(bool hovered) override;

private:
    uint32_t m_id;
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
        bool leaving = false;
    };
    void show(uint32_t id);
    void hide(uint32_t id, bool animate = true);
    void arm(Toast& t);
    void ensureSurface();
    void updateInput();

    std::function<wl::Output*()> m_output;
    std::unique_ptr<wl::LayerSurface> m_surface;
    ui::Flex* m_column = nullptr;
    std::vector<Toast> m_toasts;
    Connection m_posted, m_closed;
};

} // namespace m3::shell
