#pragma once

#include <string>
#include <vector>

namespace m3 {

// Compositor-side effects for the shell's layers and apps: what Settings
// changes there (blur, the cursor, apps blurred behind) goes into niri's
// own config through services/NiriConfig, re-applied as config.json and
// the installed packages change. The shell's surfaces send niri their blur
// regions through ext-background-effect (wl::Surface).
class Effects {
public:
    static Effects& get();

    // The cursor theme in use: the one chosen (Settings → Accessibility),
    // or Bibata when none is (if it's installed); "" for the system's own,
    // chosen or not
    static std::string cursorTheme();
    static constexpr const char* kDefaultCursor = "Bibata-Modern-Classic";
    static bool cursorInstalled(const std::string& theme);

    // Settings → Look & feel → Blur behind apps (/appearance/appBlur): the
    // apps made a little see-through for niri's blur to show, as a regex
    // each for their app id, and how opaque. niri gets background-effect
    // rules for them (services/NiriConfig)
    static std::vector<std::string> blurredApps();
    static double appOpacity();
    // The regex matching exactly these app ids
    static std::string classRegex(const std::vector<std::string>& classes);

private:
    Effects();
    void apply();
};

} // namespace m3
