// material3ui-mail: Mail, as a program of its own (packaging/material3ui-mail)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "mail";
    app.appId = "material3ui-mail";
    app.title = "Mail";
    app.icon = "mail";
    app.width = 1180;
    app.height = 760;
    app.usage = "usage: material3ui-mail [mailto:address]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::mailApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::mailGo) shell::panels::mailGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::mailClosed(); };
    app.settings = shell::panels::mailSettings;
    app.about.summary = "Your email over IMAP and SMTP: read, reply, forward and write, the password kept in your keyring.";
    app.about.features = {"One account; the servers filled in for Gmail, Outlook, Yahoo, iCloud and Fastmail", "Folders and the latest messages, unread ones marked", "Plain text preferred, HTML shown as text, quotes dimmed; attachments saved to Downloads", "Reply, reply all, forward and write; a copy kept in Sent", "New mail checked every few minutes, with a notification; mailto: links"};
    app.about.builtWith = "curl (IMAP, SMTP), the Secret Service";
    return apps::run(app, argc, argv);
}
