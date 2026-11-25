// Image Writer: a disk image (a Linux ISO, a Raspberry Pi .img.xz) onto a USB
// drive or memory card, to start a computer from. The image comes from
// Files, a drop on the window, or the list of those in Downloads and the
// like; it can be checked against its SHA-256 sum first. Only removable
// drives are offered, never one holding the system, and none too small;
// writing erases the drive, so it's asked twice. dd writes it as root
// (pkexec, the shell's polkit prompt), decompressing on the way, its
// progress read from a file it reports to; then the drive can be read back
// and compared. It's the `material3ui-imagewriter` app.
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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

using Clock = std::chrono::steady_clock;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The program that unpacks it ("" for a plain image)
std::string decompressorFor(const std::string& path) {
    const std::string p = lower(path);
    if (p.ends_with(".xz")) return "xz";
    if (p.ends_with(".gz")) return "gzip";
    if (p.ends_with(".zst")) return "zstd";
    return "";
}

bool isImage(const std::string& path) {
    std::string p = lower(path);
    for (const char* z : {".xz", ".gz", ".zst"})
        if (p.ends_with(z)) p.resize(p.size() - std::strlen(z));
    return p.ends_with(".iso") || p.ends_with(".img") || p.ends_with(".raw") || p.ends_with(".bin");
}

struct Image {
    std::string path;
    uint64_t fileSize = 0;
    uint64_t size = 0; // what's written: the file, or what it unpacks to (0: not known yet)
};

struct Drive {
    std::string path, name, model, tran;
    uint64_t size = 0;
    bool removable = false, system = false;
    std::vector<std::string> mounted; // its partitions' mount points
};

std::string driveTitle(const Drive& d) {
    if (!d.model.empty()) return d.model;
    if (d.tran == "usb") return "USB drive";
    if (d.name.starts_with("mmcblk")) return "Memory card";
    return d.name;
}

std::string driveIcon(const Drive& d) {
    if (d.name.starts_with("mmcblk")) return "sd_card";
    if (d.tran == "usb") return "usb";
    return "hard_drive";
}

// A test drive that's a file (M3_IMAGEWRITER_FAKE=/path/to/file): written
// without root, nothing real touched
std::string fakeTarget() {
    const char* f = std::getenv("M3_IMAGEWRITER_FAKE");
    return f && *f ? f : "";
}

std::vector<Drive> parseDrives(const std::string& out) {
    std::vector<Drive> drives;
    try {
        const json j = json::parse(out);
        for (const auto& d : j.value("blockdevices", json::array())) {
            if (d.value("type", "") != "disk") continue;
            Drive x;
            x.name = d.value("name", "");
            if (x.name.starts_with("zram") || x.name.starts_with("loop") || x.name.starts_with("ram")) continue;
            x.path = d.value("path", "/dev/" + x.name);
            auto str = [](const json& o, const char* k) { return o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); };
            x.model = str(d, "model");
            while (!x.model.empty() && x.model.back() == ' ') x.model.pop_back();
            x.tran = str(d, "tran");
            x.size = d.contains("size") && d["size"].is_number() ? d["size"].get<uint64_t>() : 0;
            auto truthy = [](const json& v) { return v.is_boolean() ? v.get<bool>() : v.is_string() ? v.get<std::string>() == "1" : v.is_number() && v.get<int>() != 0; };
            x.removable = (d.contains("rm") && truthy(d["rm"])) || (d.contains("hotplug") && truthy(d["hotplug"])) || x.tran == "usb" || x.tran == "mmc" ||
                          x.name.starts_with("mmcblk");
            std::function<void(const json&)> walk = [&](const json& n) {
                const std::string m = str(n, "mountpoint"), pt = str(n, "parttypename");
                if (!m.empty()) x.mounted.push_back(m);
                if (m == "/" || m == "/boot" || m == "/boot/efi" || m == "/efi" || m == "/home" || m == "/usr" || m == "/var" || m == "[SWAP]" || pt == "EFI System")
                    x.system = true;
                for (const auto& c : n.value("children", json::array())) walk(c);
            };
            walk(d);
            if (x.size == 0) continue; // an empty card reader
            drives.push_back(std::move(x));
        }
    } catch (...) {
    }
    return drives;
}

