// Settings → Material3UI apps: the shell's own apps, each a package of its
// own (material3ui-files…). Those removed (pacman -R, or never installed)
// can come back here: the package of the release that's installed is
// downloaded from GitHub (or taken from the cache) and installed with one
// password. Updates then keep it, as they do every app that's installed.
#include "shell/ShellAppsPage.h"

#include "build_config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "services/Packages.h"
#include "shell/PanelKit.h"
#include "shell/Panels.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <sys/utsname.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

struct ShellApp {
    const char* id;
    const char* title;
    const char* what;
    const char* symbol; // when its icon left with it
};

// Every app Material3UI ships, in the order update.sh knows them
const ShellApp kApps[] = {
    {"files", "Files", "The file manager", "folder"},
    {"viewer", "Photos", "Pictures, albums and slideshows", "image"},
    {"player", "Videos", "The video player", "movie"},
    {"music", "Music", "Your music library and player", "library_music"},
    {"tasks", "Task Manager", "Apps, processes and performance", "monitoring"},
    {"terminal", "Terminal", "Tabs and split panes", "terminal"},
    {"notes", "Notes", "Notes, checklists and reminders", "sticky_note_2"},
    {"pdf", "PDF", "Read, mark up and sign PDFs", "picture_as_pdf"},
    {"text", "Text Editor", "Text and code, with highlighting", "edit_note"},
    {"disks", "Disks", "Drives, partitions and space", "hard_drive"},
    {"camera", "Camera", "Photos and videos from the webcam", "photo_camera"},
    {"backups", "Backups", "Snapshots of your files", "backup"},
    {"recorder", "Sound Recorder", "Voice notes and recordings", "mic"},
    {"calculator", "Calculator", "Sums and unit conversions", "calculate"},
    {"fonts", "Fonts", "See and install fonts", "font_download"},
    {"imagewriter", "Image Writer", "ISO images onto USB drives", "usb"},
    {"store", "Store", "Find, install and update apps", "storefront"},
    {"podcasts", "Podcasts", "Subscribe to shows and listen", "podcasts"},
};

struct AppsState {
    std::set<std::string> packages; // material3ui-* installed, by pacman
    bool read = false;
    std::string busy;                       // the app id being fetched ("*": all)
    std::map<std::string, std::string> note; // why one couldn't come back
    Signal<> changed;
};

std::shared_ptr<AppsState> appsState() {
    static auto st = std::make_shared<AppsState>();
    return st;
}

bool installed(const AppsState& st, const std::string& id) {
    // M3_TEST_MISSING=music,disks: shown as removed (for testing the page)
    if (const char* t = std::getenv("M3_TEST_MISSING"); t && ("," + std::string(t) + ",").find("," + id + ",") != std::string::npos) return false;
    return st.packages.contains("material3ui-" + id) || !ownCommand("material3ui-" + id).empty();
}

void readInstalled(const std::shared_ptr<AppsState>& st) {
    m3::run({"pacman", "-Qq"}, [st](int, const std::string& out) {
        st->packages.clear();
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);)
            if (l.starts_with("material3ui")) st->packages.insert(l);
        st->read = true;
        st->changed.emit();
    });
}

std::string arch() {
    utsname u{};
    return uname(&u) == 0 ? u.machine : "x86_64";
}

// The release's package for an app, as build-pkg.sh names it
std::string packageName(const std::string& id) {
    std::string ver = M3_VERSION;
    std::erase(ver, '-');
    return std::format("material3ui-{}-{}-1-{}.pkg.tar.zst", id, ver, arch());
}

std::string cacheDir() {
    return paths::home() + "/.cache/material3ui/packages";
}

// Fetched (or already here), then all installed in one go: one password
void reinstall(const std::shared_ptr<AppsState>& st, const std::vector<std::string>& ids) {
    if (ids.empty() || !st->busy.empty() || Packages::get().busy()) return;
    st->busy = ids.size() == 1 ? ids.front() : "*";
    for (const auto& id : ids) st->note.erase(id);
    st->changed.emit();
    std::error_code ec;
    fs::create_directories(cacheDir(), ec);
    // One shell script: each package from the cache, pacman's cache, or the
    // release; the ones that came, printed
    std::string script = "cd \"$1\" || exit 1; shift; tag=\"$1\"; shift; for f in \"$@\"; do "
                         "if [ -s \"$f\" ]; then echo \"ok $f\"; continue; fi; "
                         "c=$(ls /var/cache/pacman/pkg/\"$f\" 2>/dev/null | head -n 1); if [ -n \"$c\" ]; then cp \"$c\" . && echo \"ok $f\" && continue; fi; "
                         "if curl -fsSL --retry 2 -o \"$f.part\" \"https://github.com/Abijspy/material3-android-shell/releases/download/$tag/$f\"; then "
                         "mv \"$f.part\" \"$f\" && echo \"ok $f\"; else rm -f \"$f.part\"; echo \"missing $f\"; fi; done";
    std::vector<std::string> cmd = {"sh", "-c", script, "sh", cacheDir(), std::string("v") + M3_VERSION};
    for (const auto& id : ids) cmd.push_back(packageName(id));
    m3::run(cmd, [st, ids](int, const std::string& out) {
        std::vector<std::string> files, gotIds;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
            const bool ok = l.starts_with("ok ");
            const std::string f = l.substr(l.find(' ') + 1);
            for (const auto& id : ids) {
                if (f != packageName(id)) continue;
                if (ok) files.push_back(cacheDir() + "/" + f), gotIds.push_back(id);
                else
                    st->note[id] = std::format("Not in the {} release (or you're offline): it comes with the next update", M3_VERSION);
            }
        }
        if (files.empty()) {
            st->busy.clear();
            st->changed.emit();
            return;
        }
        // Package files only, as root: quoted for its shell
        std::string args;
        for (const auto& f : files) {
            std::string q = "'";
            for (char c : f) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
            args += " " + q + "'";
        }
        const std::string what = gotIds.size() == 1 ? std::string("the app") : std::format("{} apps", gotIds.size());
        Packages::get().runAsRoot("Reinstalling " + what + "…", "pacman -U --noconfirm --needed" + args, "Reinstalled " + what, [st, gotIds](bool ok) {
            st->busy.clear();
            if (!ok)
                for (const auto& id : gotIds) st->note[id] = "It didn't install: Software & updates says why";
            readInstalled(st);
            st->changed.emit();
        });
    });
}

} // namespace

