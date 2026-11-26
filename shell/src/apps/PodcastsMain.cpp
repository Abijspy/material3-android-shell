// material3ui-podcasts: Podcasts, as a program of its own (packaging/material3ui-podcasts)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "podcasts";
    app.appId = "material3ui-podcasts";
    app.title = "Podcasts";
    app.icon = "podcasts";
    app.width = 1080;
    app.height = 760;
    app.usage = "usage: material3ui-podcasts [feed URL]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::podcastsApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::podcastsGo) shell::panels::podcastsGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::podcastsClosed(); };
    app.settings = shell::panels::podcastsSettings;
    app.about.summary = "Subscribe to podcasts and listen: new episodes from all your shows, each one resumed where you left it.";
    app.about.features = {"Shows found in Apple's podcast directory (only your search goes), or by their feed's address", "New episodes from all your shows, and each show's page", "Streams, or downloads to listen offline", "Speed from 0.8\u00d7 to 2\u00d7, skip back and forward, the bar's media controls", "Each episode resumed where you left it, marked played near its end"};
    app.about.builtWith = "libmpv, curl";
    return apps::run(app, argc, argv);
}