struct WState {
    Image image;
    std::vector<Drive> drives;
    std::string chosen; // a drive's path
    enum class Phase { Choose, Confirm, Writing, Verifying, Done, Failed } phase = Phase::Choose;
    std::string error, progressFile, checksum, checksumNote;
    bool hashing = false;
    uint64_t written = 0;
    double speed = 0; // bytes a second
    Clock::time_point started;
    Flex* body = nullptr;
    TextField* expected = nullptr;
    std::string expectedText;
    std::function<void()> show;
    EventLoop::TimerId poll = 0, tick = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    const Drive* drive() const {
        for (const auto& d : drives)
            if (d.path == chosen) return &d;
        return nullptr;
    }
};

class WriterKeeper : public Node {
public:
    explicit WriterKeeper(std::shared_ptr<WState> st) : m_st(std::move(st)) { floating = true; }
    ~WriterKeeper() override {
        *m_st->alive = false;
        if (m_st->poll) EventLoop::get().cancel(m_st->poll);
        if (m_st->tick) EventLoop::get().cancel(m_st->tick);
        m_st->show = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<WState> m_st;
};

void notify(const std::string& title, const std::string& body = {}) {
    m3::run({"notify-send", "-a", "Image Writer", "-i", "media-removable", title, body}, nullptr);
}

void refresh(std::shared_ptr<WState> st) {
    if (st->show) st->show();
}

// The drives, read again every two seconds while choosing (plugged in, out)
void readDrives(std::shared_ptr<WState> st) {
    if (!fakeTarget().empty()) {
        Drive d;
        d.path = fakeTarget();
        d.name = "fake";
        d.model = "Test drive (a file)";
        d.tran = "usb";
        d.removable = true;
        d.size = 64ull << 30;
        if (st->drives.size() == 1 && st->drives.front().path == d.path) return; // unchanged: no rebuild under the pointer
        st->drives = {d};
        if (st->chosen.empty()) st->chosen = d.path;
        refresh(st);
        return;
    }
    m3::run({"lsblk", "-J", "-b", "-o", "NAME,PATH,SIZE,TYPE,MODEL,TRAN,RM,HOTPLUG,MOUNTPOINT,PARTTYPENAME"}, [st](int code, const std::string& out) {
        if (!*st->alive || code != 0) return;
        auto drives = parseDrives(out);
        const bool all = Config::get().value<bool>("/imagewriter/allDrives", false);
        std::erase_if(drives, [all](const Drive& d) { return d.system || (!all && !d.removable); });
        const bool same = drives.size() == st->drives.size() &&
                          std::ranges::equal(drives, st->drives, [](const Drive& a, const Drive& b) { return a.path == b.path && a.size == b.size && a.mounted == b.mounted; });
        st->drives = std::move(drives);
        if (!st->drive()) st->chosen.clear();
        if (st->chosen.empty() && st->drives.size() == 1) st->chosen = st->drives.front().path;
        if (!same && (st->phase == WState::Phase::Choose || st->phase == WState::Phase::Confirm)) refresh(st);
    });
}

// What a compressed image unpacks to, when its format says
void measure(std::shared_ptr<WState> st) {
    const std::string path = st->image.path, z = decompressorFor(path);
    if (z.empty()) {
        st->image.size = st->image.fileSize;
        return;
    }
    std::vector<std::string> cmd = z == "xz" ? std::vector<std::string>{"xz", "--robot", "--list", path}
                                   : z == "gzip" ? std::vector<std::string>{"gzip", "-lq", path}
                                                 : std::vector<std::string>{"zstd", "-lv", path};
    m3::run(cmd, [st, path, z](int code, const std::string& out) {
        if (!*st->alive || st->image.path != path || code != 0) return;
        uint64_t n = 0;
        std::istringstream in(out);
        for (std::string line; std::getline(in, line);) {
            std::istringstream w(line);
            std::vector<std::string> f;
            for (std::string x; w >> x;) f.push_back(x);
            if (z == "xz" && f.size() >= 5 && f[0] == "totals") n = std::strtoull(f[4].c_str(), nullptr, 10);
            if (z == "gzip" && f.size() >= 2) n = std::strtoull(f[1].c_str(), nullptr, 10); // "compressed uncompressed ratio name" (mod 4 GiB)
            if (z == "zstd" && line.find("Decompressed Size") != std::string::npos) {
                const auto open = line.find('('), end = line.find(" B)");
                if (open != std::string::npos && end != std::string::npos) {
                    std::string digits;
                    for (char c : line.substr(open + 1, end - open - 1))
                        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
                    n = std::strtoull(digits.c_str(), nullptr, 10);
                }
            }
        }
        // gzip's count wraps at 4 GiB: believed only when bigger than the file
        if (n > st->image.fileSize) st->image.size = n;
        refresh(st);
    });
}

void setImage(std::shared_ptr<WState> st, const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return;
    st->image = {path, static_cast<uint64_t>(fs::file_size(path, ec)), 0};
    st->checksum.clear();
    st->checksumNote.clear();
    st->hashing = false;
    if (st->phase != WState::Phase::Writing && st->phase != WState::Phase::Verifying) st->phase = WState::Phase::Choose;
    measure(st);
    refresh(st);
}

// Images lying about: Downloads, Desktop, Documents, home (a level down), newest first
std::vector<std::string> foundImages() {
    struct F {
        std::string path;
        fs::file_time_type when;
    };
    std::vector<F> found;
    std::error_code ec;
    for (const auto& p : fileops::places()) {
        if (p.name == "Music" || p.name == "Pictures" || p.name == "Videos") continue;
        const bool home = p.path == paths::home();
        for (fs::recursive_directory_iterator it(p.path, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            if (it.depth() >= (home ? 0 : 1)) it.disable_recursion_pending();
            const std::string name = it->path().filename().string();
            if (name.starts_with(".")) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file(ec) && isImage(it->path().string())) found.push_back({it->path().string(), it->last_write_time(ec)});
        }
    }
    std::ranges::sort(found, std::greater{}, &F::when);
    std::vector<std::string> out;
    for (auto& f : found)
        if (std::ranges::find(out, f.path) == out.end()) out.push_back(f.path);
    if (out.size() > 8) out.resize(8);
    return out;
}

// SHA-256 of the image, and what it's checked against: a sums file beside
// it (SHA256SUMS, *.sha256…), or what was pasted
void checksum(std::shared_ptr<WState> st) {
    if (st->hashing) return;
    st->hashing = true;
    st->checksum.clear();
    st->checksumNote.clear();
    refresh(st);
    const std::string path = st->image.path;
    m3::run({"sha256sum", "--", path}, [st, path](int code, const std::string& out) {
        if (!*st->alive || st->image.path != path) return;
        st->hashing = false;
        if (code != 0 || out.size() < 64) {
            st->checksumNote = "Couldn't read the image";
            return refresh(st);
        }
        st->checksum = out.substr(0, 64);
        // A sums file beside it naming it
        const fs::path dir = fs::path(path).parent_path();
        const std::string name = fs::path(path).filename().string();
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const std::string n = lower(e.path().filename().string());
            if (!(n.find("sha256") != std::string::npos || n.ends_with(".sha256sum") || n == "checksums")) continue;
            if (e.file_size(ec) > 1'000'000) continue;
            std::ifstream in(e.path());
            for (std::string line; std::getline(in, line);) {
                if (line.find(name) == std::string::npos && line.size() > 70) continue;
                std::string hex;
                for (char c : line) {
                    if (std::isxdigit(static_cast<unsigned char>(c))) hex += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    else if (hex.size() >= 64) break;
                    else hex.clear();
                }
                if (hex.size() != 64) continue;
                st->checksumNote = hex == st->checksum ? "match:" + e.path().filename().string() : "mismatch:" + e.path().filename().string();
                return refresh(st);
            }
        }
        refresh(st);
    });
}

