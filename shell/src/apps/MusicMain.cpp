// material3ui-music: Music, as a program of its own (packaging/material3ui-music)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "music";
    app.appId = "material3ui-music";
    app.title = "Music";
    app.icon = "library_music";
    app.width = 1120;
    app.height = 760;
    app.usage = "usage: material3ui-music [song]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::musicApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::musicGo && !args.empty()) shell::panels::musicGo(args[0]);
    };
    app.opensDrops = true;
    app.closed = [] { shell::panels::musicClosed(); };
    app.settings = shell::panels::musicSettings;
    app.about.summary = "Your music library: albums, artists, songs and playlists.";
    app.about.features = {"Albums with their covers, artists, every song, search", "Playlists, a queue to play next or later, shuffle and repeat",
                          "Gapless playback and even loudness (ReplayGain)", "The bar, the lock screen and the lyrics follow along (with mpv-mpris)"};
    app.about.builtWith = "libmpv (FFmpeg), ffprobe for tags, mpv-mpris";
    return apps::run(app, argc, argv);
}
