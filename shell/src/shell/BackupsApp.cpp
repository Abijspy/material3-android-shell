// Backups (material3ui-backups): your folders, kept as dated snapshots on
// a drive or in a folder of your choosing.
//
// Each snapshot is a plain folder (browse it in Files, copy anything back);
// a file unchanged since the snapshot before is a hard link to it, so only
// what changed takes room. Caches, the trash and build folders are left
// out. Run now, or every day by itself (a systemd user timer running
// `material3ui-backups --run`); old snapshots thin out: every one for a
// week, one a week for a month, one a month for a year.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace backups {

std::string hostName() {
    char h[256] = {};
    gethostname(h, sizeof h - 1);
    return h[0] ? h : "this-computer";
}

std::string destination() {
    return Config::get().value<std::string>("/backups/destination", "");
}
std::string root() {
    const std::string d = destination();
    return d.empty() ? "" : d + "/Material3UI Backups/" + hostName();
}
std::vector<std::string> sources() {
    std::vector<std::string> out;
    const auto j = Config::get().value<json>("/backups/folders", json::array());
    for (const auto& f : j)
        if (f.is_string()) out.push_back(f.get<std::string>());
    if (out.empty()) {
        // Your own folders, to start with
        for (const char* d : {"Documents", "Pictures", "Music", "Videos", "Desktop"}) {
            std::error_code ec;
            if (fs::is_directory(paths::home() + "/" + d, ec)) out.push_back(paths::home() + "/" + d);
        }
        out.push_back(paths::home() + "/.config");
    }
    return out;
}
std::vector<std::string> excludes() {
    std::vector<std::string> out = {".cache", "Trash", "node_modules", ".npm", ".cargo/registry", "__pycache__", ".venv", "build-release", ".local/share/Steam",
                                    ".var/app", "*.tmp", ".thumbnails"};
    std::stringstream ss(Config::get().value<std::string>("/backups/exclude", ""));
    for (std::string e; std::getline(ss, e, ',');) {
        e.erase(0, e.find_first_not_of(' '));
        e.erase(e.find_last_not_of(' ') + 1);
        if (!e.empty()) out.push_back(e);
    }
    return out;
}

struct Snapshot {
    std::string path, name;
    std::time_t time = 0;
    uint64_t files = 0, bytes = 0, copied = 0;
};

std::vector<Snapshot> snapshots() {
    std::vector<Snapshot> out;
    const std::string r = root();
    std::error_code ec;
    if (r.empty() || !fs::is_directory(r, ec)) return out;
    for (const auto& e : fs::directory_iterator(r, ec)) {
        if (!e.is_directory(ec) || e.path().filename().string().ends_with(".partial")) continue;
        Snapshot s;
        s.path = e.path().string();
        s.name = e.path().filename().string();
        std::tm tm{};
        if (strptime(s.name.c_str(), "%Y-%m-%d_%H-%M-%S", &tm)) s.time = std::mktime(&tm);
        std::ifstream in(e.path() / ".material3ui-backup.json");
        try {
            if (in) {
                const json j = json::parse(in);
                s.files = j.value("files", uint64_t{0}), s.bytes = j.value("bytes", uint64_t{0}), s.copied = j.value("copied", uint64_t{0});
            }
        } catch (...) {
        }
        out.push_back(s);
    }
    std::ranges::sort(out, [](const Snapshot& a, const Snapshot& b) { return a.name > b.name; });
    return out;
}

bool excluded(const fs::path& rel, const std::vector<std::string>& ex) {
    const std::string s = rel.string();
    const std::string name = rel.filename().string();
    for (const auto& e : ex) {
        if (e.starts_with("*.") && name.ends_with(e.substr(1))) return true;
        if (name == e || s == e || s.starts_with(e + "/") || s.find("/" + e + "/") != std::string::npos || s.ends_with("/" + e)) return true;
    }
    return false;
}

struct Progress {
    std::atomic<uint64_t> files{0}, bytes{0}, copied{0}, errors{0};
    std::atomic<bool> cancel{false}, done{false};
    std::string error, snapshot;
};