void readProgress(std::shared_ptr<WState> st) {
    std::ifstream in(st->progressFile + (st->phase == WState::Phase::Verifying ? ".verify" : ".write"));
    if (!in) return;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // dd's lines, \r between: "123456789 bytes (123 MB, 117 MiB) copied, 2 s, 61.7 MB/s"
    size_t end = text.find_last_not_of("\r\n ");
    if (end == std::string::npos) return;
    const size_t start = text.find_last_of("\r\n", end);
    const std::string last = text.substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1) + 1);
    const uint64_t n = std::strtoull(last.c_str(), nullptr, 10);
    if (n > 0) {
        st->written = n;
        const double secs = std::chrono::duration<double>(Clock::now() - st->started).count();
        if (secs > 0.5) st->speed = static_cast<double>(n) / secs;
    }
}

void finish(std::shared_ptr<WState> st, int code, const std::string& out) {
    if (st->tick) EventLoop::get().cancel(st->tick), st->tick = 0;
    std::error_code ec;
    for (const char* s : {".write", ".verify", ".phase"}) fs::remove(st->progressFile + s, ec);
    const Drive* d = st->drive();
    const std::string drive = d ? driveTitle(*d) : "the drive";
    if (code == 0) {
        st->phase = WState::Phase::Done;
        notify("Ready to start from", fs::path(st->image.path).filename().string() + " is on " + drive);
        if (d && fakeTarget().empty() && Config::get().value<bool>("/imagewriter/autoEject", false))
            m3::run({"udisksctl", "power-off", "-b", d->path}, [st](int ok, const std::string&) {
                if (ok == 0) notify("Safe to unplug");
                if (*st->alive) readDrives(st);
            });
    } else if (code == 126 || code == 127) {
        st->phase = WState::Phase::Confirm; // the password was refused or cancelled
    } else {
        st->phase = WState::Phase::Failed;
        st->error = code == 3   ? "The drive doesn't read back the same as the image: it may be failing, or fake (smaller than it says). Try another drive."
                    : code == 4 ? "The drive is still in use and couldn't be unmounted. Close what's using it and try again."
                                : "Writing stopped: " + (out.empty() ? std::string("the drive was removed, or it's read-only") : out.substr(0, 240));
        notify("Couldn't write the image", st->error);
    }
    readDrives(st);
    refresh(st);
}

