// material3ui-books: Books, as a program of its own (packaging/material3ui-books)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "books";
    app.appId = "material3ui-books";
    app.title = "Books";
    app.icon = "menu_book";
    app.width = 1040;
    app.height = 780;
    app.usage = "usage: material3ui-books [book.epub]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::booksApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::booksGo) shell::panels::booksGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::booksClosed(); };
    app.settings = shell::panels::booksSettings;
    app.about.summary = "Read EPUB books: a library with covers, chapters and contents, and every book reopened where you left it.";
    app.about.features = {"Your library from Books, Documents and Downloads, with covers and how far each is read", "Chapters in a calm column; the contents a click away", "Text size, serif or sans, paper, night or your theme's colours, column width", "Space, \u2192 and Page Down turn the page, on into the next chapter", "Every book reopened where you left it"};
    app.about.builtWith = "bsdtar, Pango";
    return apps::run(app, argc, argv);
}
