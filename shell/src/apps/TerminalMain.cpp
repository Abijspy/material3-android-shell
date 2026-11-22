// material3ui-terminal: the terminal, as a program of its own (packaging/material3ui-terminal)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

namespace {
// [folder] [-e command…]
void parse(const std::vector<std::string>& args, std::string& dir, std::vector<std::string>& command) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-e" || args[i] == "--") {
            command.assign(args.begin() + static_cast<long>(i) + 1, args.end());
            return;
        }
        if (args[i] == "--working-directory" && i + 1 < args.size()) dir = args[++i];
        else if (args[i].starts_with("--working-directory=")) dir = args[i].substr(20);
        else if (dir.empty() && !args[i].starts_with("-")) dir = args[i];
    }
}
} // namespace

int main(int argc, char** argv) {
    apps::App app;
    app.id = "terminal";
    app.appId = "material3ui-terminal";
    app.title = "Terminal";
    app.icon = "terminal";
    app.width = 960;
    app.height = 620;
    app.usage = "usage: material3ui-terminal [folder] [-e command…]   (a new tab when one's open)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) {
        std::string dir;
        std::vector<std::string> command;
        parse(args, dir, command);
        shell::panels::terminal(card, p, dir, command);
    };
    app.go = [](const std::vector<std::string>& args) {
        std::string dir;
        std::vector<std::string> command;
        parse(args, dir, command);
        if (shell::panels::terminalGo) shell::panels::terminalGo(dir, command);
    };
    app.closed = [] { shell::panels::terminalClosed(); };
    return apps::run(app, argc, argv);
}