// The whole job, as root: unmount its partitions, write (unpacking on the
// way), flush, then read it back and compare
const char* kScript = R"sh(
img="$1"; dev="$2"; prog="$3"; verify="$4"; unpack="$5"; direct="$6"
src() { if [ -n "$unpack" ]; then "$unpack" -dc -- "$img"; else cat -- "$img"; fi; }
if [ -b "$dev" ]; then
  for p in $(lsblk -nlpo NAME "$dev" | tail -n +2) "$dev"; do
    if grep -q "^$p " /proc/mounts; then umount "$p" || exit 4; fi
  done
fi
echo write > "$prog.phase"
src | dd of="$dev" bs=4M iflag=fullblock $direct conv=fsync status=progress 2> "$prog.write" || { tail -c 300 "$prog.write" | tr '\r' '\n' | tail -n 2; exit 2; }
sync
if [ "$verify" = 1 ]; then
  echo verify > "$prog.phase"
  n=$(tr '\r' '\n' < "$prog.write" | grep -o '^[0-9][0-9]* bytes' | tail -n 1 | cut -d' ' -f1)
  [ -n "$n" ] || exit 0
  [ -b "$dev" ] && echo 1 > /proc/sys/vm/drop_caches 2>/dev/null
  src | cmp -n "$n" - "$dev" >/dev/null 2>&1 || exit 3
fi
exit 0
)sh";

void write(std::shared_ptr<WState> st) {
    const Drive* d = st->drive();
    if (!d || st->image.path.empty()) return;
    const bool fake = !fakeTarget().empty();
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    st->progressFile = std::format("{}/material3ui-imagewriter-{}", rt && *rt ? rt : "/tmp", getpid());
    const bool verify = Config::get().value<bool>("/imagewriter/verify", true);
    std::vector<std::string> cmd = {"sh", "-c", kScript, "sh", st->image.path, d->path, st->progressFile, verify ? "1" : "0",
                                    decompressorFor(st->image.path), fake ? "" : "oflag=direct"};
    if (!fake) cmd.insert(cmd.begin(), "pkexec");
    st->phase = WState::Phase::Writing;
    st->written = 0;
    st->speed = 0;
    st->error.clear();
    st->started = Clock::now();
    refresh(st);
    m3::run(cmd, [st](int code, const std::string& out) {
        if (*st->alive) finish(st, code, out);
    });
    // Progress twice a second
    st->tick = EventLoop::get().addTimer(500, [st] {
        if (!*st->alive) return;
        std::ifstream ph(st->progressFile + ".phase");
        std::string phase;
        ph >> phase;
        if (phase == "verify" && st->phase == WState::Phase::Writing) {
            st->phase = WState::Phase::Verifying;
            st->started = Clock::now();
        }
        if (phase == "write" && st->started + std::chrono::seconds(0) > Clock::now()) st->started = Clock::now();
        readProgress(st);
        refresh(st);
    }, true);
}

std::string human(uint64_t b) {
    return fileops::humanSize(b);
}

