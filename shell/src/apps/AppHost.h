#pragma once

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
    std::string usage; // for --help
};

int run(const App& app, int argc, char** argv);

// Asks the running shell: `material3ui msg <args…>`
void askShell(const std::vector<std::string>& args);

} // namespace m3::apps
