#include "shell/KeyCapture.h"

#include "services/Hyprland.h"
#include "services/Keybinds.h"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

namespace m3::shell {

using namespace m3::ui;

namespace {

bool isModifier(uint32_t sym) {
    switch (sym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
    case XKB_KEY_ISO_Level3_Shift:
    case XKB_KEY_Caps_Lock:
    case XKB_KEY_Num_Lock: return true;
    default: return false;
    }
}

void submap(const char* name) {
    Hyprland::get().request(std::string("dispatch hl.dsp.submap(\"") + name + "\")");
}

} // namespace

KeyCapture::KeyCapture(std::string combo) : Button(Direction::Row), m_combo(std::move(combo)) {
    pill(setBackground("surface_container_highest", false))->setPadding({14, 8})->setSpacing(8);
    pressedRadius = 10;
    m_icon = add<Icon>("keyboard", 18);
    m_label = add<Label>();
    m_label->setSize(14)->setWeight(500);
    show();
}

KeyCapture::~KeyCapture() {
    // Never leave Hyprland without its binds
    if (m_listening) submap("reset");
    if (m_timeout) EventLoop::get().cancel(m_timeout);
}

void KeyCapture::show() {
    m_label->setText(m_listening ? "Press the keys…" : m_combo.empty() ? "Set shortcut" : Keybinds::pretty(m_combo));
    m_icon->setText(m_listening ? "radio_button_checked" : "keyboard");
    m_icon->setColor(m_listening ? "error" : "on_surface_variant");
    setBackground(m_listening ? ColorRef("primary_container") : ColorRef("surface_container_highest"));
}

void KeyCapture::listen(bool on) {
    if (on == m_listening) return;
    m_listening = on;
    if (m_timeout) EventLoop::get().cancel(m_timeout);
    m_timeout = 0;
    submap(on ? "material3ui_capture" : "reset");
    if (on) m_timeout = EventLoop::get().addTimer(10000, [this] {
            m_timeout = 0;
            listen(false);
        });
    show();
}

void KeyCapture::onClick(uint32_t button) {
    if (button != BTN_LEFT) return;
    if (!m_listening) focus();
    listen(!m_listening);
}

void KeyCapture::onFocus(bool focused) {
    Button::onFocus(focused);
    if (!focused) listen(false);
}

bool KeyCapture::onKey(const KeyEvent& e) {
    if (!m_listening) return false;
    if (e.repeat || isModifier(e.sym)) return true;
    if (e.sym == XKB_KEY_Escape && !e.mods) {
        listen(false);
        return true;
    }
    std::string combo;
    if (e.mods & ModSuper) combo += "SUPER + ";
    if (e.mods & ModCtrl) combo += "CTRL + ";
    if (e.mods & ModAlt) combo += "ALT + ";
    if (e.mods & ModShift) combo += "SHIFT + ";
    char name[64];
    xkb_keysym_get_name(e.baseSym ? e.baseSym : e.sym, name, sizeof name);
    std::string key = name;
    // Letters as Hyprland's own binds write them
    if (key.size() == 1) key[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
    m_combo = combo + key;
    listen(false);
    if (onCaptured) onCaptured(m_combo);
    return true;
}

} // namespace m3::shell
