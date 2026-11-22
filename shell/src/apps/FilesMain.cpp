// material3ui-files: Files, as a program of its own (packaging/material3ui-files)
#include "apps/AppHost.h"
#include "core/Paths.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "files";
    app.appId = "material3ui-files";
    app.title = "Files";
    app.icon = "folder";
    app.width = 1120;
    app.height = 720;
    app.usage = "usage: material3ui-files [folder|file]   (a file: its folder, with it selected)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::files(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::filesGo && !args.empty()) shell::panels::filesGo(args[0]);
    };
    app.closed = [] { shell::panels::filesClosed(); };
    return apps::run(app, argc, argv);
}
