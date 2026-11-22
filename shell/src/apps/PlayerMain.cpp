// material3ui-player: Videos, as a program of its own (packaging/material3ui-player)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "player";
    app.appId = "material3ui-player";
    app.title = "Videos";
    app.icon = "smart_display";
    app.width = 1120;
    app.height = 720;
    app.usage = "usage: material3ui-player [file|URL|subtitles]   (a subtitle file goes with what's playing)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::player(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::playerGo && !args.empty()) shell::panels::playerGo(args[0]);
    };
    app.closed = [] { shell::panels::playerClosed(); };
    return apps::run(app, argc, argv);
}
