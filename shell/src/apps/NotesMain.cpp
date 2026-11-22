// material3ui-notes: Notes, as a program of its own (packaging/material3ui-notes)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "notes";
    app.appId = "material3ui-notes";
    app.title = "Notes";
    app.icon = "sticky_note_2";
    app.width = 1080;
    app.height = 720;
    app.usage = "usage: material3ui-notes [new|checklist]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::notesApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::notesGo) shell::panels::notesGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::notesAppClosed(); };
    return apps::run(app, argc, argv);
}
