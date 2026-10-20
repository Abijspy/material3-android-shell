#pragma once

namespace m3::wl {
struct Output;
}

namespace m3::shell {

// Super+Tab: every open window, on every workspace, most recently used
// first, as a row of app icons. Tab and Shift+Tab (or the arrows) move
// along; letting go of Super switches to the one chosen (its workspace
// too), Enter or a click does the same, Escape stays put. A quick
// Super+Tab goes straight back to the previous window.
namespace switcher {
// From the keybind: opens (on `output`) or moves along by `step`
void step(wl::Output* output, int step);
bool isOpen();
} // namespace switcher

} // namespace m3::shell
