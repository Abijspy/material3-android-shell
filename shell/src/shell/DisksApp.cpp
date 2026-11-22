// Disks (material3ui-disks): your drives and what's on them.
//
// Each drive with its partitions as a bar to scale; mount, unmount and power
// off (udisks2, no password); rename, check and format a partition (through
// pkexec, the shell's polkit prompt asks); the drive's health (smartctl);
// and Space: what fills a folder, biggest first, a click deeper.
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Renderer.h"
#include "services/FileOps.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <linux/input-event-codes.h>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

// A share of something, as a bar: the track, and the part filled
class FracBar : public Node {
public:
    float fraction = 0, height = 8;
    std::string role = "primary";

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, height}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Theme::get().color("surface_container_highest"), Radii(height / 2));
        const float w = std::max(height, b.w * std::clamp(fraction, 0.0f, 1.0f));
        r.rect({b.x, b.y, w, b.h}, Theme::get().color(role), Radii(height / 2));
    }
};

struct Part {
    std::string name, path, fstype, label, mount, parttype, uuid;
    uint64_t size = 0, used = 0, fssize = 0;
};
struct Disk {
    std::string name, path, model, tran;
    uint64_t size = 0;
    bool removable = false, rotational = false;
    std::vector<Part> parts;
};

uint64_t num(const json& v) {
    if (v.is_number()) return v.get<uint64_t>();
    if (v.is_string()) return std::strtoull(v.get<std::string>().c_str(), nullptr, 10);
    return 0;
}
std::string str(const json& j, const char* k) {
    return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : "";
}

Part partOf(const json& c) {
    Part p;
    p.name = str(c, "name"), p.path = str(c, "path"), p.fstype = str(c, "fstype"), p.label = str(c, "label"), p.mount = str(c, "mountpoint");
    p.parttype = str(c, "parttypename"), p.uuid = str(c, "uuid");
    p.size = num(c.value("size", json())), p.used = num(c.value("fsused", json())), p.fssize = num(c.value("fssize", json()));
    return p;
}

std::vector<Disk> readDisks(const std::string& out) {
    std::vector<Disk> disks;
    try {
        for (const auto& d : json::parse(out).value("blockdevices", json::array())) {
            if (str(d, "type") != "disk" || str(d, "name").starts_with("zram") || str(d, "name").starts_with("loop")) continue;
            Disk k;
            k.name = str(d, "name"), k.path = str(d, "path"), k.model = str(d, "model"), k.tran = str(d, "tran");
            k.size = num(d.value("size", json()));
            k.removable = d.value("rm", false) || k.tran == "usb";
            k.rotational = d.value("rota", false);
            for (const auto& c : d.value("children", json::array())) k.parts.push_back(partOf(c));
            // A drive formatted whole (no partition table): itself as the one
            if (k.parts.empty() && !str(d, "fstype").empty()) k.parts.push_back(partOf(d));
            disks.push_back(std::move(k));
        }
    } catch (...) {
    }
    return disks;
}

// The system's own: formatting these is refused outright
bool systemPart(const Part& p) {
    return p.mount == "/" || p.mount == "/boot" || p.mount == "/boot/efi" || p.mount == "/efi" || p.mount == "/home" || p.mount == "/usr" ||
           p.mount == "/var" || p.mount == "[SWAP]" || p.parttype == "EFI System";
}

std::string kindOf(const Disk& d) {
    if (d.tran == "usb") return "USB drive";
    if (d.tran == "nvme") return "NVMe SSD";
    if (d.name.starts_with("mmcblk")) return "Memory card";
    if (d.tran == "sata" || d.tran == "ata") return d.rotational ? "Hard disk" : "SATA SSD";
    return d.rotational ? "Hard disk" : "Drive";
}
std::string iconOf(const Disk& d) {
    if (d.tran == "usb") return "usb";
    if (d.name.starts_with("mmcblk")) return "sd_card";
    return d.rotational ? "hard_drive" : "storage";
}

struct State {
    std::vector<Disk> disks;
    int view = 0; // 0..n-1 a disk, -1 Space
    std::string spaceDir;
    std::vector<std::pair<uint64_t, std::string>> spaceItems;
    bool spaceLoading = false;
    std::map<std::string, std::string> health; // disk path → summary
    std::string busy;                          // what's under way
    Flex *side = nullptr, *content = nullptr, *dialog = nullptr;
    std::function<void()> show, sidebar, reload;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId poll = 0;
};

