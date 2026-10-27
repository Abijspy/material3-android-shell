#include "shell/SoftwarePages.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Process.h"
#include "services/Firmware.h"
#include "services/Network.h"
#include "services/Packages.h"
#include "services/Repos.h"
#include "services/SystemStats.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <ctime>
#include <format>

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// "just now", "5 min ago", "3 h ago", "2 days ago"
std::string ago(std::time_t t) {
    const long s = static_cast<long>(std::time(nullptr) - t);
    if (s < 60) return "just now";
    if (s < 3600) return std::format("{} min ago", s / 60);
    if (s < 86400) return std::format("{} h ago", s / 3600);
    return s < 2 * 86400 ? std::string("yesterday") : std::format("{} days ago", s / 86400);
}

// Settings' other pages, from a click on this one (which a reshow replaces)
void showPage(const std::string& id) {
    EventLoop::get().defer([id] { settingsPage(id); });
}

// A small tonal label at a row's end ("Always on", "Up to date")
void chip(Node* parent, const std::string& text, const char* bg = "secondary_container", const char* fg = "on_secondary_container") {
    auto* c = parent->add<Flex>(Flex::Direction::Row);
    pill(c->setBackground(bg, false))->setPadding({12, 6});
    c->add<Label>(text)->setSize(12)->setWeight(600)->setColor(fg, false);
}

// The task under way (Packages' one at a time), or how the last one ended
void taskStatus(Node* body, const std::string& error) {
    auto& p = Packages::get();
    if (p.busy()) {
        auto* box = body->add<Flex>(Flex::Direction::Column);
        box->setSpacing(8)->setPadding({8, 8})->setAlignItems(Align::Stretch);
        box->add<Label>(p.status())->setSize(14)->setWeight(500)->setMaxWidth(600);
        box->add<WavyBar>();
        return;
    }
    const std::string& text = !error.empty() ? error : p.status();
    if (text.empty()) return;
    const bool bad = !error.empty() || text.starts_with("Didn't") || text.starts_with("Cancelled");
    auto* l = body->add<Label>(text);
    l->setSize(13)->setWeight(500)->setMaxWidth(600)->setMaxLines(3)->setColor(bad ? "error" : "primary", false);
    l->alignSelf = Align::Start;
}

// A labelled field in a form row
TextField* field(Node* parent, const std::string& placeholder, std::string* draft) {
    auto* f = parent->add<TextField>();
    f->setPlaceholder(placeholder);
    f->setText(*draft);
    f->onChanged = [draft](const std::string& t) { *draft = t; };
    return f;
}

// What's typed in the forms, kept while the page rebuilds under it (a
// task's progress) and between visits
struct Drafts {
    bool addingRepo = false, addingRemote = false;
    std::string name, server, key, hold, remoteName, remoteUrl;
    std::string country = "?"; // "?": not picked yet (the mirrors' own, once known)
};
Drafts& drafts() {
    static Drafts d;
    return d;
}

const char* repoAbout(const std::string& name) {
    if (name == "core") return "The base system: the kernel, pacman, systemd";
    if (name == "extra") return "Everything else Arch packages: desktops, apps, libraries";
    if (name == "multilib") return "32-bit libraries, for Steam, Wine and older games";
    if (name == "core-testing") return "core's packages before release: for testing, may break things";
    if (name == "extra-testing") return "extra's packages before release: for testing, may break things";
    if (name == "multilib-testing") return "multilib's packages before release: for testing";
    return "";
}

