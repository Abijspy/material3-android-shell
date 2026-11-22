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
    app.settings = shell::panels::filesSettings;
    app.about.summary = "Your files, folders, drives and servers, in Material3UI's look.";
    app.about.features = {"Grid with previews, or a list with dates and sizes; search, sort, hidden files", "Copy, move, rename, the trash shared with other file managers, and Quick Share", "ZIP and unzip (ZIP, tar, 7z, rar, ISO and more)", "USB drives opened and ejected safely; FTP, SFTP and WebDAV servers", "A preview and properties pane: pictures, videos, text, permissions, sizes"};
    app.about.builtWith = "udisks2, gvfs, libarchive (bsdtar), ffmpeg and ImageMagick for previews";
    return apps::run(app, argc, argv);
}
