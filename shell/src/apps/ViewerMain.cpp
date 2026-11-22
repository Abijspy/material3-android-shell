// material3ui-viewer: the image viewer, as a program of its own (packaging/material3ui-viewer)
#include "apps/AppHost.h"
#include "shell/Panels.h"

#include <iostream>

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "viewer";
    app.appId = "material3ui-viewer";
    app.title = "Image Viewer";
    app.icon = "image";
    app.width = 1100;
    app.height = 760;
    app.usage = "usage: material3ui-viewer <picture>   (the folder's others a key away)\n";
    if (argc < 2) {
        std::cerr << app.usage;
        return 2;
    }
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::viewer(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::viewerGo && !args.empty()) shell::panels::viewerGo(args[0]);
    };
    app.closed = [] { shell::panels::viewerClosed(); };
    return apps::run(app, argc, argv);
}