void notify(const std::string& title, const std::string& body = {}) {
    m3::run({"notify-send", "-a", "Disks", "-i", "drive-harddisk", title, body}, nullptr);
}

// Something needing root, through pkexec; then the drives read again
void rooted(const std::shared_ptr<State>& st, const std::string& what, std::vector<std::string> cmd) {
    st->busy = what + "…";
    if (st->show) st->show();
    cmd.insert(cmd.begin(), "pkexec");
    m3::run(cmd, [st, what](int code, const std::string& out) {
        if (!*st->alive) return;
        st->busy.clear();
        if (code == 0) notify(what + ": done");
        else if (code != 126 && code != 127) notify("Couldn't " + what, out.substr(0, 300));
        if (st->reload) st->reload();
    });
}

void dialog(const std::shared_ptr<State>& st, const std::string& icon, const std::string& title, const std::string& text, std::function<void(Flex*)> body) {
    auto* d = st->dialog;
    d->clearChildren();
    d->setVisible(true);
    auto* head = d->add<Flex>(Flex::Direction::Row);
    head->setSpacing(10)->setAlignItems(Align::Center);
    head->add<Icon>(icon, 24)->setColor("primary", false);
    auto* t = head->add<Label>(title);
    t->setSize(17)->setWeight(600);
    t->grow = true;
    iconButton(head, "close", [st] { st->dialog->setVisible(false); }, 20);
    if (!text.empty()) d->add<Label>(text)->setSize(14)->setMaxWidth(560)->setMaxLines(4)->setColor("on_surface_variant", false);
    body(d);
}

void formatDialog(const std::shared_ptr<State>& st, const Part& p) {
    dialog(st, "format_paint", "Format " + (p.label.empty() ? p.name : p.label),
           std::format("Everything on {} ({}) is erased. Type the name below to go ahead.", p.path, fileops::humanSize(p.size)), [st, p](Flex* d) {
               auto fsType = std::make_shared<std::string>("exfat");
               auto row = d->add<Flex>(Flex::Direction::Row);
               row->setSpacing(10)->setAlignItems(Align::Center);
               row->add<Label>("As")->setSize(14);
               connectedGroup<std::string>(row, *fsType, {{"exfat", "exFAT (everywhere)"}, {"vfat", "FAT32 (old devices)"}, {"ext4", "ext4 (Linux)"}, {"ntfs", "NTFS (Windows)"}},
                                           [fsType](const std::string& v) { *fsType = v; });
               auto* name = d->add<TextField>();
               name->setPlaceholder("The new name (letters and numbers)");
               name->setText(p.label.empty() ? "Drive" : p.label);
               auto* confirm = d->add<TextField>();
               confirm->setPlaceholder("Type " + p.name + " to confirm");
               auto* btns = d->add<Flex>(Flex::Direction::Row);
               btns->setSpacing(8)->setJustify(Align::End);
               pillButton(btns, "Cancel", false, [st] { st->dialog->setVisible(false); });
               pillButton(btns, "Erase and format", true, [st, p, fsType, name, confirm] {
                   if (confirm->text() != p.name) return notify("Not formatted", "Type " + p.name + " exactly to confirm");
                   std::string label = name->text();
                   std::erase_if(label, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != ' '; });
                   if (label.empty()) label = "Drive";
                   st->dialog->setVisible(false);
                   // Unmounted first (udisks, as you), then the filesystem made (root)
                   auto make = [st, p, fsType, label] {
                       std::vector<std::string> cmd;
                       if (*fsType == "exfat") cmd = {"mkfs.exfat", "-n", label.substr(0, 15), p.path};
                       else if (*fsType == "vfat") cmd = {"mkfs.fat", "-F", "32", "-n", label.substr(0, 11), p.path};
                       else if (*fsType == "ntfs") cmd = {"mkfs.ntfs", "-f", "-L", label, p.path};
                       else cmd = {"mkfs.ext4", "-F", "-L", label.substr(0, 16), p.path};
                       if (!haveCommand(cmd[0])) {
                           const char* pkg = *fsType == "exfat" ? "exfatprogs" : *fsType == "vfat" ? "dosfstools" : *fsType == "ntfs" ? "ntfs-3g" : "e2fsprogs";
                           return notify("Formatting as this needs " + std::string(pkg), std::string("sudo pacman -S ") + pkg);
                       }
                       rooted(st, "format " + p.name, cmd);
                   };
                   if (!p.mount.empty()) m3::run({"udisksctl", "unmount", "-b", p.path}, [make](int, const std::string&) { make(); });
                   else make();
               }, "delete_forever");
           });
}

