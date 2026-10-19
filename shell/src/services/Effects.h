#pragma once

#include <string>

namespace m3 {

// Compositor-side effects for the shell's layers. Blur on Hyprland is a
// layer rule, not something a client can ask for: this writes the rules (and
// the blur strength) from config.json into effects_custom.lua, which
// shell.lua reads, and reloads Hyprland when they change. Compositors with
// ext-background-effect get per-surface blur regions from wl::Surface instead.
class Effects {
public:
    static Effects& get();

private:
    Effects();
    void apply();

    std::string m_applied; // legacy configs: what the keywords last set
    int m_cursor = 0;      // the cursor size last applied
    void applyInput();     // Settings → Mouse & touchpad, as input_custom.lua
    void applyAccessibility(); // the magnifier and colour correction
    std::string m_a11yApplied;
    std::string m_inputApplied; // legacy configs: what the keywords last set
};

} // namespace m3
