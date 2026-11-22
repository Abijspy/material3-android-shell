// material3ui-pdf: the PDF viewer and editor, as a program of its own (packaging/material3ui-pdf)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "pdf";
    app.appId = "material3ui-pdf";
    app.title = "PDF";
    app.icon = "picture_as_pdf";
    app.width = 1100;
    app.height = 800;
    app.usage = "usage: material3ui-pdf [file.pdf]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::pdfViewer(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::pdfGo && !args.empty()) shell::panels::pdfGo(args[0]);
    };
    app.closed = [] { shell::panels::pdfClosed(); };
    return apps::run(app, argc, argv);
}