// ── The page ──

Flex* section(Flex* parent, const std::string& icon, const std::string& title) {
    auto* box = parent->add<Flex>(Flex::Direction::Column);
    box->setPadding(Insets(18))->setSpacing(10)->setRadius(Radii(24))->setAlignItems(Align::Stretch);
    box->setBackground(ColorRef("surface_container", 0.75f), false);
    auto* head = box->add<Flex>(Flex::Direction::Row);
    head->setSpacing(10)->setAlignItems(Align::Center);
    head->add<Icon>(icon, 20)->setColor("primary", false);
    head->add<Label>(title)->setSize(15)->setWeight(600);
    return box;
}

void imageSection(std::shared_ptr<WState> st, Flex* page, bool locked) {
    auto* box = section(page, "album", "Image");
    if (st->image.path.empty()) {
        caption(box, "Drop an ISO or IMG file here, open one from Files, or pick one found on this computer");
        const auto found = foundImages();
        for (const auto& f : found) {
            auto* b = box->add<Button>(Flex::Direction::Row);
            pill(b->setBackground("surface_container_high", false))->setPadding({14, 9})->setSpacing(12)->setAlignItems(Align::Center);
            b->add<Icon>("album", 20)->setColor("on_surface_variant", false);
            auto* t = b->add<Flex>(Flex::Direction::Column);
            t->setAlignItems(Align::Start);
            t->grow = true;
            t->givesWay = true;
            t->add<Label>(fs::path(f).filename().string())->setSize(14)->setWeight(500)->setMaxWidth(480);
            std::error_code ec;
            caption(t, std::format("{}  ·  {}", human(static_cast<uint64_t>(fs::file_size(f, ec))), fileops::displayPath(fs::path(f).parent_path().string())));
            b->onClicked = [st, f](uint32_t button) {
                if (button == BTN_LEFT) setImage(st, f);
            };
        }
        if (found.empty()) caption(box, "No images in Downloads, Desktop or Documents");
        return;
    }
    auto* row = box->add<Flex>(Flex::Direction::Row);
    row->setSpacing(12)->setAlignItems(Align::Center);
    auto* glyph = row->add<Flex>(Flex::Direction::Row);
    glyph->setJustify(Align::Center)->setAlignItems(Align::Center)->setRadius(Radii(14))->setBackground("primary_container", false);
    glyph->fixedWidth = 48, glyph->fixedHeight = 48;
    glyph->add<Icon>("album", 26)->setColor("on_primary_container", false);
    auto* t = row->add<Flex>(Flex::Direction::Column);
    t->setAlignItems(Align::Start)->setSpacing(2);
    t->grow = true;
    t->givesWay = true;
    t->add<Label>(fs::path(st->image.path).filename().string())->setSize(16)->setWeight(600)->setMaxWidth(440);
    const std::string z = decompressorFor(st->image.path);
    caption(t, z.empty() ? human(st->image.fileSize)
                         : std::format("{} compressed ({}){}", human(st->image.fileSize), z, st->image.size ? ", " + human(st->image.size) + " to write" : ""));
    if (!locked)
        pillButton(row, "Change", false, [st] {
            st->image = {};
            st->phase = WState::Phase::Choose;
            refresh(st);
        });

    // Its checksum
    if (!locked) {
        auto* sum = box->add<Flex>(Flex::Direction::Row);
        sum->setSpacing(8)->setAlignItems(Align::Center);
        if (st->checksum.empty()) {
            pillButton(sum, st->hashing ? "Reading the image…" : "Check SHA-256", false, [st] { checksum(st); }, "verified");
            caption(sum, st->checksumNote.empty() ? "Compares it with the sum its download page lists" : st->checksumNote);
        } else {
            auto* l = sum->add<Label>(st->checksum);
            l->setSize(11)->setFamily("monospace")->setColor("on_surface_variant", false)->setMaxWidth(560);
            l->grow = true;
        }
        if (!st->checksum.empty()) {
            const std::string note = st->checksumNote;
            if (note.starts_with("match:") || note.starts_with("mismatch:")) {
                const bool ok = note.starts_with("match:");
                auto* r = box->add<Flex>(Flex::Direction::Row);
                r->setSpacing(8)->setAlignItems(Align::Center);
                r->add<Icon>(ok ? "verified" : "gpp_bad", 18)->setFill(true)->setColor(ok ? "primary" : "error", false);
                r->add<Label>(ok ? "Matches " + note.substr(6) : "Doesn't match " + note.substr(9) + ": the download may be broken")
                    ->setSize(13)
                    ->setColor(ok ? "primary" : "error", false);
            } else {
                // Pasted from the download page
                auto* r = box->add<Flex>(Flex::Direction::Row);
                pill(r->setBackground("surface_container_high", false))->setPadding({14, 4})->setSpacing(8)->setAlignItems(Align::Center);
                auto* f = r->add<TextField>();
                f->bare = true;
                f->grow = true;
                f->setPlaceholder("Paste the SHA-256 from the download page");
                f->setText(st->expectedText);
                auto* mark = r->add<Icon>("", 18);
                auto judge = [st, mark](const std::string& text) {
                    std::string hex;
                    for (char c : text)
                        if (std::isxdigit(static_cast<unsigned char>(c))) hex += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    st->expectedText = text;
                    if (hex.size() != 64) return void(mark->setText(""));
                    const bool ok = hex == st->checksum;
                    mark->setText(ok ? "check_circle" : "cancel");
                    mark->setFill(true)->setColor(ok ? "primary" : "error", false);
                };
                judge(st->expectedText);
                f->onChanged = judge;
            }
        }
    }
}

