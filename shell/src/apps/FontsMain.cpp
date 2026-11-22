// material3ui-fonts: Fonts, as a program of its own (packaging/material3ui-fonts)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "fonts";
    app.appId = "material3ui-fonts";
    app.title = "Fonts";
    app.icon = "font_download";
    app.width = 1040;
    app.height = 740;
    app.usage = "usage: material3ui-fonts [font file]   (none: the fonts installed)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::fontsApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::fontsGo) shell::panels::fontsGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::fontsClosed(); };
    app.settings = shell::panels::fontsSettings;
    app.about.summary = "The fonts installed, each in its own face, and font files to look at and install.";
    app.about.features = {"Every family, each shown in itself; search, monospace, yours", "A family's styles, a sample from 12 to 80 px, and its characters", "Type your own text to see it in any font", "Font files from Files or dropped on the window: look, then install for every app", "Remove fonts you installed; copy a name, show the file in Files"};
    app.about.builtWith = "fontconfig, Pango";
    return apps::run(app, argc, argv);
}