void renameDialog(const std::shared_ptr<State>& st, const Part& p) {
    dialog(st, "edit", "Rename " + (p.label.empty() ? p.name : p.label), "The name the drive shows in Files and on other computers.", [st, p](Flex* d) {
        auto* name = d->add<TextField>();
        name->setText(p.label);
        name->setPlaceholder("Name");
        auto* btns = d->add<Flex>(Flex::Direction::Row);
        btns->setSpacing(8)->setJustify(Align::End);
        pillButton(btns, "Cancel", false, [st] { st->dialog->setVisible(false); });
        pillButton(btns, "Rename", true, [st, p, name] {
            const std::string l = name->text();
            st->dialog->setVisible(false);
            std::vector<std::string> cmd;
            if (p.fstype == "ext2" || p.fstype == "ext3" || p.fstype == "ext4") cmd = {"e2label", p.path, l};
            else if (p.fstype == "vfat") cmd = {"fatlabel", p.path, l};
            else if (p.fstype == "exfat") cmd = {"exfatlabel", p.path, l};
            else if (p.fstype == "ntfs") cmd = {"ntfslabel", p.path, l};
            else if (p.fstype == "btrfs") cmd = {"btrfs", "filesystem", "label", p.mount.empty() ? p.path : p.mount, l};
            else return notify("Can't rename " + p.fstype + " here");
            if (!haveCommand(cmd[0])) return notify("Renaming needs " + cmd[0]);
            rooted(st, "rename " + p.name, cmd);
        }, "check");
    });
}

void checkHealth(const std::shared_ptr<State>& st, const Disk& d) {
    if (!haveCommand("smartctl")) return notify("Drive health needs smartmontools", "sudo pacman -S smartmontools");
    st->health[d.path] = "Reading…";
    if (st->show) st->show();
    const std::string path = d.path;
    m3::run({"pkexec", "smartctl", "-j", "-H", "-A", "-i", path}, [st, path](int, const std::string& out) {
        if (!*st->alive) return;
        std::string s;
        try {
            const json j = json::parse(out);
            if (j.contains("smart_status")) s += j["smart_status"].value("passed", false) ? "Healthy" : "Failing: back up now";
            else s += "Health not reported";
            if (j.contains("temperature")) s += std::format("  ·  {} °C", j["temperature"].value("current", 0));
            if (j.contains("power_on_time")) s += std::format("  ·  {} hours on", j["power_on_time"].value("hours", 0));
            if (j.contains("nvme_smart_health_information_log")) {
                const auto& n = j["nvme_smart_health_information_log"];
                s += std::format("  ·  {}% worn", n.value("percentage_used", 0));
                if (n.contains("data_units_written")) s += std::format("  ·  {} written", fileops::humanSize(n.value("data_units_written", uint64_t{0}) * 512000));
            }
            if (j.contains("ata_smart_attributes"))
                for (const auto& a : j["ata_smart_attributes"].value("table", json::array()))
                    if (a.value("id", 0) == 5 && a.contains("raw")) s += std::format("  ·  {} reallocated sectors", a["raw"].value("value", 0));
        } catch (...) {
            s = "Couldn't read its health";
        }
        st->health[path] = s;
        if (st->show) st->show();
    });
}

// The partitions as a bar, each to scale, in its own tone
void partBar(Flex* c, const Disk& d) {
    auto* bar = c->add<Flex>(Flex::Direction::Row);
    bar->setSpacing(3)->setRadius(Radii(14))->setAlignItems(Align::Stretch);
    bar->fixedHeight = 44;
    const char* tones[] = {"primary_container", "tertiary_container", "secondary_container", "error_container"};
    uint64_t sum = 0;
    for (const auto& p : d.parts) sum += p.size;
    int i = 0;
    for (const auto& p : d.parts) {
        auto* seg = bar->add<Flex>(Flex::Direction::Column);
        seg->setBackground(tones[i++ % 4], false)->setRadius(Radii(10))->setJustify(Align::Center)->setPadding({8, 0});
        seg->grow = true;
        seg->fixedWidth = std::max(6.0f, 560.0f * static_cast<float>(p.size) / static_cast<float>(std::max<uint64_t>(1, d.size)));
        seg->add<Label>(p.label.empty() ? p.name : p.label)->setSize(11)->setWeight(600)->setMaxWidth(seg->fixedWidth.value_or(60) - 12);
    }
    if (d.size > sum + d.size / 100) {
        auto* free = bar->add<Flex>(Flex::Direction::Column);
        free->setBorder(1.5f, ColorRef("outline_variant"))->setRadius(Radii(10))->setJustify(Align::Center)->setPadding({8, 0});
        free->grow = true;
        free->fixedWidth = std::max(6.0f, 560.0f * static_cast<float>(d.size - sum) / static_cast<float>(d.size));
        free->add<Label>("Free")->setSize(11)->setColor("on_surface_variant", false);
    }
}

