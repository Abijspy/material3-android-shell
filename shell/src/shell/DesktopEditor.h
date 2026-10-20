#pragma once

#include "core/Signal.h"

namespace m3::wl {
struct Output;
}

namespace m3::shell {

// Edit mode for the desktop: over the screen, its widgets and shortcuts as
// cards to drag into place (snapping to a grid) or remove, and a toolbar to
// add widgets, apps and files or folders. Each change is saved at once, so
// the desktop beneath follows. Done or Escape ends it.
namespace desktopeditor {
void open(wl::Output* output);
void close();
bool isOpen();
// Opened or closed (the desktop hides its own items meanwhile)
Signal<>& changed();
// The desktop's right-click menu, at (x, y) on that output
void menu(wl::Output* output, float x, float y);
} // namespace desktopeditor

} // namespace m3::shell