void reposGroup(Node* page) {
    auto& r = Repos::get();
    auto* g = group(page, "Repositories", "Where pacman gets packages, in the order it looks. Changes ask for your password");
    // Arch's own, in Arch's order, whether the file has them or not
    for (const char* name : {"core-testing", "core", "extra-testing", "extra", "multilib-testing", "multilib"}) {
        const auto& repos = r.conf().repos;
        const auto it = std::ranges::find(repos, std::string(name), &pacmanconf::Repo::name);
        const bool on = it != repos.end() && it->enabled;
        auto row1 = row(g, std::string(name).ends_with("-testing") ? "science" : "deployed_code", name, repoAbout(name));
        if (std::string(name) == "core" || std::string(name) == "extra") {
            chip(row1.row, "Always on");
            continue;
        }
        auto* sw = row1.row->add<Switch>();
        sw->setChecked(on, false);
        const std::string repo = name;
        sw->onToggled = [repo](bool v) { Repos::get().setEnabled(repo, v); };
    }
    // Your own
    for (const auto& repo : r.conf().repos) {
        if (pacmanconf::official(repo.name)) continue;
        const std::string where = !repo.servers.empty() ? repo.servers.front() : !repo.includes.empty() ? repo.includes.front() : "";
        auto rr = row(g, "deployed_code_account", repo.name, where);
        const std::string name = repo.name;
        confirmButton(rr.row, "repo:" + name, "delete", [name] { Repos::get().remove(name); })->tooltip = "Remove";
        auto* sw = rr.row->add<Switch>();
        sw->setChecked(repo.enabled, false);
        sw->onToggled = [name](bool v) { Repos::get().setEnabled(name, v); };
    }
    auto& d = drafts();
    auto add = row(g, "add", "Add a repository", "Your own, or one like chaotic-aur, with its server's address");
    if (!d.addingRepo) {
        pillButton(add.row, "Add", false, [] {
            drafts().addingRepo = true;
            Repos::get().changed.emit();
        }, "add");
        return;
    }
    auto* form = g->add<Flex>(Flex::Direction::Column);
    form->setPadding({20, 8, 20, 14})->setSpacing(8)->setAlignItems(Align::Stretch);
    auto* name = field(form, "Name, as in [name]", &d.name);
    field(form, "Server: https://example.org/$repo/$arch", &d.server);
    field(form, "Its key's fingerprint (optional: fetched and trusted)", &d.key);
    name->focus();
    auto* buttons = form->add<Flex>(Flex::Direction::Row);
    buttons->setSpacing(8)->setJustify(Align::End);
    pillButton(buttons, "Cancel", false, [] {
        drafts().addingRepo = false;
        Repos::get().changed.emit();
    });
    pillButton(buttons, "Add", true, [] {
        auto& dd = drafts();
        Repos::get().add(dd.name, dd.server, dd.key);
        if (Repos::get().error().empty()) {
            dd.addingRepo = false;
            dd.name.clear();
            dd.server.clear();
            dd.key.clear();
        }
    }, "check");
}

