#pragma once

#include "core/EventLoop.h"
#include "ui/Controls.h"
#include "ui/Node.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace m3::shell {

// The lock screen's face, shared with the greeter: wallpaper with a scrim,
// a clock and widgets (lock.clock and lock.widgets in config.json, edited in
// Settings → Lock screen), then avatar, name and a password pill. In greeter
// mode it adds user and session pickers and power buttons.
// One lock-screen widget by id (clock, date, weather…), live and aligned for
// grid column `col`; the lock screen, greeter and lock screen editor share it.
// `clockScale` shrinks the clock (the editor is smaller than the screen).
ui::Node* addLockWidget(ui::Node* parent, const std::string& id, bool greeter, int col, float clockScale = 1);

class LockView : public ui::Flex {
public:
    struct Choice {
        std::string id, label;
    };
    struct Options {
        bool greeter = false;
        std::string wallpaper;
        std::vector<Choice> users;    // greeter: id = username
        std::vector<Choice> sessions; // greeter: id = Exec command
        std::string user;             // initially selected / locked user
    };

    explicit LockView(Options options);
    ~LockView() override;

    // Password (or prompt answer) submitted
    std::function<void(const std::string& answer)> onSubmit;
    std::function<void(const std::string& user)> onUserChanged; // greeter
    std::function<void(const std::string& action)> onPower;     // greeter: suspend, reboot, poweroff

    void setBusy(bool busy);
    void fail(const std::string& message);   // clears the field, shakes
    void setPrompt(const std::string& text, bool secret);
    void setInfo(const std::string& text);
    ui::TextField* field() const { return m_field; }
    const std::string& user() const;
    const std::string& session() const;

protected:
    void doPaint(Renderer& r) override;

private:
    void cycle(std::vector<Choice>& list, size_t& index, int step, ui::Label* label);

    Options m_options;
    size_t m_userIndex = 0, m_sessionIndex = 0;
    ui::Label *m_name, *m_status, *m_session = nullptr;
    ui::Flex* m_pill;
    ui::Label* m_avatarLetter;
    ui::TextField* m_field;
    bool m_busy = false;
};

} // namespace m3::shell
