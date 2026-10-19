#pragma once

#include <cstdint>
#include <string>

namespace m3::ui {

enum Modifier : uint32_t {
    ModShift = 1,
    ModCtrl = 2,
    ModAlt = 4,
    ModSuper = 8,
};

// A key press (or repeat) after xkb translation. `sym` is an XKB keysym
// (XKB_KEY_Escape, XKB_KEY_a…); `text` is what it types, if anything.
struct KeyEvent {
    uint32_t sym = 0;
    uint32_t baseSym = 0; // the key's own symbol, ignoring Shift and the rest
    std::string text;
    uint32_t mods = 0;
    bool repeat = false;

    bool ctrl() const { return mods & ModCtrl; }
    bool shift() const { return mods & ModShift; }
};

} // namespace m3::ui
