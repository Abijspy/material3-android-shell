#pragma once

namespace m3::themedicons {

// The shell's app icons in the wallpaper's colours, as Android themes its
// Settings icon: copies in ~/.local/share/icons (ahead of the installed
// ones, for the dock, Spotlight and other launchers), rewritten whenever
// the palette changes
void update();

} // namespace m3::themedicons