void mirrorsGroup(Node* page) {
    auto& r = Repos::get();
    const auto& ms = r.mirrors();
    auto* g = group(page, "Mirrors",
                    ms.empty() ? std::string("None in use: pacman can't download anything")
                               : std::format("{} in use; pacman tries them in order", ms.size()));
    for (size_t i = 0; i < ms.size() && i < 5; ++i) {
        // The same host often twice: over HTTPS and plain HTTP
        const std::string how = ms[i].starts_with("https://") ? "HTTPS" : ms[i].starts_with("http://") ? "HTTP, not encrypted" : "Local";
        auto mr = row(g, "dns", pacmanconf::host(ms[i]), i == 0 ? "Tried first · " + how : how);
        if (ms.size() > 1) {
            const std::string url = ms[i];
            confirmButton(mr.row, "mirror:" + url, "delete", [url] { Repos::get().removeMirror(url); })->tooltip = "Remove";
        }
    }
    if (ms.size() > 5) {
        auto* more = g->add<Flex>(Flex::Direction::Row);
        more->setPadding({20, 8});
        caption(more, std::format("and {} more", ms.size() - 5));
    }

    // The fastest near you: archlinux.org's up-to-date mirrors, timed here
    auto& d = drafts();
    auto find = row(g, "speed", "Find the fastest", "Times the best-kept mirrors from here, then lists the quickest");
    auto* box = g->add<Flex>(Flex::Direction::Column);
    box->setAlignItems(Align::Stretch);
    if (!r.statusLoaded()) {
        if (!r.loadingStatus()) pillButton(find.row, "Look up", false, [] { Repos::get().loadStatus(); }, "travel_explore");
    } else {
        if (d.country == "?") d.country = r.currentCountry();
        std::vector<std::pair<std::string, std::string>> items = {{"", "Worldwide"}};
        std::string shown = "Worldwide";
        for (const auto& c : r.countries()) {
            items.emplace_back(c.code, std::format("{} ({})", c.name, c.mirrors));
            if (c.code == d.country) shown = c.name;
        }
        dropdown(find.row, box, shown, items, [](const std::string& code) {
            drafts().country = code;
            Repos::get().changed.emit();
        });
        if (!r.ranking()) pillButton(find.row, "Find", true, [] { Repos::get().rank(drafts().country); }, "bolt");
    }
    if (!r.rankStatus().empty()) {
        auto* st = box->add<Flex>(Flex::Direction::Column);
        st->setPadding({20, 6, 20, 12})->setSpacing(8)->setAlignItems(Align::Stretch);
        st->add<Label>(r.rankStatus())->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(600);
        if (r.ranking()) st->add<WavyBar>();
    }
    if (!r.ranked().empty()) {
        for (const auto& m : r.ranked())
            row(box, "bolt", pacmanconf::host(m.url), std::format("{}/s · {}", formatBytes(static_cast<uint64_t>(m.speed)), m.country));
        auto* use = box->add<Flex>(Flex::Direction::Row);
        use->setPadding({20, 6, 20, 14})->setJustify(Align::End);
        pillButton(use, "Use these", true, [] { Repos::get().useRanked(); }, "check");
    }
}

void optionsGroups(Node* page) {
    auto& r = Repos::get();
    auto* dl = group(page, "Downloads");
    auto par = row(dl, "download", "At the same time", "How many packages download at once");
    int current = r.conf().parallelDownloads;
    std::vector<std::pair<int, std::string>> counts = {{1, "1"}, {3, "3"}, {5, "5"}, {10, "10"}};
    if (std::ranges::none_of(counts, [&](const auto& c) { return c.first == current; })) counts.emplace_back(current, std::to_string(current));
    connectedGroup<int>(par.row, current, counts, [](int n) { Repos::get().setParallelDownloads(n); });

    auto* held = group(page, "Held back", "Left out of updates until you let them (pacman's IgnorePkg)");
    for (const auto& pkg : r.conf().ignored) {
        auto hr = row(held, "pause_circle", pkg);
        pillButton(hr.row, "Let it update", false, [pkg] { Repos::get().hold(pkg, false); });
    }
    auto* add = held->add<Flex>(Flex::Direction::Row);
    add->setPadding({20, 10})->setSpacing(8);
    auto* f = field(add, "A package to hold back, as linux or nvidia", &drafts().hold);
    f->grow = true;
    auto hold = [] {
        auto& d = drafts();
        if (d.hold.empty()) return;
        Repos::get().hold(d.hold, true);
        if (Repos::get().error().empty()) d.hold.clear();
    };
    f->onSubmit = [hold](const std::string&) { hold(); };
    pillButton(add, "Hold", false, hold, "pause");
}

