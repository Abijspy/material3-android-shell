// material3ui-store: Store, as a program of its own (packaging/material3ui-store)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "store";
    app.appId = "material3ui-store";
    app.title = "Store";
    app.icon = "storefront";
    app.width = 1120;
    app.height = 780;
    app.usage = "usage: material3ui-store [updates|search:<words>|category:<name>|<flathub app id>]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::storeApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::storeGo) shell::panels::storeGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::storeClosed(); };
    app.settings = shell::panels::storeSettings;
    app.about.summary = "Find, install and update apps: Flathub's catalogue, your system's repositories and the AUR.";
    app.about.features = {"Flathub's apps by category, featured and verified ones first, with icons, screenshots and descriptions", "Search Flathub, your repositories (pacman) and the AUR at once", "Install, open and remove apps, with one password", "Your installed apps, and every update in one place"};
    app.about.builtWith = "Flatpak and AppStream, pacman, the AUR's RPC";
    return apps::run(app, argc, argv);
}
