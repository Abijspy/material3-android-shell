// material3ui-camera: Camera, as a program of its own (packaging/material3ui-camera)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "camera";
    app.appId = "material3ui-camera";
    app.title = "Camera";
    app.icon = "photo_camera";
    app.width = 980;
    app.height = 760;
    app.usage = "usage: material3ui-camera [video]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::cameraApp(card, p, args.empty() ? "" : args[0]); };
    app.settings = shell::panels::cameraSettings;
    app.about.summary = "Photos and videos from your webcam.";
    app.about.features = {"Photos of what's on screen, with a 3 or 10 second timer", "Videos with sound (H.264), the picture still showing while it records", "Mirror the picture, switch between cameras, 480p to 1080p", "The last one a click away, in Photos or Videos"};
    app.about.builtWith = "libmpv (v4l2), FFmpeg for recording";
    return apps::run(app, argc, argv);
}
