#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"
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
    void doArrange() override;

private:
    enum class Menu { None, Session, Power };
    void cycle(std::vector<Choice>& list, size_t& index, int step, ui::Label* label);
    // Greeter: the session chip and power button along the bottom, each
    // opening a card of choices above it
    void buildBottomBar();
    void openMenu(Menu menu);
    void closeMenu();
    // The chosen session on its chip: its compositor's logo (Hyprland's,
    // niri's), else the glyph
    void showSession();

    Options m_options;
    size_t m_userIndex = 0, m_sessionIndex = 0;
    ui::Label *m_name, *m_status, *m_session = nullptr;
    ui::Button *m_sessionChip = nullptr, *m_powerButton = nullptr;
    ui::Icon *m_sessionExpand = nullptr, *m_sessionGlyph = nullptr;
    ui::ImageView* m_sessionLogo = nullptr;
    ui::Node* m_scrim = nullptr; // catches clicks outside an open menu
    ui::Flex* m_menu = nullptr;
    Menu m_open = Menu::None;
    ui::Flex* m_pill;
    ui::Button* m_go;       // submit: an arrow, or the wavy loader while checking
    ui::Icon* m_goArrow;
    ui::Node* m_goLoader;
    void showChecking(bool on);
    ui::Label* m_avatarLetter;
    ui::TextField* m_field;
    bool m_busy = false;
    Connection m_alarms; // something rings: its cards stand in for the widgets
};

} // namespace m3::shell