void flatpakGroup(Node* page) {
    auto& r = Repos::get();
    auto* g = group(page, "Flatpak", "Remotes Flatpak installs apps from");
    if (!r.flatpak()) {
        auto none = row(g, "apps", "Flatpak isn't installed", "Apps from Flathub, each in its own sandbox");
        pillButton(none.row, "Install", true, [] {
            Packages::get().runAsRoot("Installing Flatpak…", "pacman -S --needed --noconfirm flatpak", "Installed Flatpak: add Flathub to get apps");
        }, "download");
        return;
    }
    bool flathub = false;
    for (const auto& remote : r.remotes()) {
        flathub = flathub || remote.name == "flathub";
        auto rr = row(g, "cloud", remote.title, remote.url + (remote.user ? " · yours only" : ""));
        confirmButton(rr.row, "remote:" + remote.name, "delete", [remote] { Repos::get().removeRemote(remote); })->tooltip = "Remove";
        auto* sw = rr.row->add<Switch>();
        sw->setChecked(remote.enabled, false);
        sw->onToggled = [remote](bool v) { Repos::get().setRemoteEnabled(remote, v); };
    }
    if (!flathub) {
        auto fh = row(g, "storefront", "Flathub", "Where most Flatpak apps are published");
        pillButton(fh.row, "Add", true, [] { Repos::get().addFlathub(); }, "add");
    }
    auto& d = drafts();
    auto add = row(g, "add", "Add a remote", "Its name and address (a .flatpakrepo file's, or the repo's)");
    if (!d.addingRemote) {
        pillButton(add.row, "Add", false, [] {
            drafts().addingRemote = true;
            Repos::get().changed.emit();
        }, "add");
        return;
    }
    auto* form = g->add<Flex>(Flex::Direction::Column);
    form->setPadding({20, 8, 20, 14})->setSpacing(8)->setAlignItems(Align::Stretch);
    field(form, "Name", &d.remoteName)->focus();
    field(form, "https://…/repo.flatpakrepo", &d.remoteUrl);
    auto* buttons = form->add<Flex>(Flex::Direction::Row);
    buttons->setSpacing(8)->setJustify(Align::End);
    pillButton(buttons, "Cancel", false, [] {
        drafts().addingRemote = false;
        Repos::get().changed.emit();
    });
    pillButton(buttons, "Add", true, [] {
        auto& dd = drafts();
        Repos::get().addRemote(dd.remoteName, dd.remoteUrl);
        if (Repos::get().error().empty()) {
            dd.addingRemote = false;
            dd.remoteName.clear();
            dd.remoteUrl.clear();
        }
    }, "check");
}

} // namespace

void repositoriesTab(Flex* body) {
    auto* live = body->add<Live>();
    live->setSpacing(0);
    live->watch(Repos::get().changed);
    live->sync = [live] {
        live->clearChildren();
        taskStatus(live, Repos::get().error());
        reposGroup(live);
        mirrorsGroup(live);
        optionsGroups(live);
        flatpakGroup(live);
        if (Repos::get().hasBackup()) {
            auto* g = group(live, "Undo");
            auto u = row(g, "undo", "pacman.conf before the last change", "Put it back (and the current one aside, to undo this too)");
            pillButton(u.row, "Restore", false, [] { Repos::get().restoreBackup(); }, "settings_backup_restore");
        }
    };
    live->sync();
}

void updateSettings(Flex* body) {
    auto& p = Packages::get();
    auto& fw = Firmware::get();
    auto& c = Config::get();

    // Firmware: its own page
    auto* f = group(body, "Firmware", "Your devices' own software: the BIOS, SSDs, docks, touchpads");
    std::string about = "Install fwupd to update it";
    if (fw.installed()) {
        const int n = fw.updateCount();
        about = fw.checking() ? "Checking…" : fw.lastChecked() == 0 ? "Not checked yet"
              : n == 0 ? "Up to date" : std::format("{} update{} for your devices", n, n == 1 ? "" : "s");
        if (fw.needsReboot()) about = "Restart to finish updating";
    }
    auto fr = row(f, "developer_board", "Firmware updates", about);
    pillButton(fr.row, "Open", false, [] { showPage("firmware"); }, "chevron_right");

    // Automatic checks: when, and what they take in
    auto* g = group(body, "Automatic updates", "Checks in the background; installing is always up to you");
    auto every = row(g, "schedule", "Check for updates",
                     p.lastChecked() ? "Last checked " + ago(p.lastChecked()) : std::string("Not checked yet"));
    connectedGroup<int>(every.row, c.value<int>("/updates/checkEvery", 24), {{0, "Off"}, {6, "6 h"}, {24, "Daily"}, {168, "Weekly"}},
                        [](int h) { Config::get().set("/updates/checkEvery", h); });
    auto toggle = [g](const std::string& icon, const std::string& title, const std::string& sub, const char* key) {
        auto r = row(g, icon, title, sub);
        auto* sw = r.row->add<Switch>();
        sw->setChecked(Config::get().value<bool>(key, true), false);
        sw->onToggled = [key](bool on) {
            Config::get().set(key, on);
            Packages::get().check(); // what's pending changes with it
        };
    };
    toggle("notifications", "Tell me about new ones", "A notification when a check finds updates", "/updates/notify");
    toggle("deployed_code_account", "AUR updates",
           p.aurHelper().empty() ? std::string("No AUR helper (yay or paru) found") : "Checked and installed with " + p.aurHelper(), "/updates/aur");
    if (Repos::get().flatpak()) toggle("apps", "Flatpak updates", "Apps from Flathub and your other remotes", "/updates/flatpak");
    if (fw.installed()) toggle("developer_board", "Firmware updates", "Checked with the others (installed from Firmware)", "/updates/firmware");
}

