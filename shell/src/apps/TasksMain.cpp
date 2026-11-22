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
    app.settings = shell::panels::tasksSettings;
    app.about.summary = "What's running, and how hard the computer is working.";
    app.about.features = {"Apps with every process they started; your background processes and the system's", "Stop, resume, end or force kill; end a whole app", "Live graphs: CPU (each core), memory, disks, the network, the GPU", "Login apps switched on and off; systemd services started, stopped, enabled"};
    app.about.builtWith = "/proc and /sys, systemd";
    return apps::run(app, argc, argv);
}
