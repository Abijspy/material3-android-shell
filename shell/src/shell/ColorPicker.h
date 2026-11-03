#pragma once

#include <string>
#include <vector>

namespace m3::wl {
struct Output;
}

namespace m3::shell::colorpicker {

// Picks a colour from anywhere on `output` (Tools → Colour picker, its
// keybind): the screen frozen with grim, a loupe following the pointer with
// the pixels round it magnified and the colour's hex; a click copies it
// (#rrggbb) and keeps it in the recent ones, Escape or a right-click cancels
void start(wl::Output* output);
bool active();
// The last colours picked, newest first ("#1a73e8")
std::vector<std::string> recent();

} // namespace m3::shell::colorpicker