// One snapshot: what's new or changed copied, the rest hard-linked to the
// one before. Runs on whatever thread calls it
void run(Progress& p) {
    const std::string r = root();
    std::error_code ec;
    if (r.empty()) {
        p.error = "No place to back up to: choose one first";
        p.done = true;
        return;
    }
    if (!fs::is_directory(destination(), ec)) {
        p.error = "The backup drive isn't connected (" + destination() + ")";
        p.done = true;
        return;
    }
    fs::create_directories(r, ec);
    const auto before = snapshots();
    const fs::path prev = before.empty() ? fs::path() : fs::path(before.front().path);
    char name[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof name, "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
    const fs::path partial = fs::path(r) / (std::string(name) + ".partial");
    fs::remove_all(partial, ec);
    fs::create_directories(partial, ec);
    const fs::path home = paths::home();
    const auto ex = excludes();
    for (const auto& src : sources()) {
        if (p.cancel) break;
        const fs::path s(src);
        if (!fs::exists(s, ec)) continue;
        // Inside the home folder: kept under the same path; elsewhere, under its full path
        const fs::path rel = s.string().starts_with(home.string() + "/") ? fs::relative(s, home, ec) : fs::path("_root") / s.relative_path();
        auto one = [&](const fs::path& file, const fs::path& relFile) {
            const fs::path to = partial / relFile;
            fs::create_directories(to.parent_path(), ec);
            if (fs::is_symlink(file, ec)) {
                const auto target = fs::read_symlink(file, ec);
                fs::create_symlink(target, to, ec);
                return;
            }
            const auto size = fs::file_size(file, ec);
            const auto mtime = fs::last_write_time(file, ec);
            p.files++;
            p.bytes += size;
            if (!prev.empty()) {
                const fs::path old = prev / relFile;
                std::error_code e2;
                if (fs::exists(old, e2) && fs::file_size(old, e2) == size && fs::last_write_time(old, e2) == mtime) {
                    fs::create_hard_link(old, to, e2);
                    if (!e2) return;
                }
            }
            std::error_code e3;
            fs::copy_file(file, to, fs::copy_options::overwrite_existing, e3);
            if (e3) {
                p.errors++;
                return;
            }
            fs::last_write_time(to, mtime, e3);
            fs::permissions(to, fs::status(file, e3).permissions(), e3);
            p.copied += size;
        };
        if (fs::is_regular_file(s, ec)) {
            one(s, rel);
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(s, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (p.cancel) break;
            if (ec) {
                ec.clear();
                continue;
            }
            const fs::path relFile = rel / fs::relative(it->path(), s, ec);
            if (excluded(relFile, ex)) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            // The backups themselves, if they sit inside a folder being backed up
            if (it->path().string().starts_with(destination())) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(ec) && !it->is_symlink(ec)) {
                fs::create_directories(partial / relFile, ec);
                continue;
            }
            if (it->is_regular_file(ec) || it->is_symlink(ec)) one(it->path(), relFile);
        }
    }
    if (p.cancel) {
        fs::remove_all(partial, ec);
        p.error = "Stopped";
        p.done = true;
        return;
    }
    {
        std::ofstream m(partial / ".material3ui-backup.json");
        m << json{{"files", p.files.load()}, {"bytes", p.bytes.load()}, {"copied", p.copied.load()}, {"errors", p.errors.load()}, {"sources", sources()}, {"time", now}}.dump(1);
    }
    fs::rename(partial, fs::path(r) / name, ec);
    p.snapshot = (fs::path(r) / name).string();
    if (ec) p.error = ec.message();
    // Thinning: every one for 7 days, one a week for 5 weeks, one a month for a year
    if (Config::get().value<bool>("/backups/thin", true)) {
        std::set<std::string> keep;
        std::set<std::string> weeks, months;
        for (const auto& s : snapshots()) {
            const double age = std::difftime(now, s.time) / 86400.0;
            char wk[16], mo[16];
            std::tm tm = *std::localtime(&s.time);
            std::strftime(wk, sizeof wk, "%G-%V", &tm);
            std::strftime(mo, sizeof mo, "%Y-%m", &tm);
            if (age <= 7) keep.insert(s.path);
            else if (age <= 35 && weeks.insert(wk).second) keep.insert(s.path);
            else if (age <= 366 && months.insert(mo).second) keep.insert(s.path);
        }
        for (const auto& s : snapshots())
            if (!keep.contains(s.path)) fs::remove_all(s.path, ec);
    }
    p.done = true;
}

// The daily timer, as a systemd user unit
void setAutomatic(bool on) {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const std::string dir = (x && *x ? std::string(x) : paths::home() + "/.config") + "/systemd/user";
    std::error_code ec;
    if (on) {
        fs::create_directories(dir, ec);
        const std::string prog = ownCommand("material3ui-backups").empty() ? "material3ui-backups" : ownCommand("material3ui-backups");
        paths::writeFile(dir + "/material3ui-backups.service",
                         "[Unit]\nDescription=Material3UI Backups: today's snapshot\n\n[Service]\nType=oneshot\nNice=10\nIOSchedulingClass=idle\nExecStart=" + prog + " --run\n");
        paths::writeFile(dir + "/material3ui-backups.timer",
                         "[Unit]\nDescription=Material3UI Backups, every day\n\n[Timer]\nOnCalendar=daily\nPersistent=true\nRandomizedDelaySec=15min\n\n[Install]\nWantedBy=timers.target\n");
        m3::run({"systemctl", "--user", "daemon-reload"}, [](int, const std::string&) { m3::run({"systemctl", "--user", "enable", "--now", "material3ui-backups.timer"}, nullptr); });
    } else {
        m3::run({"systemctl", "--user", "disable", "--now", "material3ui-backups.timer"}, nullptr);
    }
}

} // namespace backups