void shellAppsPage(Flex* page, Popup&) {
    auto st = appsState();
    readInstalled(st);
    auto* live = page->add<Live>();
    live->setSpacing(12)->setAlignItems(Align::Stretch);
    live->sync = [live, st] {
        live->clearChildren();
        std::vector<std::string> missing;
        for (const auto& a : kApps)
            if (st->read && !installed(*st, a.id)) missing.push_back(a.id);

        // The summary, and every missing one back in one go
        auto* top = live->add<Flex>(Flex::Direction::Row);
        top->setSpacing(12)->setAlignItems(Align::Center)->setPadding({4, 0});
        auto* t = top->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(2);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(!st->read          ? "Looking…"
                      : missing.empty()  ? "Every app is installed"
                                         : std::format("{} app{} removed", missing.size(), missing.size() == 1 ? "" : "s"))
            ->setSize(16)
            ->setWeight(600);
        t->add<Label>("Each app is a package of its own: remove the ones you don't use, and bring any back here (Material3UI " + std::string(M3_VERSION) + ")")
            ->setSize(12)
            ->setMaxWidth(470)
            ->setMaxLines(2)
            ->setColor("on_surface_variant", false);
        if (missing.size() > 1)
            pillButton(top, st->busy == "*" ? "Reinstalling…" : "Reinstall all", true, [st, missing] { reinstall(st, missing); }, "restore");

        auto* g = group(live, "Apps");
        for (const auto& a : kApps) {
            const bool here = st->read && installed(*st, a.id);
            auto* row = g->add<Flex>(Flex::Direction::Row);
            row->setPadding({16, 10})->setSpacing(14)->setAlignItems(Align::Center);
            const std::string icon = std::string("material3ui-") + a.id;
            std::error_code ec;
            Node* img;
            std::string file = "/usr/share/icons/hicolor/scalable/apps/" + icon + ".svg";
            if (!fs::exists(file, ec)) file = paths::dataFile("icons/" + icon + ".svg");
            if (fs::exists(file, ec)) {
                img = row->add<ImageView>(32, [file](float scale, int& w, int& h) -> GLuint {
                    auto* tex = Images::get().icon(file, static_cast<int>(std::lround(32 * scale)));
                    if (!tex) return 0;
                    w = tex->width, h = tex->height;
                    return tex->id;
                });
            } else {
                // Its icon went with its package: its symbol instead
                auto* box = row->add<Flex>(Flex::Direction::Row);
                box->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(10))->setBackground("surface_container_highest", false);
                box->fixedWidth = 32, box->fixedHeight = 32;
                box->add<Icon>(a.symbol, 20)->setColor("on_surface_variant", false);
                img = box;
            }
            if (!here) img->setOpacity(0.5f, false);
            auto* texts = row->add<Flex>(Flex::Direction::Column);
            texts->setAlignItems(Align::Start)->setSpacing(1);
            texts->grow = true;
            texts->givesWay = true;
            texts->add<Label>(a.title)->setSize(15)->setWeight(500)->setColor(here ? "on_surface" : "on_surface_variant", false);
            const auto note = st->note.find(a.id);
            texts->add<Label>(note != st->note.end() ? note->second : here ? a.what : std::string(a.what) + "  ·  not installed")
                ->setSize(12)
                ->setMaxWidth(420)
                ->setMaxLines(2)
                ->setColor(note != st->note.end() ? "error" : "on_surface_variant", false);
            if (!st->read) continue;
            if (here) {
                auto* chip = row->add<Flex>(Flex::Direction::Row);
                pill(chip->setBackground(ColorRef("primary", 0.12f), false))->setPadding({10, 4})->setSpacing(4)->setAlignItems(Align::Center);
                chip->add<Icon>("check", 14)->setColor("primary", false);
                chip->add<Label>("Installed")->setSize(12)->setWeight(500)->setColor("primary", false);
            } else {
                const bool busy = st->busy == a.id || st->busy == "*";
                pillButton(row, busy ? "Reinstalling…" : "Reinstall", false, [st, id = std::string(a.id)] { reinstall(st, {id}); }, busy ? "" : "download");
            }
        }
        caption(live, "  Packages come from the release on GitHub (or your package cache). To remove an app: sudo pacman -R material3ui-<app>");
    };
    live->watch(st->changed);
    live->watch(Packages::get().changed);
    live->sync();
}

} // namespace m3::shell::panels
