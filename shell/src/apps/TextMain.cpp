// material3ui-text: the text editor, as a program of its own (packaging/material3ui-text)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "text";
    app.appId = "material3ui-text";
    app.title = "Text Editor";
    app.icon = "edit_note";
    app.width = 1000;
    app.height = 720;
    app.usage = "usage: material3ui-text [file]   (a file already open comes to the front; launched again, another tab)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::textEditor(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::textGo) shell::panels::textGo(args.empty() ? "" : args[0]);
    };
    app.opensDrops = true;
    app.closed = [] { shell::panels::textEditorClosed(); };
    app.settings = shell::panels::textSettings;
    app.about.summary = "Plain text and code, in tabs.";
    app.about.features = {"Line numbers, auto-indent, Tab and Shift+Tab on selections, comments with Ctrl+/", "Find and replace, go to a line, undo and redo", "Line endings and Latin-1 files kept; big files stay quick"};
    app.about.builtWith = "Pango, the shell's own text engine";
    return apps::run(app, argc, argv);
}