void driveSection(std::shared_ptr<WState> st, Flex* page, bool locked) {
    auto* box = section(page, "usb", "Drive");
    if (locked) {
        if (const Drive* d = st->drive()) {
            auto* row = box->add<Flex>(Flex::Direction::Row);
            row->setSpacing(12)->setAlignItems(Align::Center);
            row->add<Icon>(driveIcon(*d), 24)->setColor("on_surface_variant", false);
            row->add<Label>(std::format("{}  ·  {}  ·  {}", driveTitle(*d), human(d->size), d->path))->setSize(14);
        }
        return;
    }
    if (st->drives.empty()) {
        caption(box, Config::get().value<bool>("/imagewriter/allDrives", false) ? "No drive to write to (the system's own are never offered)"
                                                                             : "Plug in a USB drive or a memory card");
        return;
    }
    for (const auto& d : st->drives) {
        const bool small = st->image.size && d.size < st->image.size;
        const bool on = d.path == st->chosen;
        auto* b = box->add<Button>(Flex::Direction::Row);
        pill(b->setBackground(on ? ColorRef("secondary_container") : ColorRef("surface_container_high"), false))->setPadding({14, 10})->setSpacing(12)->setAlignItems(Align::Center);
        b->add<Icon>(on ? "radio_button_checked" : "radio_button_unchecked", 20)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
        b->add<Icon>(driveIcon(d), 22)->setColor(on ? "on_secondary_container" : "on_surface_variant", false);
        auto* t = b->add<Flex>(Flex::Direction::Column);
        t->setAlignItems(Align::Start);
        t->grow = true;
        t->givesWay = true;
        t->add<Label>(driveTitle(d))->setSize(14)->setWeight(500)->setMaxWidth(420)->setColor(on ? "on_secondary_container" : "on_surface", false);
        std::string sub = human(d.size) + "  ·  " + d.path;
        if (!d.mounted.empty()) sub += "  ·  in use (unmounted first)";
        if (small) sub = "Too small for this image  ·  " + human(d.size);
        t->add<Label>(sub)->setSize(12)->setColor(small ? "error" : on ? "on_secondary_container" : "on_surface_variant", false);
        if (small) b->setOpacity(0.55f, false);
        b->onClicked = [st, p = d.path, small](uint32_t button) {
            if (button != BTN_LEFT || small) return;
            st->chosen = p;
            st->phase = WState::Phase::Choose;
            refresh(st);
        };
    }
}