namespace {

using backups::Progress;

struct State {
    std::shared_ptr<Progress> progress;
    std::thread worker;
    Flex* content = nullptr;
    std::function<void()> show;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId tick = 0;
    ~State() {
        if (worker.joinable()) {
            if (progress) progress->cancel = true;
            worker.join();
        }
    }
};

void startBackup(const std::shared_ptr<State>& st) {
    if (st->progress && !st->progress->done) return;
    if (st->worker.joinable()) st->worker.join();
    st->progress = std::make_shared<Progress>();
    auto p = st->progress;
    st->worker = std::thread([p] { backups::run(*p); });
    if (st->show) st->show();
}

// Where to back up to: the drives plugged in, or any folder
void destinationMenu(const std::shared_ptr<State>& st, Node* from) {
    std::vector<MenuItem> items;
    for (const auto& d : fileops::drives()) {
        if (d.mountpoint.empty() || d.mountpoint == "/" || d.mountpoint.starts_with("/boot")) continue;
        const std::string m = d.mountpoint;
        items.emplace_back("usb", (d.label.empty() ? d.device : d.label) + "  ·  " + d.size, m, [st, m] {
            Config::get().set("/backups/destination", m);
            if (st->show) st->show();
        });
    }
    const std::string local = paths::home() + "/Backups";
    items.emplace_back("folder", "A folder in your home (~/Backups)", "not safe from a broken drive", [st, local] {
        std::error_code ec;
        fs::create_directories(local, ec);
        Config::get().set("/backups/destination", local);
        if (st->show) st->show();
    }, false, false, true, true);
    floatingMenu(from, items);
}

class BackupsKeeper : public Node {
public:
    explicit BackupsKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~BackupsKeeper() override {
        *m_st->alive = false;
        if (m_st->tick) EventLoop::get().cancel(m_st->tick);
        m_st->show = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void backupsApp(Flex* card, Popup& popup, const std::string& arg) {
    (void)popup;
    (void)arg;
    auto st = std::make_shared<State>();
    card->setPadding(Insets(14))->setSpacing(0);
    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    st->content = scroll->add<Flex>(Flex::Direction::Column);
    st->content->setSpacing(14)->setAlignItems(Align::Stretch)->setPadding({16, 8, 16, 16});
    card->add<BackupsKeeper>(st);

    st->show = [st] {
        auto* c = st->content;
        c->clearChildren();
        auto* head = c->add<Flex>(Flex::Direction::Row);
        head->setSpacing(16)->setAlignItems(Align::Center);
        auto* dot = head->add<Flex>(Flex::Direction::Row);
        dot->fixedWidth = dot->fixedHeight = 64;
        dot->setRadius(Radii(32))->setBackground("primary_container", false)->setAlignItems(Align::Center)->setJustify(Align::Center);
        dot->add<Icon>("backup", 34)->setFill(true)->setColor("on_primary_container", false);
        auto* t = head->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start)->setSpacing(2);
        t->grow = true;
        t->add<Label>("Backups")->setSize(26)->setWeight(500);
        const auto snaps = backups::snapshots();
        t->add<Label>(snaps.empty() ? "No backups yet" : "Last backup " + fileops::humanTime(snaps.front().time))->setSize(14)->setColor("on_surface_variant", false);

        // Under way, or the button
        const auto p = st->progress;
        if (p && !p->done) {
            auto* box = c->add<Flex>(Flex::Direction::Column);
            box->setPadding({18, 14})->setSpacing(8)->setRadius(Radii(22))->setBackground("secondary_container", false);
            box->add<Label>("Backing up…")->setSize(16)->setWeight(600)->setColor("on_secondary_container", false);
            box->add<Label>(std::format("{} files ({}) looked at  ·  {} copied, the rest unchanged", p->files.load(), fileops::humanSize(p->bytes.load()), fileops::humanSize(p->copied.load())))
                ->setSize(13)
                ->setColor("on_secondary_container", false);
            pillButton(box, "Stop", false, [p] { p->cancel = true; }, "stop");
        } else {
            if (p && p->done)
                caption(c, p->error.empty() ? std::format("Backed up: {} files ({} new or changed)", p->files.load(), fileops::humanSize(p->copied.load()))
                                            : p->error);
            auto* row = c->add<Flex>(Flex::Direction::Row);
            row->setSpacing(10);
            pillButton(row, "Back up now", true, [st] { startBackup(st); }, "backup");
            if (!snaps.empty())
                pillButton(row, "Browse backups", false, [] {
                    if (open) open("files " + backups::root());
                }, "folder_open");
        }

        auto* g = group(c, "Where");
        const std::string dest = backups::destination();
        std::error_code ec;
        const bool there = !dest.empty() && fs::is_directory(dest, ec);
        auto r = row(g, there ? "save" : "usb_off", dest.empty() ? "Choose where to back up" : fileops::displayPath(dest),
                     dest.empty() ? "A USB drive or another disk keeps them safe from this one failing" : there ? "Connected" : "Not connected now: plug it in to back up");
        Button* choose = nullptr;
        choose = pillButton(r.row, dest.empty() ? "Choose" : "Change", dest.empty(), {}, "drive_file_move");
        choose->onClicked = [st, choose](uint32_t b) {
            if (b == BTN_LEFT) destinationMenu(st, choose);
        };
        auto* w = group(c, "What", "Caches, the trash and build folders are left out");
        for (const auto& f : backups::sources()) {
            auto fr = row(w, "folder", fileops::displayPath(f), "");
            iconButton(fr.row, "close", [st, f] {
                auto list = backups::sources();
                std::erase(list, f);
                Config::get().set("/backups/folders", list);
                if (st->show) st->show();
            }, 18)->tooltip = "Don't back this up";
        }
        auto* addRow = w->add<Flex>(Flex::Direction::Row);
        addRow->setPadding({16, 8})->setSpacing(10)->setAlignItems(Align::Center);
        addRow->add<Icon>("create_new_folder", 20)->setColor("on_surface_variant", false);
        auto* f = addRow->add<TextField>();
        f->bare = true;
        f->grow = true;
        f->setPlaceholder("Add a folder: ~/Projects (Enter)");
        f->onSubmit = [st](const std::string& t) {
            std::string path = t;
            if (path.starts_with("~/")) path = paths::home() + path.substr(1);
            std::error_code e2;
            if (!fs::exists(path, e2)) return;
            auto list = backups::sources();
            if (std::ranges::find(list, path) == list.end()) list.push_back(path);
            Config::get().set("/backups/folders", list);
            if (st->show) st->show();
        };
        auto* when = group(c, "When");
        switchRow(when, "schedule", "Every day by itself", "When the backup drive is connected (a systemd timer)", "/backups/automatic", false,
                  [](bool on) { backups::setAutomatic(on); });
        switchRow(when, "auto_delete", "Thin out old backups", "Every one for a week, one a week for a month, one a month for a year", "/backups/thin", true);

        if (!snaps.empty()) {
            auto* h = group(c, std::format("Backups ({})", snaps.size()));
            for (const auto& s : snaps) {
                auto sr = row(h, "history", fileops::humanTime(s.time), s.files ? std::format("{} files, {}  ·  {} new or changed", s.files, fileops::humanSize(s.bytes), fileops::humanSize(s.copied)) : s.name);
                const std::string path = s.path;
                pillButton(sr.row, "Open", false, [path] {
                    if (open) open("files " + path);
                }, "folder_open");
                Button* more = iconButton(sr.row, "more_vert", [] {}, 20);
                more->onClicked = [st, path, more](uint32_t b) {
                    if (b != BTN_LEFT) return;
                    std::vector<MenuItem> items;
                    items.emplace_back("restore", "Restore all of it", "copied back over what's there", [st, path] {
                        // Copied back, never deleting what's newer
                        auto copy = std::make_shared<std::thread>([path] {
                            std::error_code e;
                            const fs::path home = paths::home();
                            for (auto it = fs::recursive_directory_iterator(path, e); it != fs::recursive_directory_iterator(); it.increment(e)) {
                                if (e) break;
                                const fs::path rel = fs::relative(it->path(), path, e);
                                if (rel.filename() == ".material3ui-backup.json") continue;
                                const fs::path to = rel.begin()->string() == "_root" ? fs::path("/") / fs::relative(rel, "_root", e) : home / rel;
                                if (it->is_directory(e)) fs::create_directories(to, e);
                                else fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, e);
                            }
                        });
                        copy->detach();
                        m3::run({"notify-send", "-a", "Backups", "-i", "document-revert", "Restoring", "Your files are being copied back"}, nullptr);
                        (void)st;
                    });
                    items.emplace_back("delete", "Delete this backup", "", [st, path] {
                        std::error_code e;
                        fs::remove_all(path, e);
                        if (st->show) st->show();
                    }, false, true, true, true);
                    floatingMenu(more, items);
                };
            }
            caption(c, "Each backup is a plain folder: open one in Files and copy back any file you need");
        }
    };
    st->tick = EventLoop::get().addTimer(700, [st] {
        if (!*st->alive || !st->progress) return;
        static bool wasRunning = false;
        const bool running = !st->progress->done;
        if (running || wasRunning) {
            if (st->show) st->show();
            if (!running) m3::run({"notify-send", "-a", "Backups", "-i", "backup", st->progress->error.empty() ? "Backed up" : "Backup: " + st->progress->error}, nullptr);
        }
        wasRunning = running;
    }, true);
    st->show();
}

void backupsSettings(Flex* page) {
    auto* g = group(page, "Left out");
    textRow(g, "block", "Also leave out", "/backups/exclude", "");
    caption(g, "  Names or paths, separated by commas: Downloads, *.iso, .local/share/containers (Enter saves)");
    auto* n = group(page, "Daily backups");
    choiceRow<std::string>(n, "notifications", "Tell me", "When the timer backs up by itself", "/backups/notify", "problems", {{"always", "Every time"}, {"problems", "Only when it fails"}, {"never", "Never"}});
    caption(page, "Always left out: .cache, the trash, node_modules, .npm, __pycache__, .venv, Steam's games, Flatpak app data, thumbnails");
}

// `material3ui-backups --run`: one snapshot, no window (the daily timer)
int backupsRunHeadless() {
    backups::Progress p;
    backups::run(p);
    const std::string when = Config::get().value<std::string>("/backups/notify", "problems");
    if (when == "always" || (when == "problems" && !p.error.empty())) {
        const std::string text = p.error.empty() ? std::format("{} files backed up", p.files.load()) : "Backup didn't finish: " + p.error;
        if (fork() == 0) {
            execlp("notify-send", "notify-send", "-a", "Backups", "-i", "backup", text.c_str(), nullptr);
            _exit(127);
        }
    }
    if (!p.error.empty()) {
        std::fprintf(stderr, "material3ui-backups: %s\n", p.error.c_str());
        return 1;
    }
    std::printf("%s: %llu files, %llu bytes new\n", p.snapshot.c_str(), static_cast<unsigned long long>(p.files.load()), static_cast<unsigned long long>(p.copied.load()));
    return 0;
}

} // namespace m3::shell::panels
