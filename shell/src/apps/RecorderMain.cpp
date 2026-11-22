// material3ui-recorder: Sound Recorder, as a program of its own (packaging/material3ui-recorder)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "recorder";
    app.appId = "material3ui-recorder";
    app.title = "Sound Recorder";
    app.icon = "mic";
    app.width = 620;
    app.height = 720;
    app.usage = "usage: material3ui-recorder\\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::recorderApp(card, p, args.empty() ? "" : args[0]); };
    app.settings = shell::panels::recorderSettings;
    app.about.summary = "Voice notes and anything your microphone hears.";
    app.about.features = {"Record in Opus, MP3, FLAC or WAV, from the microphone you choose", "The level live as you record", "Your recordings listed: play, show in Files, Quick Share, trash"};
    app.about.builtWith = "FFmpeg, PipeWire (parec)";
    return apps::run(app, argc, argv);
}
