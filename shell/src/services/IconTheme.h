#pragma once

#include <string>

namespace m3::icontheme {

// Material You icons for the whole desktop: an icon theme ("Material3-You",
// in ~/.local/share/icons) the shell draws itself, each icon a Material
// Symbol in a tonal circle of the wallpaper's palette: folders and places,
// file types, drives and devices, and the common apps. What it doesn't
// draw (other apps, symbolic icons) comes from Adwaita and hicolor, which it
// inherits. Look & feel → Icon theme turns it on; it's drawn again whenever
// the palette changes, and GTK apps and the shell pick it up at once.

inline constexpr const char* kName = "Material3-You";

bool enabled();
void setEnabled(bool on); // on: drawn and made GTK's icon theme; off: the one before put back
// Called at start and on each palette change: drawn again when it's on and
// the colours differ from what it was drawn in
void update();
// How many icons it has (Settings says so)
int count();

} // namespace m3::icontheme
