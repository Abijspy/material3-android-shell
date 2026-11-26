// material3ui-contacts: Contacts, as a program of its own (packaging/material3ui-contacts)
#include "apps/AppHost.h"
#include "shell/Panels.h"

using namespace m3;

int main(int argc, char** argv) {
    apps::App app;
    app.id = "contacts";
    app.appId = "material3ui-contacts";
    app.title = "Contacts";
    app.icon = "contacts";
    app.width = 980;
    app.height = 720;
    app.usage = "usage: material3ui-contacts [file.vcf]\n";
    app.build = [](ui::Flex* card, shell::Popup& p, const std::vector<std::string>& args) { shell::panels::contactsApp(card, p, args.empty() ? "" : args[0]); };
    app.go = [](const std::vector<std::string>& args) {
        if (shell::panels::contactsGo) shell::panels::contactsGo(args.empty() ? "" : args[0]);
    };
    app.closed = [] { shell::panels::contactsClosed(); };
    app.settings = shell::panels::contactsSettings;
    app.about.summary = "Your address book: people's numbers, emails, addresses and notes, kept as vCards.";
    app.about.features = {"Search by name, number or email; favourites first, then A to Z", "Numbers, emails, address, organisation, birthday, notes and a picture", "Import and export .vcf files (phones, Google Contacts, other address books)", "Your phone's contacts through KDE Connect alongside, read-only, with a copy kept on request", "Copy a number or address, or write an email, in one click"};
    app.about.builtWith = "vCard";
    return apps::run(app, argc, argv);
}