void diskPage(const std::shared_ptr<State>& st, Flex* c, const Disk& d) {
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setSpacing(16)->setAlignItems(Align::Center);
    auto* dot = head->add<Flex>(Flex::Direction::Row);
    dot->fixedWidth = dot->fixedHeight = 64;
    dot->setRadius(Radii(20))->setBackground("secondary_container", false)->setAlignItems(Align::Center)->setJustify(Align::Center);
    dot->add<Icon>(iconOf(d), 34)->setColor("on_secondary_container", false);
    auto* t = head->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(2);
    t->grow = true;
    t->add<Label>(d.model.empty() ? d.name : d.model)->setSize(22)->setWeight(500)->setMaxWidth(440);
    t->add<Label>(std::format("{}  ·  {}  ·  {}", kindOf(d), fileops::humanSize(d.size), d.path))->setSize(13)->setColor("on_surface_variant", false);
    if (d.removable) {
        const std::string path = d.path;
        pillButton(head, "Power off", false, [st, path] {
            m3::run({"udisksctl", "power-off", "-b", path}, [st](int code, const std::string& out) {
                notify(code == 0 ? "Safe to remove" : "Couldn't power it off", code == 0 ? "" : out);
                if (*st->alive && st->reload) st->reload();
            });
        }, "eject");
    }
    // Health
    auto* h = c->add<Flex>(Flex::Direction::Row);
    h->setSpacing(10)->setAlignItems(Align::Center)->setPadding({14, 10})->setRadius(Radii(18));
    h->setBackground("surface_container_high", false);
    const auto it = st->health.find(d.path);
    const bool known = it != st->health.end();
    const bool failing = known && it->second.starts_with("Failing");
    h->add<Icon>(failing ? "warning" : "health_and_safety", 22)->setColor(failing ? "error" : "primary", false);
    auto* hl = h->add<Label>(known ? it->second : "Its health: from the drive's own self-monitoring (SMART)");
    hl->setSize(14)->setMaxWidth(440)->setMaxLines(2);
    hl->grow = true;
    pillButton(h, known ? "Check again" : "Check health", false, [st, d] { checkHealth(st, d); }, "monitor_heart");

    if (!d.parts.empty()) partBar(c, d);
    caption(c, d.parts.empty() ? "No partitions (empty, or not formatted)" : std::format("{} partitions", d.parts.size()));
    for (const auto& p : d.parts) {
        auto* card = c->add<Flex>(Flex::Direction::Column);
        card->setPadding({16, 12})->setSpacing(8)->setRadius(Radii(20))->setBackground("surface_container_high", false)->setAlignItems(Align::Stretch);
        auto* top = card->add<Flex>(Flex::Direction::Row);
        top->setSpacing(10)->setAlignItems(Align::Center);
        auto* names = top->add<Flex>(Flex::Direction::Column);
        names->setAlignItems(Align::Start);
        names->grow = true;
        names->add<Label>(p.label.empty() ? p.name : p.label)->setSize(15)->setWeight(600);
        std::string sub = std::format("{}  ·  {}", p.path, fileops::humanSize(p.size));
        if (!p.fstype.empty()) sub += "  ·  " + p.fstype;
        if (!p.parttype.empty()) sub += "  ·  " + p.parttype;
        names->add<Label>(sub)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(440);
        if (!p.mount.empty() && p.mount != "[SWAP]") {
            const std::string m = p.mount;
            pillButton(top, "Open", false, [m] {
                if (open) open("files " + m);
            }, "folder_open");
        }
        Button* more = iconButton(top, "more_vert", [] {}, 20);
        more->onClicked = [st, p, more](uint32_t b) {
            if (b != BTN_LEFT) return;
            std::vector<MenuItem> items;
            const bool sys = systemPart(p);
            if (!p.fstype.empty() && p.fstype != "swap") {
                if (p.mount.empty())
                    items.emplace_back("folder_open", "Mount", "", [st, p] {
                        m3::run({"udisksctl", "mount", "-b", p.path}, [st](int code, const std::string& out) {
                            if (code != 0) notify("Couldn't mount it", out);
                            if (*st->alive && st->reload) st->reload();
                        });
                    });
                else if (!sys)
                    items.emplace_back("eject", "Unmount", "", [st, p] {
                        m3::run({"udisksctl", "unmount", "-b", p.path}, [st](int code, const std::string& out) {
                            if (code != 0) notify("Couldn't unmount it", out);
                            if (*st->alive && st->reload) st->reload();
                        });
                    });
                items.emplace_back("edit", "Rename…", "", [st, p] { renameDialog(st, p); });
                if (p.mount.empty() && haveCommand("fsck"))
                    items.emplace_back("fact_check", "Check for errors", "read-only", [st, p] { rooted(st, "check " + p.name, {"fsck", "-n", p.path}); });
            }
            items.emplace_back("content_copy", "Copy the UUID", "", [p] { m3::run({"wl-copy"}, nullptr, p.uuid); }, false, false, !p.uuid.empty());
            items.emplace_back("format_paint", sys ? "Format (the system's own: not here)" : "Format…", "", [st, p] { formatDialog(st, p); }, false, true, !sys, true);
            floatingMenu(more, items);
        };
        if (p.fssize > 0) {
            auto* use = card->add<Flex>(Flex::Direction::Row);
            use->setSpacing(10)->setAlignItems(Align::Center);
            auto* bar = use->add<FracBar>();
            bar->grow = true;
            bar->fraction = static_cast<float>(p.used) / static_cast<float>(p.fssize);
            bar->role = bar->fraction > 0.9f ? "error" : "primary";
            use->add<Label>(std::format("{} free of {}", fileops::humanSize(p.fssize - p.used), fileops::humanSize(p.fssize)))->setSize(12)->setColor("on_surface_variant", false);
        }
        if (!p.mount.empty()) caption(card, "Mounted at " + p.mount);
    }
}