void actionSection(std::shared_ptr<WState> st, Flex* page) {
    const Drive* d = st->drive();
    const bool ready = d && !st->image.path.empty() && !(st->image.size && d->size < st->image.size);
    using P = WState::Phase;
    switch (st->phase) {
    case P::Choose: {
        auto* row = page->add<Flex>(Flex::Direction::Row);
        row->setSpacing(10)->setAlignItems(Align::Center);
        auto* note = row->add<Label>(ready ? "Everything on the drive will be erased" : "Choose an image and a drive");
        note->setSize(13)->setColor("on_surface_variant", false);
        note->grow = true;
        auto* b = pillButton(row, "Write", true, [st, ready] {
            if (!ready) return;
            st->phase = WState::Phase::Confirm;
            refresh(st);
        }, "edit");
        b->setOpacity(ready ? 1.0f : 0.4f, false);
        break;
    }
    case P::Confirm: {
        auto* box = page->add<Flex>(Flex::Direction::Column);
        box->setPadding(Insets(18))->setSpacing(12)->setRadius(Radii(24))->setBackground("error_container", false)->setAlignItems(Align::Stretch);
        auto* head = box->add<Flex>(Flex::Direction::Row);
        head->setSpacing(10)->setAlignItems(Align::Center);
        head->add<Icon>("warning", 22)->setFill(true)->setColor("on_error_container", false);
        head->add<Label>(std::format("Erase {} ({})?", d ? driveTitle(*d) : "the drive", d ? human(d->size) : ""))->setSize(16)->setWeight(600)->setColor("on_error_container", false);
        box->add<Label>("Every file and partition on it is deleted for good, and the image takes its place. Your password is asked for next. Once it starts, let it finish.")
            ->setSize(13)
            ->setMaxWidth(600)
            ->setMaxLines(3)
            ->setColor("on_error_container", false);
        auto* row = box->add<Flex>(Flex::Direction::Row);
        row->setSpacing(8)->setJustify(Align::End);
        row->add<Node>()->grow = true;
        pillButton(row, "Cancel", false, [st] {
            st->phase = WState::Phase::Choose;
            refresh(st);
        });
        auto* go = pillButton(row, "Erase and write", true, [st] { write(st); }, "delete_forever");
        go->setBackground("error", false);
        break;
    }
    case P::Writing:
    case P::Verifying: {
        auto* box = page->add<Flex>(Flex::Direction::Column);
        box->setPadding(Insets(18))->setSpacing(10)->setRadius(Radii(24))->setBackground(ColorRef("surface_container", 0.75f), false)->setAlignItems(Align::Stretch);
        const bool verifying = st->phase == P::Verifying;
        const uint64_t total = st->image.size;
        const float frac = total ? std::clamp(static_cast<float>(static_cast<double>(st->written) / static_cast<double>(total)), 0.0f, 1.0f) : 0.0f;
        auto* head = box->add<Flex>(Flex::Direction::Row);
        head->setSpacing(10)->setAlignItems(Align::Center);
        head->add<Icon>(verifying ? "fact_check" : "edit", 20)->setColor("primary", false);
        auto* t = head->add<Label>(verifying ? "Checking the drive…" : st->written == 0 ? "Starting…" : "Writing…");
        t->setSize(15)->setWeight(600);
        t->grow = true;
        if (!verifying && total) head->add<Label>(std::format("{:.0f}%", frac * 100))->setSize(15)->setWeight(600)->setColor("primary", false);
        box->add<Meter>(verifying ? 1.0f : frac, ColorRef(verifying ? "tertiary" : "primary"), 10);
        std::string info;
        if (verifying) info = "Reading it back and comparing with the image";
        else {
            info = human(st->written) + (total ? " of " + human(total) : "");
            if (st->speed > 0) {
                info += std::format("  ·  {}/s", human(static_cast<uint64_t>(st->speed)));
                if (total > st->written) {
                    const auto left = static_cast<long>((static_cast<double>(total - st->written)) / st->speed);
                    info += left >= 60 ? std::format("  ·  about {} min left", (left + 59) / 60) : std::format("  ·  {} s left", left);
                }
            }
        }
        caption(box, info);
        caption(box, "Don't unplug the drive. The last part, flushing to it, can take a while.");
        break;
    }
    case P::Done: {
        auto* box = page->add<Flex>(Flex::Direction::Column);
        box->setPadding(Insets(20))->setSpacing(10)->setRadius(Radii(24))->setBackground("primary_container", false)->setAlignItems(Align::Center);
        box->add<Icon>("check_circle", 44)->setFill(true)->setColor("on_primary_container", false);
        box->add<Label>("Ready to start from")->setSize(18)->setWeight(600)->setColor("on_primary_container", false);
        box->add<Label>(std::format("{} is on {}{}", fs::path(st->image.path).filename().string(), d ? driveTitle(*d) : "the drive",
                                    Config::get().value<bool>("/imagewriter/verify", true) ? ", checked" : ""))
            ->setSize(13)
            ->setMaxWidth(560)
            ->setCentered(true)
            ->setMaxLines(2)
            ->setColor("on_primary_container", false);
        auto* row = box->add<Flex>(Flex::Direction::Row);
        row->setSpacing(8);
        if (d && fakeTarget().empty())
            pillButton(row, "Eject", true, [st, path = d->path] {
                m3::run({"udisksctl", "power-off", "-b", path}, [st](int code, const std::string& out) {
                    if (code == 0) notify("Safe to unplug");
                    else notify("Couldn't eject", out.substr(0, 200));
                    if (*st->alive) readDrives(st);
                });
            }, "eject");
        pillButton(row, "Write another", false, [st] {
            st->phase = WState::Phase::Choose;
            st->image = {};
            refresh(st);
        });
        break;
    }
    case P::Failed: {
        auto* box = page->add<Flex>(Flex::Direction::Column);
        box->setPadding(Insets(18))->setSpacing(10)->setRadius(Radii(24))->setBackground("error_container", false)->setAlignItems(Align::Stretch);
        auto* head = box->add<Flex>(Flex::Direction::Row);
        head->setSpacing(10)->setAlignItems(Align::Center);
        head->add<Icon>("error", 22)->setFill(true)->setColor("on_error_container", false);
        head->add<Label>("Couldn't write the image")->setSize(16)->setWeight(600)->setColor("on_error_container", false);
        box->add<Label>(st->error)->setSize(13)->setMaxWidth(600)->setMaxLines(4)->setColor("on_error_container", false);
        auto* row = box->add<Flex>(Flex::Direction::Row);
        row->add<Node>()->grow = true;
        pillButton(row, "Try again", true, [st] {
            st->phase = WState::Phase::Choose;
            refresh(st);
        });
        break;
    }
    }
}

