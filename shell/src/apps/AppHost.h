#pragma once

#include "shell/Popup.h"

#include <functional>
#include <string>
#include <vector>

namespace m3::ui {
class Flex;
}
namespace m3::shell {
class Popup;
}

namespace m3::apps {

// What each of Material3UI's apps (Files, Image Viewer, Videos, Task
// Manager, Terminal) is as a program of its own, in a package of its own:
// one window, in the shell's look and colours, on libmaterial3ui.
//
// One copy runs at a time: launched again, a program hands its arguments
// to the one that's running (its socket in $XDG_RUNTIME_DIR) and leaves,
// and that one raises its window. The shell launches them; they ask the
// shell (`material3ui msg`) for what's the shell's (the wallpaper, the
// image editor, Quick Share) and for each other.
struct App {
    std::string id;                 // "files": its socket, its program (material3ui-files)
    std::string appId, title, icon; // the window's
    int width = 1000, height = 700;
    // Arguments are as given, paths made absolute (from where it was run)
    std::function<void(ui::Flex* card, shell::Popup& popup, const std::vector<std::string>& args)> build;
    std::function<void(const std::vector<std::string>& args)> go; // launched again while open
    std::function<void()> closed;
    // Files dropped on its window open in it, one by one through `go` (a
    // viewer, an editor); off for apps that take drops their own way
    bool opensDrops = false;
    std::string usage; // for --help

    // Its Settings page (groups of rows, config.json under its own key) and
    // its About page, behind the gear in the title bar
    std::function<void(ui::Flex* page)> settings;
    // The same, for pages that need the window (the shell's own Settings pages)
    std::function<void(ui::Flex* page, shell::Popup& popup)> settingsIn;
    struct About {
        std::string summary;               // a sentence: what it is
        std::vector<std::string> features; // what it does, a line each
        std::string builtWith;             // the libraries it stands on
    } about;
};

int run(const App& app, int argc, char** argv);

// The Settings and About sheet over a window: a gear in its title bar
// (added to `opts`), Ctrl+, and Escape. `inner` builds the window itself;
// `app` must outlive the window
using Builder = std::function<void(ui::Flex* card, shell::Popup& popup)>;
Builder withSheet(const App& app, shell::Popup::Options& opts, Builder inner);

// Asks the running shell: `material3ui msg <args…>`
void askShell(const std::vector<std::string>& args);

} // namespace m3::apps
