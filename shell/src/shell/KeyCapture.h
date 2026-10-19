#pragma once

#include "core/EventLoop.h"
#include "ui/Controls.h"
#include "ui/Node.h"

#include <functional>
#include <string>

namespace m3::shell {

// A field that takes a shortcut by pressing it. While it listens, Hyprland
// is switched to an empty submap (material3ui_capture, from shell.lua) so
// its own binds don't take the keys first; Escape, losing focus or ten
// seconds end it, and Hyprland always goes back to its usual binds.
class KeyCapture : public ui::Button {
public:
    explicit KeyCapture(std::string combo);
    ~KeyCapture() override;

    // "SUPER + SHIFT + T", in Hyprland's bind syntax
    std::function<void(const std::string& combo)> onCaptured;

    bool focusable() const override { return true; }
    bool onKey(const ui::KeyEvent& e) override;
    void onFocus(bool focused) override;
    void onClick(uint32_t button) override;

private:
    void listen(bool on);
    void show();
    std::string m_combo;
    bool m_listening = false;
    EventLoop::TimerId m_timeout = 0;
    ui::Label* m_label;
    ui::Icon* m_icon;
};

} // namespace m3::shell