void build(std::shared_ptr<WState> st) {
    auto* p = st->body;
    p->clearChildren();
    using P = WState::Phase;
    const bool locked = st->phase == P::Writing || st->phase == P::Verifying || st->phase == P::Done;
    imageSection(st, p, locked);
    driveSection(st, p, locked);
    actionSection(st, p);
}

} // namespace

std::function<void(const std::string&)> imagewriterGo;

void imagewriterApp(Flex* card, Popup& popup, const std::string& arg) {
    (void)popup;
    auto st = std::make_shared<WState>();
    card->setPadding(Insets(10))->setSpacing(8);
    card->add<WriterKeeper>(st);
    auto* scroll = card->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    scroll->blurEdges = true;
    st->body = scroll->add<Flex>(Flex::Direction::Column);
    st->body->setSpacing(12)->setAlignItems(Align::Stretch)->setPadding({14, 6, 14, 18});
    st->show = [st] { build(st); };

    auto take = [st](const std::string& path) {
        if (st->phase == WState::Phase::Writing || st->phase == WState::Phase::Verifying) return;
        if (isImage(path) || fs::path(path).extension().empty()) setImage(st, path);
        else notify("Not a disk image", fs::path(path).filename().string() + ": ISO, IMG (or .img.xz, .gz, .zst) files are written");
    };
    card->onDropFiles = [take](const std::vector<std::string>& paths) {
        if (!paths.empty()) take(paths.front());
    };
    imagewriterGo = [st, take](const std::string& path) {
        if (*st->alive && !path.empty()) take(path);
    };

    if (!arg.empty()) {
        std::error_code ec;
        if (fs::is_regular_file(arg, ec)) setImage(st, arg);
    }
    build(st);
    readDrives(st);
    st->poll = EventLoop::get().addTimer(2000, [st] {
        if (*st->alive && (st->phase == WState::Phase::Choose || st->phase == WState::Phase::Confirm)) readDrives(st);
    }, true);
}

void imagewriterClosed() {
    imagewriterGo = nullptr;
}

// Settings → Image Writer
void imagewriterSettings(Flex* page) {
    auto* g = group(page, "Writing");
    switchRow(g, "fact_check", "Check the drive afterwards", "Read it back and compare with the image: slower, but catches failing and fake drives",
              "/imagewriter/verify", true);
    switchRow(g, "eject", "Eject when it's done", "Power the drive off after a good write, ready to unplug", "/imagewriter/autoEject", false);
    switchRow(g, "hard_drive", "Offer every drive", "Not only USB drives and memory cards (the system's own are never offered)", "/imagewriter/allDrives", false);
}

} // namespace m3::shell::panels