// Space: what fills a folder, biggest first
void loadSpace(const std::shared_ptr<State>& st, const std::string& dir) {
    st->spaceDir = dir;
    st->spaceItems.clear();
    st->spaceLoading = true;
    if (st->show) st->show();
    m3::run({"du", "-x", "-b", "-d", "1", dir}, [st, dir](int, const std::string& out) {
        if (!*st->alive || st->spaceDir != dir) return;
        st->spaceLoading = false;
        std::istringstream in(out);
        for (std::string line; std::getline(in, line);) {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const std::string path = line.substr(tab + 1);
            if (path == dir) continue;
            st->spaceItems.push_back({std::strtoull(line.c_str(), nullptr, 10), path});
        }
        // Files directly in it count too
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && !e.is_symlink(ec)) st->spaceItems.push_back({e.file_size(ec), e.path().string()});
        std::ranges::sort(st->spaceItems, std::greater{});
        if (st->show) st->show();
    });
}

void spacePage(const std::shared_ptr<State>& st, Flex* c) {
    if (st->spaceDir.empty()) st->spaceDir = paths::home();
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setSpacing(8)->setAlignItems(Align::Center);
    iconButton(head, "arrow_upward", [st] {
        const fs::path up = fs::path(st->spaceDir).parent_path();
        if (!up.empty() && up != fs::path(st->spaceDir)) loadSpace(st, up.string());
    }, 22)->tooltip = "Up";
    auto* t = head->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start);
    t->grow = true;
    t->add<Label>("Space used")->setSize(22)->setWeight(500);
    t->add<Label>(fileops::displayPath(st->spaceDir))->setSize(13)->setColor("on_surface_variant", false)->setMaxWidth(420);
    for (const auto& [name, dir] : std::initializer_list<std::pair<const char*, std::string>>{{"Home", paths::home()}, {"Whole system", "/"}}) {
        auto* chip = head->add<Button>();
        pill(chip->setBackground("surface_container_high", false))->setPadding({12, 6});
        chip->add<Label>(name)->setSize(13)->setWeight(500);
        chip->onClicked = [st, d = dir](uint32_t b) {
            if (b == BTN_LEFT) loadSpace(st, d);
        };
    }
    if (st->spaceLoading) {
        caption(c, "Adding up what's in it…");
        return;
    }
    if (st->spaceItems.empty()) {
        if (!st->spaceDir.empty()) loadSpace(st, st->spaceDir);
        return;
    }
    uint64_t total = 0;
    for (const auto& [n, p] : st->spaceItems) total += n;
    caption(c, std::format("{} in all", fileops::humanSize(total)));
    const uint64_t biggest = st->spaceItems.front().first;
    size_t shown = 0;
    for (const auto& [n, path] : st->spaceItems) {
        if (++shown > 200 || n == 0) break;
        std::error_code ec;
        const bool dir = fs::is_directory(path, ec);
        auto* b = c->add<Button>(Flex::Direction::Column);
        b->setPadding({14, 8})->setSpacing(5)->setRadius(Radii(14))->setAlignItems(Align::Stretch);
        auto* r = b->add<Flex>(Flex::Direction::Row);
        r->setSpacing(10)->setAlignItems(Align::Center);
        r->add<Icon>(dir ? "folder" : fileops::kindIcon(fileops::kindOf(path), path), 20)->setColor(dir ? "primary" : "on_surface_variant", false);
        auto* l = r->add<Label>(fs::path(path).filename().string());
        l->setSize(14)->setWeight(500)->setMaxWidth(380);
        l->grow = true;
        r->add<Label>(std::format("{}  ·  {:.0f}%", fileops::humanSize(n), 100.0 * static_cast<double>(n) / static_cast<double>(std::max<uint64_t>(1, total))))->setSize(12)->setColor("on_surface_variant", false);
        auto* bar = b->add<FracBar>();
        bar->height = 6;
        bar->role = dir ? "primary" : "tertiary";
        bar->fraction = static_cast<float>(n) / static_cast<float>(std::max<uint64_t>(1, biggest));
        const std::string p = path;
        b->onClicked = [st, p, dir, b](uint32_t btn) {
            if (btn == BTN_LEFT && dir) loadSpace(st, p);
            if (btn == BTN_RIGHT || (btn == BTN_LEFT && !dir)) {
                std::vector<MenuItem> items;
                items.emplace_back("folder_open", "Show in Files", "", [p] {
                    if (open) open("files " + p);
                });
                items.emplace_back("delete", "Move to the trash", "", [st, p] {
                    fileops::trash({p}, [st](const std::string& err) {
                        if (!err.empty()) notify("Couldn't move it to the trash", err);
                        if (*st->alive) loadSpace(st, st->spaceDir);
                    });
                }, false, true, true, true);
                floatingMenu(b, items);
            }
        };
    }
}