void firmwarePage(Flex* page, Popup&) {
    auto* live = page->add<Live>();
    live->setSpacing(0);
    live->watch(Firmware::get().changed);
    live->watch(Packages::get().changed);
    live->watch(Power::get().changed);
    live->sync = [live] {
        live->clearChildren();
        auto& fw = Firmware::get();
        auto& p = Packages::get();

        if (!fw.installed()) {
            // Offered, never installed behind your back
            auto* hero = live->add<Flex>(Flex::Direction::Column);
            hero->setPadding(Insets(20))->setSpacing(14)->setRadius(Radii(24))->setBackground("surface_container_high", false);
            hero->setAlignItems(Align::Stretch);
            auto* top = hero->add<Flex>(Flex::Direction::Row);
            top->setSpacing(16);
            top->add<Icon>("developer_board", 36)->setColor("primary", false);
            auto* texts = top->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(4);
            texts->grow = true;
            texts->add<Label>("Firmware updates need fwupd")->setSize(18)->setWeight(600);
            texts->add<Label>("fwupd finds updates for your computer's firmware (its BIOS or UEFI, SSDs, docks, touchpads, webcams and more) "
                              "on the LVFS, where vendors publish them, and installs them.")
                ->setSize(13)
                ->setMaxWidth(560)
                ->setMaxLines(4)
                ->setColor("on_surface_variant", false);
            if (p.busy()) {
                hero->add<Label>(p.status())->setSize(14)->setWeight(500);
                hero->add<WavyBar>();
            } else {
                if (!p.status().empty() && (p.status().starts_with("Didn't") || p.status().starts_with("Cancelled")))
                    hero->add<Label>(p.status())->setSize(13)->setColor("error", false)->setMaxWidth(560)->setMaxLines(2);
                auto* actions = hero->add<Flex>(Flex::Direction::Row);
                actions->setJustify(Align::End);
                pillButton(actions, "Install fwupd", true, [] {
                    Packages::get().runAsRoot("Installing fwupd…", "pacman -S --needed --noconfirm fwupd", "Installed fwupd");
                }, "download");
            }
            return;
        }

        // Installed, waiting for a restart (system firmware goes in as it boots)
        if (fw.needsReboot()) {
            auto* banner = live->add<Flex>(Flex::Direction::Row);
            banner->setPadding(Insets(20))->setSpacing(16)->setRadius(Radii(24))->setBackground("tertiary_container", false);
            banner->add<Icon>("restart_alt", 32)->setColor("on_tertiary_container", false);
            auto* texts = banner->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(2);
            texts->grow = true;
            std::string names;
            for (const auto& d : fw.devices())
                if (d.needsReboot()) names += (names.empty() ? "" : ", ") + d.name;
            texts->add<Label>("Restart to finish updating")->setSize(18)->setWeight(600)->setColor("on_tertiary_container", false);
            texts->add<Label>(names + " install as the computer starts")
                ->setSize(13)
                ->setMaxWidth(440)
                ->setMaxLines(2)
                ->setColor("on_tertiary_container", false);
            pillButton(banner, "Restart now", true, [] { spawn("systemctl reboot"); }, "restart_alt");
            live->add<Node>()->minHeight = 12;
        }

        // Where things stand, and the actions
        const int n = fw.updateCount();
        const bool working = fw.checking() || fw.updating();
        auto* hero = live->add<Flex>(Flex::Direction::Column);
        hero->setPadding(Insets(20))->setSpacing(14)->setRadius(Radii(24))->setBackground("surface_container_high", false);
        hero->setAlignItems(Align::Stretch);
        auto* top = hero->add<Flex>(Flex::Direction::Row);
        top->setSpacing(16);
        if (working) top->add<WavyLoader>(36);
        else top->add<Icon>(n ? "system_update_alt" : "verified", 36)->setColor("primary", false);
        auto* texts = top->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(2);
        texts->grow = true;
        const std::string status = fw.updating() ? fw.status()
                                 : fw.checking() ? "Checking for firmware updates…"
                                 : !fw.error().empty() && fw.devices().empty() ? fw.error()
                                 : fw.lastChecked() == 0 ? "Not checked yet"
                                 : n == 0 ? "Your firmware is up to date"
                                 : std::format("{} firmware update{}", n, n == 1 ? "" : "s");
        texts->add<Label>(status)->setSize(18)->setWeight(600)->setMaxWidth(560)->setMaxLines(2);
        std::time_t listed = 0;
        for (const auto& r : fw.remotes())
            if (r.enabled && r.kind == "download") listed = std::max(listed, r.modified);
        std::string detail = std::format("{} device{}", fw.devices().size(), fw.devices().size() == 1 ? "" : "s");
        if (listed) detail += " · the LVFS's list from " + ago(listed);
        if (!fw.error().empty() && !fw.devices().empty()) detail += " · " + fw.error();
        texts->add<Label>(detail)->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(560)->setMaxLines(2);
        if (!fw.updating() && !fw.status().empty()) {
            const bool bad = fw.status().starts_with("Didn't") || fw.status().starts_with("Cancelled");
            texts->add<Label>(fw.status())->setSize(13)->setColor(bad ? "error" : "primary", false)->setMaxWidth(560)->setMaxLines(3);
        }
        if (working) hero->add<WavyBar>();
        auto* actions = hero->add<Flex>(Flex::Direction::Row);
        actions->setSpacing(8)->setJustify(Align::End);
        actions->setVisible(!working);
        pillButton(actions, "Check now", false, [] { Firmware::get().check(true); }, "refresh");
        if (n > 1) pillButton(actions, "Update all", true, [] { Firmware::get().update(""); }, "download");

        // The updates: each device's newest release, its notes, and Update
        const bool onBattery = Power::get().hasBattery() && !Power::get().pluggedIn();
        if (n > 0) {
            auto* g = group(live, std::format("Updates · {}", n), "Installed version → new version");
            for (const auto& d : fw.devices()) {
                if (d.releases.empty() || !d.releases.front().upgrade) continue;
                const auto& rel = d.releases.front();
                auto r = row(g, d.symbol(), d.name, rel.summary.empty() ? d.vendor : rel.summary);
                auto* versions = r.row->add<Flex>(Flex::Direction::Row);
                versions->setSpacing(8);
                versions->add<Label>(d.version.empty() ? "—" : d.version)->setSize(13)->setMaxWidth(120)->setColor("on_surface_variant", false);
                versions->add<Icon>("arrow_forward", 16)->setColor("on_surface_variant", false);
                auto* to = versions->add<Flex>(Flex::Direction::Row);
                pill(to->setBackground("primary_container", false))->setPadding({10, 4});
                to->add<Label>(rel.version)->setSize(13)->setWeight(600)->setMaxWidth(120)->setColor("on_primary_container", false);
                const std::string id = d.id;
                if (!working) pillButton(r.row, "Update", true, [id] { Firmware::get().update(id); }, "download");
                // The notes, and what to know first
                auto* notes = g->add<Flex>(Flex::Direction::Column);
                notes->setPadding({58, 0, 20, 14})->setSpacing(6)->setAlignItems(Align::Start);
                auto* tags = notes->add<Flex>(Flex::Direction::Row);
                tags->setSpacing(6);
                if (rel.urgency == "high" || rel.urgency == "critical")
                    chip(tags, rel.urgency == "critical" ? "Critical" : "Important", "error_container", "on_error_container");
                if (rel.size) chip(tags, formatBytes(rel.size), "surface_container_highest", "on_surface_variant");
                if (rel.remote == "lvfs-testing") chip(tags, "Testing", "tertiary_container", "on_tertiary_container");
                tags->setVisible(!tags->children().empty());
                if (!rel.description.empty())
                    notes->add<Label>(rel.description)->setSize(13)->setMaxWidth(540)->setMaxLines(6)->setColor("on_surface_variant", false);
                // What fwupd says stands in its way now, else what to know
                std::string problems;
                for (const auto& pr : d.problems) problems += (problems.empty() ? "" : " · ") + Firmware::problemText(pr);
                std::string know;
                if (d.appliesOnRestart()) know = "Goes in as the computer restarts";
                if (d.has("require-ac")) know += std::string(know.empty() ? "Keep" : ": keep") + " the charger plugged in";
                if (!problems.empty())
                    notes->add<Label>("Can't update yet: " + problems)->setSize(12)->setWeight(500)->setMaxWidth(540)->setColor("error", false);
                else if (!know.empty())
                    notes->add<Label>(know)->setSize(12)->setWeight(500)->setMaxWidth(540)->setColor(onBattery && d.has("require-ac") ? "error" : "primary", false);
            }
        }

        // Every device fwupd knows
        auto* devices = group(live, "Your devices", "Firmware fwupd can see, and the version each runs");
        for (const auto& d : fw.devices()) {
            std::string sub = d.vendor;
            if (!d.version.empty()) sub += (sub.empty() ? "Version " : " · ") + d.version;
            if (!d.updatable()) sub += sub.empty() ? "Can't be updated here" : " · can't be updated here";
            auto r = row(devices, d.symbol(), d.name, sub);
            if (d.updateState == "failed" && !d.updateError.empty()) {
                chip(r.row, "Last update failed", "error_container", "on_error_container");
                r.row->tooltip = d.updateError;
            } else if (!d.releases.empty() && d.releases.front().upgrade) {
                chip(r.row, "Update available", "primary_container", "on_primary_container");
            } else if (d.updatable() && fw.lastChecked()) {
                chip(r.row, "Up to date");
            }
        }
        if (fw.devices().empty() && !fw.checking()) {
            auto* none = devices->add<Flex>(Flex::Direction::Row);
            none->setPadding({20, 14});
            caption(none, "None found yet");
        }

        // Where the updates come from
        auto* sources = group(live, "Where updates come from", "Turning one on or off asks for your password");
        for (const auto& r : fw.remotes()) {
            // The embargo list is vendors' own, behind a login
            if (r.kind != "download" || r.id.find("embargo") != std::string::npos) continue;
            const bool testing = r.id.find("testing") != std::string::npos;
            std::string sub = testing ? "Updates before release, for testing" : "Vendors publish their firmware here";
            if (r.enabled && r.modified) sub += " · list from " + ago(r.modified);
            auto rr = row(sources, testing ? "science" : "cloud_download", r.title, sub);
            auto* sw = rr.row->add<Switch>();
            sw->setChecked(r.enabled, false);
            const std::string id = r.id;
            sw->onToggled = [id](bool on) { Firmware::get().setRemote(id, on); };
        }
        auto more = row(sources, "schedule", "Checking automatically", Packages::includesFirmware()
                                                                          ? "With the other updates (Software & updates)"
                                                                          : "Off: turn it on in Software & updates");
        pillButton(more.row, "Settings", false, [] { showPage("packages"); }, "chevron_right");
    };
    live->sync();
    auto& fw = Firmware::get();
    if (fw.installed() && fw.lastChecked() == 0) fw.check(true);
}

} // namespace m3::shell::panels
