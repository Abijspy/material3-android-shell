// material3ui-tasks: the Task Manager, as a program of its own (packaging/material3ui-tasks)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "tasks";
    app.appId = "material3ui-tasks";
    app.title = "Task Manager";
    app.icon = "monitoring";
    app.width = 1040;
    app.height = 720;
    app.usage = "usage: material3ui-tasks [processes|performance|startup|services]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::taskManager(card, p, args.empty() ? "" : args[0]); };
    return apps::run(app, argc, argv);
}