class DisksKeeper : public Node {
public:
    explicit DisksKeeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~DisksKeeper() override {
        *m_st->alive = false;
        if (m_st->poll) EventLoop::get().cancel(m_st->poll);
        m_st->show = m_st->sidebar = m_st->reload = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

} // namespace

void disksApp(Flex* card, Popup& popup, const std::string& arg) {
    (void)popup;
    auto st = std::make_shared<State>();
    card->setPadding(Insets(12))->setSpacing(8);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setSpacing(12)->setAlignItems(Align::Stretch);
    columns->grow = true;
    st->side = columns->add<Flex>(Flex::Direction::Column);
    st->side->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8});
    st->side->fixedWidth = 240;
    auto* right = columns->add<Flex>(Flex::Direction::Column);
    right->setSpacing(10)->setAlignItems(Align::Stretch);
    right->grow = true;
    st->dialog = right->add<Flex>(Flex::Direction::Column);
    st->dialog->setSpacing(12)->setPadding({18, 16})->setRadius(Radii(24))->setBackground("surface_container_highest", false)->setAlignItems(Align::Stretch);
    st->dialog->setVisible(false);
    auto* scroll = right->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    scroll->blurEdges = true;
    st->content = scroll->add<Flex>(Flex::Direction::Column);
    st->content->setSpacing(12)->setAlignItems(Align::Stretch)->setPadding({4, 4, 16, 12});
    card->add<DisksKeeper>(st);

    st->sidebar = [st] {
        auto* s = st->side;
        s->clearChildren();
        auto* brand = s->add<Label>("Disks");
        brand->setSize(30)->setWeight(400);
        brand->alignSelf = Align::Start;
        s->add<Node>()->fixedHeight = 6;
        for (int i = 0; i < static_cast<int>(st->disks.size()); ++i) {
            const auto& d = st->disks[static_cast<size_t>(i)];
            const bool here = st->view == i;
            auto* b = s->add<Button>(Flex::Direction::Row);
            b->setPadding({14, 9})->setSpacing(12)->setRadius(Radii(20));
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(iconOf(d), 22)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            auto* t = b->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start);
            t->grow = true;
            t->add<Label>(d.model.empty() ? d.name : d.model)->setSize(13)->setWeight(here ? 600 : 500)->setMaxWidth(160);
            t->add<Label>(std::format("{}  ·  {}", kindOf(d), fileops::humanSize(d.size)))->setSize(11)->setColor("on_surface_variant", false);
            b->onClicked = [st, i](uint32_t btn) {
                if (btn != BTN_LEFT) return;
                st->view = i;
                st->dialog->setVisible(false);
                EventLoop::get().defer([st] {
                    if (*st->alive) st->sidebar(), st->show();
                });
            };
        }
        s->add<Node>()->fixedHeight = 8;
        auto* sp = s->add<Button>(Flex::Direction::Row);
        sp->setPadding({14, 10})->setSpacing(12)->setRadius(Radii(20));
        if (st->view == -1) sp->setBackground("secondary_container", false);
        sp->add<Icon>("donut_large", 22)->setColor(st->view == -1 ? "on_secondary_container" : "on_surface_variant", false);
        sp->add<Label>("Space used")->setSize(14)->setWeight(st->view == -1 ? 600 : 500);
        sp->onClicked = [st](uint32_t btn) {
            if (btn != BTN_LEFT) return;
            st->view = -1;
            EventLoop::get().defer([st] {
                if (*st->alive) st->sidebar(), st->show();
            });
        };
    };
    st->show = [st] {
        auto* c = st->content;
        c->clearChildren();
        if (!st->busy.empty()) caption(c, st->busy);
        if (st->view == -1) return spacePage(st, c);
        if (st->disks.empty()) {
            caption(c, "Reading your drives…");
            return;
        }
        if (st->view >= static_cast<int>(st->disks.size())) st->view = 0;
        diskPage(st, c, st->disks[static_cast<size_t>(st->view)]);
    };
    st->reload = [st] {
        m3::run({"lsblk", "-J", "-b", "-o", "NAME,PATH,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT,MODEL,TRAN,RM,ROTA,FSUSED,FSSIZE,PARTTYPENAME,UUID"},
                [st](int, const std::string& out) {
                    if (!*st->alive) return;
                    auto disks = readDisks(out);
                    // Unchanged: left as it is (a menu may be open)
                    static std::string last;
                    if (out == last && !st->disks.empty()) return;
                    last = out;
                    st->disks = std::move(disks);
                    if (st->sidebar) st->sidebar();
                    if (st->show) st->show();
                });
    };
    // Drives plugged in and out
    st->poll = EventLoop::get().addTimer(3000, [st] {
        if (*st->alive && st->reload) st->reload();
    }, true);
    if (arg == "space") st->view = -1;
    st->sidebar();
    st->show();
    st->reload();
}

void disksSettings(Flex* page) {
    auto* g = group(page, "Tools", "What Disks uses for each job (installed ones are ticked)");
    for (const auto& [cmd, pkg, what] : std::initializer_list<std::tuple<const char*, const char*, const char*>>{
             {"udisksctl", "udisks2", "Mounting, unmounting and powering off"},
             {"smartctl", "smartmontools", "Drive health (SMART)"},
             {"mkfs.exfat", "exfatprogs", "Formatting as exFAT"},
             {"mkfs.fat", "dosfstools", "Formatting as FAT32"},
             {"mkfs.ntfs", "ntfs-3g", "Formatting as NTFS"},
             {"mkfs.ext4", "e2fsprogs", "Formatting as ext4"}}) {
        auto r = row(g, haveCommand(cmd) ? "check_circle" : "radio_button_unchecked", what, haveCommand(cmd) ? std::string(pkg) + ": installed" : std::string("sudo pacman -S ") + pkg);
        (void)r;
    }
    caption(page, "The system's own partitions (/, /boot, /home, swap) can't be formatted from Disks");
}

} // namespace m3::shell::panels
