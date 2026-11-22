// material3ui-viewer: the image viewer, as a program of its own (packaging/material3ui-viewer)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "viewer";
    app.appId = "material3ui-viewer";
    app.title = "Photos";
    app.icon = "image";
    app.width = 1100;
    app.height = 760;
    app.usage = "usage: material3ui-viewer [picture|folder]   (none: the gallery of your Pictures)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::viewer(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::viewerGo && !args.empty()) shell::panels::viewerGo(args[0]);
    };
    app.closed = [] { shell::panels::viewerClosed(); };
    app.settings = shell::panels::viewerSettings;
    app.about.summary = "Your pictures in a gallery, and a viewer for every image format.";
    app.about.features = {"A gallery of a folder's pictures, newest first, with your usual places a click away", "PNG, JPEG, WebP, GIF, AVIF, HEIC, JPEG XL, TIFF, SVG, OpenEXR; PSD, XCF and camera RAW too", "Zoom and pan, the folder's pictures a key away, animated GIF and WebP", "Favourites and albums, without moving a file", "A fullscreen slideshow, in order or shuffled", "The image editor, set as wallpaper, show in Files, move to the trash"};
    app.about.builtWith = "gdk-pixbuf, ImageMagick for the formats it can't read";
    return apps::run(app, argc, argv);
}
