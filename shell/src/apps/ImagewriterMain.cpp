// material3ui-imagewriter: Image Writer, as a program of its own (packaging/material3ui-imagewriter)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "imagewriter";
    app.appId = "material3ui-imagewriter";
    app.title = "Image Writer";
    app.icon = "usb";
    app.width = 760;
    app.height = 720;
    app.usage = "usage: material3ui-imagewriter [image.iso|image.img(.xz|.gz|.zst)]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::imagewriterApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::imagewriterGo) shell::panels::imagewriterGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::imagewriterClosed(); };
    app.settings = shell::panels::imagewriterSettings;
    app.about.summary = "Writes a disk image (a Linux ISO, a Raspberry Pi image) to a USB drive or memory card, to start a computer from.";
    app.about.features = {"ISO and IMG images, and .xz, .gz and .zst ones unpacked as they're written", "Only USB drives and memory cards; never a drive holding the system, or one too small", "Live progress, speed and time left; the drive read back and compared afterwards", "The image's SHA-256 checked against its sums file or the one you paste", "Eject when it's done; images from Files, a drop, or found in Downloads"};
    app.about.builtWith = "dd, pkexec, lsblk, udisks";
    return apps::run(app, argc, argv);
}
