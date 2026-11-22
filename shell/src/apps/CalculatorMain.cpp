// material3ui-calculator: Calculator, as a program of its own (packaging/material3ui-calculator)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "calculator";
    app.appId = "material3ui-calculator";
    app.title = "Calculator";
    app.icon = "calculate";
    app.width = 720;
    app.height = 680;
    app.usage = "usage: material3ui-calculator [sum]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::calculatorApp(card, p, args.empty() ? "" : args[0]); };
    app.settings = shell::panels::calculatorSettings;
    app.about.summary = "Sums as you type them, scientific keys, and a unit converter.";
    app.about.features = {"The answer as you type, Enter to keep it", "Scientific keys: sin, cos, tan (degrees or radians), ln, log, √, π, e, powers", "Your last 30 sums, a click to use one again", "Units: length, mass, temperature, area, volume, speed, data and time"};
    app.about.builtWith = "the shell's own arithmetic (Spotlight's)";
    return apps::run(app, argc, argv);
}
