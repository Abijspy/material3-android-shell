// material3ui-disks: Disks, as a program of its own (packaging/material3ui-disks)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "disks";
    app.appId = "material3ui-disks";
    app.title = "Disks";
    app.icon = "hard_drive";
    app.width = 1060;
    app.height = 740;
    app.usage = "usage: material3ui-disks [space]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::disksApp(card, p, args.empty() ? "" : args[0]); };
    app.settings = shell::panels::disksSettings;
    app.about.summary = "Your drives, their health, and what fills them.";
    app.about.features = {"Each drive with its partitions to scale, their filesystems and free space", "Mount, unmount and power off USB drives and cards", "Rename, check and format partitions (exFAT, FAT32, ext4, NTFS)", "The drive's health from its own self-monitoring (SMART)", "Space used: what fills a folder, biggest first, a click deeper"};
    app.about.builtWith = "udisks2, lsblk, smartmontools, the mkfs tools";
    return apps::run(app, argc, argv);
}
