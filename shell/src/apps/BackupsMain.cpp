// material3ui-backups: Backups, as a program of its own (packaging/material3ui-backups)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

namespace m3::shell::panels {
int backupsRunHeadless();
}

int main(int argc, char** argv) {
    // The daily timer: one backup, no window
    if (argc > 1 && std::string(argv[1]) == "--run") return shell::panels::backupsRunHeadless();
    apps::App app;
    app.id = "backups";
    app.appId = "material3ui-backups";
    app.title = "Backups";
    app.icon = "backup";
    app.width = 820;
    app.height = 760;
    app.usage = "usage: material3ui-backups [--run]   (--run: one backup, no window; what the daily timer runs)\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::backupsApp(card, p, args.empty() ? "" : args[0]); };
    app.settings = shell::panels::backupsSettings;
    app.about.summary = "Your folders, kept as dated snapshots on a drive of your choosing.";
    app.about.features = {"Snapshots that only take room for what changed (unchanged files are hard links)", "A USB drive, another disk or a folder; plain folders you can browse in Files", "Every day by itself, with old backups thinned out", "Restore a whole backup, or copy back any file"};
    app.about.builtWith = "std::filesystem (hard links), systemd user timers";
    return apps::run(app, argc, argv);
}
