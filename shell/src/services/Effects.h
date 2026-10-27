#pragma once

#include <string>

namespace m3 {

// Compositor-side effects for the shell's layers. Blur on Hyprland is a
// layer rule, not something a client can ask for: this writes the rules (and
// the blur strength) from config.json into effects_custom.lua, which
// shell.lua reads, and reloads Hyprland when they change. Hyprland shapes
// that blur by the layers' alpha, so they send it no region; compositors
// with only ext-background-effect get per-surface regions from wl::Surface.
class Effects {
public:
    static Effects& get();
    // A colour filter over the Accessibility one for a while ("grayscale"
    // at bedtime); "" ends it
    void setTemporaryFilter(const std::string& filter);

    // The cursor theme in use: the one chosen (Hyprland Settings → Cursor),
    // or Bibata when none is (if it's installed); "" for the system's own,
    // chosen or not
    static std::string cursorTheme();
    static constexpr const char* kDefaultCursor = "Bibata-Modern-Classic";
    static bool cursorInstalled(const std::string& theme);

private:
    std::string m_temporaryFilter;
    Effects();
    void apply();

    std::string m_applied; // legacy configs: what the keywords last set
    int m_cursor = 0;          // the cursor size last applied
    std::string m_cursorTheme; // what setcursor last applied
    void applyCursor();
    void applyInput();     // Settings → Mouse & touchpad, as input_custom.lua
    void applyAccessibility(); // the magnifier and colour correction
    void applySharing();       // Settings → Tools → Screen sharing, as xdph.conf
    std::string m_a11yApplied;
    std::string m_inputApplied; // legacy configs: what the keywords last set
};

} // namespace m3
