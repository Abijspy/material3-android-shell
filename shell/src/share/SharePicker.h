#pragma once

#include <memory>
#include <string>
#include <vector>

namespace m3::wl {
class LayerSurface;
}

namespace m3::share {

// `material3ui share-picker`: what an app may share of the screen, asked by
// xdg-desktop-portal-hyprland (its screencopy:custom_picker_binary). A card
// over the screen offers each screen (with a preview), each window (from
// $XDPH_WINDOW_SHARING_LIST) or a region to draw, and whether the app may
// share again without asking. The choice goes to stdout as XDPH reads it
// ("[SELECTION]r/screen:DP-1"); closing it shares nothing.
//
// `--umbriel`: the same card for xdg-desktop-portal-umbriel (its
// screencast.chooser_cmd), which writes what may be shared to stdin as one
// line of JSON (the screens, the windows, which kinds) and reads the choice
// back the same way ({"selections": [{"kind": "monitor", "output": …}],
// "dynamic": …}). No region (Umbriel's portal has none); the switch lets
// the share be changed later with Umbriel's screencast keys
class SharePicker {
public:
    // `allowToken`: XDPH passed --allow-token (remembering starts on)
    explicit SharePicker(bool allowToken, bool umbriel = false);
    ~SharePicker();
    bool start();

    struct Window {
        std::string id, cls, title;
    };

private:
    void finish(const std::string& selection);
    void pickRegion();

    bool m_allowToken;
    bool m_umbriel; // speaking xdg-desktop-portal-umbriel's JSON, not XDPH's lines
    bool m_screensAllowed = true, m_windowsAllowed = true;
    struct Screen {
        std::string name, description;
        bool focused = false;
    };
    std::vector<Screen> m_screens; // Umbriel's portal's list (else the compositor's)
    std::vector<Window> m_windows;
    std::unique_ptr<wl::LayerSurface> m_surface;
};

} // namespace m3::share
