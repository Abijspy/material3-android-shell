// Task Manager: what's running and how hard the computer is working, in an
// app window of the shell's own (Ctrl+Alt+Del, `material3ui msg tasks`).
//
//  Processes   apps (the processes with windows, with everything they
//              started), your background processes, and the system's;
//              search, sort, and each one's details and actions (stop,
//              end, restart, force)
//  Performance CPU (each core too), memory and swap, disks (reading and
//              writing), the network, and the GPU where it says, as graphs
//  Startup     the apps that start when you log in (XDG autostart), each
//              with a switch, and adding one
//  Services    systemd's, the system's and yours: start, stop, restart,
//              enable and disable (polkit asks where it must)
#include "shell/Panels.h"

#include "core/Config.h"
#include "core/EventLoop.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "render/Images.h"
#include "render/Renderer.h"
#include "services/Compositor.h"
#include "services/FileOps.h"
#include "services/Network.h"
#include "services/Search.h"
#include "services/SystemStats.h"
#include "shell/PanelKit.h"
#include "shell/Popup.h"
#include "shell/SettingsKit.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <algorithm>
#include <csignal>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <linux/input-event-codes.h>
#include <map>
#include <memory>
#include <pwd.h>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3::shell::panels {

using namespace m3::ui;
using namespace m3::shell::kit;

namespace {

std::string bytes(double b) {
    return fileops::humanSize(static_cast<std::uintmax_t>(std::max(0.0, b)));
}
std::string rate(double bps) {
    return bytes(bps) + "/s";
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string readLine(const std::string& path) {
    std::ifstream in(path);
    std::string s;
    std::getline(in, s);
    return s;
}

// ── What the Performance tab draws, sampled each second while it's open ──

constexpr size_t kHistory = 90;

struct Series {
    std::deque<double> values;
    void push(double v) {
        values.push_back(v);
        while (values.size() > kHistory) values.pop_front();
    }
    double last() const { return values.empty() ? 0 : values.back(); }
    double peak() const { return values.empty() ? 0 : *std::ranges::max_element(values); }
};

struct Sampler {
    // CPU: all of it and each core, from /proc/stat
    Series cpu;
    std::vector<double> cores;
    std::vector<std::pair<uint64_t, uint64_t>> lastCpu; // idle, total, each line
    double mhz = 0;
    // Memory
    Series mem, swap;
    // Disks: bytes read and written each second, from /proc/diskstats
    Series diskRead, diskWrite;
    uint64_t lastRead = 0, lastWrite = 0;
    bool diskPrimed = false;
    // Network
    Series netDown, netUp;
    // GPU (amdgpu says how busy it is, and its memory)
    Series gpu;
    std::string gpuBusyPath, gpuVramUsed, gpuVramTotal;
    double vramUsed = 0, vramTotal = 0;

    Sampler() {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
            const std::string p = e.path().string() + "/device/gpu_busy_percent";
            if (fs::exists(p, ec)) {
                gpuBusyPath = p;
                gpuVramUsed = e.path().string() + "/device/mem_info_vram_used";
                gpuVramTotal = e.path().string() + "/device/mem_info_vram_total";
                break;
            }
        }
    }
    bool hasGpu() const { return !gpuBusyPath.empty(); }

    void sample() {
        // CPU, each line: idle and total since the last time
        std::ifstream st("/proc/stat");
        std::vector<std::pair<uint64_t, uint64_t>> now;
        for (std::string line; std::getline(st, line) && line.starts_with("cpu");) {
            std::istringstream ss(line.substr(line.find(' ')));
            uint64_t v, total = 0, idle = 0;
            for (int i = 0; ss >> v; ++i) {
                total += v;
                if (i == 3 || i == 4) idle += v; // idle, iowait
            }
            now.push_back({idle, total});
        }
        if (lastCpu.size() == now.size()) {
            cores.clear();
            for (size_t i = 0; i < now.size(); ++i) {
                const double dt = static_cast<double>(now[i].second - lastCpu[i].second), di = static_cast<double>(now[i].first - lastCpu[i].first);
                const double use = dt > 0 ? std::clamp(100.0 * (1 - di / dt), 0.0, 100.0) : 0;
                if (i == 0) cpu.push(use);
                else cores.push_back(use);
            }
        }
        lastCpu = now;
        // The cores' clock, on average
        std::ifstream info("/proc/cpuinfo");
        double sum = 0;
        int n = 0;
        for (std::string line; std::getline(info, line);)
            if (line.starts_with("cpu MHz")) sum += std::atof(line.substr(line.find(':') + 1).c_str()), ++n;
        mhz = n ? sum / n : 0;

        auto& s = SystemStats::get();
        mem.push(s.memTotal() ? 100.0 * static_cast<double>(s.memUsed()) / static_cast<double>(s.memTotal()) : 0);
        swap.push(s.swapTotal() ? 100.0 * static_cast<double>(s.swapUsed()) / static_cast<double>(s.swapTotal()) : 0);

        // Disks: the whole devices (nvme0n1, sda, vda, mmcblk0), not their parts
        std::ifstream ds("/proc/diskstats");
        uint64_t rd = 0, wr = 0;
        for (std::string line; std::getline(ds, line);) {
            std::istringstream ss(line);
            int major, minor;
            std::string name;
            uint64_t f[11] = {};
            ss >> major >> minor >> name;
            for (auto& x : f) ss >> x;
            const bool whole = (name.starts_with("nvme") && name.find('p') == std::string::npos) ||
                               ((name.starts_with("sd") || name.starts_with("vd") || name.starts_with("hd")) && !std::isdigit(static_cast<unsigned char>(name.back()))) ||
                               (name.starts_with("mmcblk") && name.find('p') == std::string::npos);
            if (!whole) continue;
            rd += f[2] * 512, wr += f[6] * 512; // sectors read, written
        }
        if (diskPrimed) diskRead.push(static_cast<double>(rd - lastRead)), diskWrite.push(static_cast<double>(wr - lastWrite));
        lastRead = rd, lastWrite = wr, diskPrimed = true;

        const auto& d = Network::get().details();
        netDown.push(d.rxRate);
        netUp.push(d.txRate);

        if (hasGpu()) {
            gpu.push(std::atof(readLine(gpuBusyPath).c_str()));
            vramUsed = std::atof(readLine(gpuVramUsed).c_str());
            vramTotal = std::atof(readLine(gpuVramTotal).c_str());
        }
    }
};

// A history as an area, newest on the right: one thin bar for each sample,
// its top edge brighter; `max` 0 scales to the peak
class AreaGraph : public Node {
public:
    const Series* series = nullptr;
    const Series* second = nullptr; // drawn over it, in the tertiary colour (writes, uploads)
    double max = 100;
    float height = 110;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, height}; }
    void doPaint(Renderer& r) override {
        const auto& b = rect();
        r.rect(b, Theme::get().color("surface_container_highest"), Radii(14));
        double top = max;
        if (top <= 0) {
            top = 1;
            if (series) top = std::max(top, series->peak());
            if (second) top = std::max(top, second->peak());
            top *= 1.15;
        }
        // Thirds, faintly
        const Color grid = Theme::get().color("outline_variant").withAlpha(0.35f);
        for (int i = 1; i < 4; ++i) r.rect({b.x + 6, b.y + b.h * static_cast<float>(i) / 4, b.w - 12, 1}, grid);
        auto draw = [&](const Series* s, const char* role) {
            if (!s || s->values.empty()) return;
            const Color c = Theme::get().color(role);
            const float bw = (b.w - 12) / static_cast<float>(kHistory);
            const size_t n = s->values.size();
            for (size_t i = 0; i < n; ++i) {
                const float h = static_cast<float>(std::clamp(s->values[i] / top, 0.0, 1.0)) * (b.h - 12);
                const float x = b.x + 6 + static_cast<float>(kHistory - n + i) * bw;
                if (h < 0.5f) continue;
                r.rect({x, b.bottom() - 6 - h, bw + 0.6f, h}, c.withAlpha(0.28f));
                r.rect({x, b.bottom() - 6 - h, bw + 0.6f, std::min(h, 2.5f)}, c);
            }
        };
        draw(series, "primary");
        draw(second, "tertiary");
    }
};

// Each core's load, as a row of short bars
class CoreBars : public Node {
public:
    const std::vector<double>* cores = nullptr;

protected:
    SizeF doMeasure(SizeF available) override { return {available.w, 44}; }
    void doPaint(Renderer& r) override {
        if (!cores || cores->empty()) return;
        const auto& b = rect();
        const float n = static_cast<float>(cores->size()), gap = 3, w = std::max(2.0f, (b.w - gap * (n - 1)) / n);
        const Color track = Theme::get().color("surface_container_highest"), fill = Theme::get().color("primary");
        for (size_t i = 0; i < cores->size(); ++i) {
            const float x = b.x + static_cast<float>(i) * (w + gap);
            r.rect({x, b.y, w, b.h}, track, Radii(std::min(w, 6.0f) / 2));
            const float h = static_cast<float>(std::clamp((*cores)[i] / 100.0, 0.0, 1.0)) * b.h;
            if (h > 1) r.rect({x, b.bottom() - h, w, h}, (*cores)[i] > 90 ? Theme::get().color("error") : fill, Radii(std::min(w, 6.0f) / 2));
        }
    }
};

// ── Processes ──

struct Proc {
    int pid = 0, ppid = 0, threads = 0;
    std::string name, user;
    double cpu = 0;
    uint64_t rss = 0;
    char state = '?';
    bool mine = false;
};

// What SystemStats sampled, with each one's parent and owner from /proc
std::vector<Proc> processes() {
    std::vector<Proc> out;
    static std::map<uid_t, std::string> users;
    for (const auto& p : SystemStats::get().allProcesses()) {
        Proc x;
        x.pid = p.pid, x.name = p.name, x.cpu = p.cpu, x.rss = p.rss, x.state = p.state, x.mine = p.mine;
        const std::string stat = readLine(std::format("/proc/{}/stat", p.pid));
        // After the name in brackets: state, ppid … threads (field 20)
        if (const size_t close = stat.rfind(')'); close != std::string::npos) {
            std::istringstream ss(stat.substr(close + 2));
            std::string field;
            for (int i = 3; ss >> field && i <= 20; ++i) {
                if (i == 4) x.ppid = std::atoi(field.c_str());
                if (i == 20) x.threads = std::atoi(field.c_str());
            }
        }
        struct stat s {};
        if (::stat(std::format("/proc/{}", p.pid).c_str(), &s) == 0) {
            auto it = users.find(s.st_uid);
            if (it == users.end()) {
                const passwd* pw = getpwuid(s.st_uid);
                it = users.emplace(s.st_uid, pw ? pw->pw_name : std::to_string(s.st_uid)).first;
            }
            x.user = it->second;
        }
        out.push_back(std::move(x));
    }
    return out;
}

void signal(const Proc& p, int sig) {
    if (p.mine) {
        ::kill(p.pid, sig);
        return;
    }
    m3::run({"pkexec", "kill", std::format("-{}", sig), std::to_string(p.pid)}, nullptr);
}

std::string commandLine(int pid) {
    std::ifstream in(std::format("/proc/{}/cmdline", pid), std::ios::binary);
    std::string out, arg;
    while (std::getline(in, arg, '\0')) out += (out.empty() ? "" : " ") + arg;
    return out;
}

// ── Startup: XDG autostart ──

struct Autostart {
    std::string id, name, comment, icon, exec, path; // path: the file in force (yours over the system's)
    bool enabled = true, system = false;
};

std::string autostartDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.config") + "/autostart";
}

std::map<std::string, std::string> desktopKeys(const std::string& file) {
    std::map<std::string, std::string> kv;
    std::ifstream in(file);
    bool main = false;
    for (std::string line; std::getline(in, line);) {
        if (line.starts_with("[")) {
            main = line == "[Desktop Entry]";
            continue;
        }
        const size_t eq = line.find('=');
        if (main && eq != std::string::npos && !kv.contains(line.substr(0, eq))) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return kv;
}

std::vector<Autostart> autostarts() {
    std::map<std::string, Autostart> byId;
    std::error_code ec;
    for (const auto& dir : {std::string("/etc/xdg/autostart"), autostartDir()}) {
        const bool system = dir.starts_with("/etc");
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".desktop") continue;
            const auto kv = desktopKeys(e.path().string());
            Autostart a;
            a.id = e.path().filename().string();
            a.path = e.path().string();
            a.system = system && !byId.contains(a.id);
            a.name = kv.contains("Name") ? kv.at("Name") : a.id;
            a.comment = kv.contains("Comment") ? kv.at("Comment") : "";
            a.icon = kv.contains("Icon") ? kv.at("Icon") : "";
            a.exec = kv.contains("Exec") ? kv.at("Exec") : "";
            a.enabled = !(kv.contains("Hidden") && kv.at("Hidden") == "true") &&
                        !(kv.contains("X-GNOME-Autostart-enabled") && kv.at("X-GNOME-Autostart-enabled") == "false");
            // Only for some desktops (GNOME, KDE): not niri's
            const std::string only = kv.contains("OnlyShowIn") ? kv.at("OnlyShowIn") : "";
            if (!only.empty() && only.find("niri") == std::string::npos) continue;
            const std::string not_ = kv.contains("NotShowIn") ? kv.at("NotShowIn") : "";
            if (not_.find("niri") != std::string::npos) continue;
            if (!system && byId.contains(a.id)) a.system = byId[a.id].system;
            byId[a.id] = a;
        }
    }
    std::vector<Autostart> out;
    for (auto& [id, a] : byId) out.push_back(a);
    std::ranges::sort(out, [](const Autostart& a, const Autostart& b) { return lower(a.name) < lower(b.name); });
    return out;
}

// Switched: your copy of the entry says so (Hidden), the system's left as it is
void setAutostart(const Autostart& a, bool on) {
    std::error_code ec;
    fs::create_directories(autostartDir(), ec);
    const std::string mine = autostartDir() + "/" + a.id;
    std::ifstream in(a.path);
    std::string text, line;
    bool inMain = false, wrote = false;
    while (std::getline(in, line)) {
        if (line.starts_with("[")) {
            if (inMain && !wrote) text += std::string("Hidden=") + (on ? "false" : "true") + "\n", wrote = true;
            inMain = line == "[Desktop Entry]";
        } else if (inMain && (line.starts_with("Hidden=") || line.starts_with("X-GNOME-Autostart-enabled="))) {
            if (line.starts_with("Hidden=")) text += std::string("Hidden=") + (on ? "false" : "true") + "\n", wrote = true;
            continue;
        }
        text += line + "\n";
    }
    if (!wrote) text += std::string("Hidden=") + (on ? "false" : "true") + "\n";
    in.close();
    paths::writeFile(mine, text);
}

// ── Services: systemd's ──

struct Service {
    std::string unit, load, active, sub, description, enabled;
};

std::vector<Service> parseUnits(const std::string& units, const std::string& files) {
    std::map<std::string, std::string> enabled;
    std::istringstream fs_(files);
    for (std::string line; std::getline(fs_, line);) {
        std::istringstream ss(line);
        std::string unit, state;
        if (ss >> unit >> state) enabled[unit] = state;
    }
    std::vector<Service> out;
    std::set<std::string> seen;
    std::istringstream us(units);
    for (std::string line; std::getline(us, line);) {
        std::istringstream ss(line);
        Service s;
        if (!(ss >> s.unit >> s.load >> s.active >> s.sub)) continue;
        if (s.unit.starts_with("●")) continue;
        std::getline(ss, s.description);
        s.description.erase(0, s.description.find_first_not_of(' '));
        s.enabled = enabled.contains(s.unit) ? enabled[s.unit] : "";
        seen.insert(s.unit);
        out.push_back(std::move(s));
    }
    // Installed but never loaded (disabled ones mostly)
    for (const auto& [unit, state] : enabled)
        if (!seen.contains(unit) && !unit.ends_with("@.service")) out.push_back({unit, "loaded", "inactive", "dead", "", state});
    std::ranges::sort(out, [](const Service& a, const Service& b) { return lower(a.unit) < lower(b.unit); });
    return out;
}

// ── The window ──

// The content, knowing when the pointer last moved over it: the lists aren't
// rebuilt then, so a click never lands half on a row that's being replaced
class Watcher : public Flex {
public:
    Watcher() : Flex(Direction::Column) {}
    bool hovering = false;
    EventLoop::Clock::time_point moved{};
    bool busy() const { return hovering && EventLoop::Clock::now() - moved < std::chrono::milliseconds(1500); }
    void onHoverWithin(bool in) override { hovering = in; }
    void onPointerMove(float, float) override { moved = EventLoop::Clock::now(); }
};

struct State {
    int tab = 0; // 0 processes, 1 performance, 2 startup, 3 services
    std::string query;
    int sort = 0;              // 0 CPU, 1 memory, 2 name
    int open = 0;              // the pid whose details show
    std::string openGroup;     // the app whose processes show
    int armed = 0;             // the pid whose Force was tapped once
    bool showSystem = false;   // the system's processes listed
    bool userServices = false; // Services: yours, not the system's
    std::string serviceQuery, openService;
    std::vector<Service> services;
    bool servicesLoading = false;
    Sampler sampler;
    Flex* side = nullptr;
    Watcher* content = nullptr;
    Label* glance = nullptr; // the sidebar's figures
    std::function<void()> show, sidebar;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    EventLoop::TimerId timer = 0;
    SystemStats::Lease stats;
    Network::Lease traffic;
};

class Keeper : public Node {
public:
    explicit Keeper(std::shared_ptr<State> st) : m_st(std::move(st)) { floating = true; }
    ~Keeper() override {
        *m_st->alive = false;
        if (m_st->timer) EventLoop::get().cancel(m_st->timer);
        m_st->show = m_st->sidebar = nullptr;
    }

protected:
    SizeF doMeasure(SizeF) override { return {0, 0}; }

private:
    std::shared_ptr<State> m_st;
};

Flex* tile(Node* parent) {
    auto* c = parent->add<Flex>(Flex::Direction::Column);
    c->setPadding(Insets(16))->setSpacing(10)->setAlignItems(Align::Stretch)->setRadius(Radii(24));
    c->setBackground("surface_container_high", false);
    return c;
}

Flex* tileHead(Flex* t, const std::string& icon, const std::string& title, const std::string& value) {
    auto* h = t->add<Flex>(Flex::Direction::Row);
    h->setSpacing(10)->setAlignItems(Align::Center);
    h->add<Icon>(icon, 22)->setColor("primary", false);
    auto* l = h->add<Label>(title);
    l->setSize(16)->setWeight(600);
    l->grow = true;
    h->add<Label>(value)->setSize(14)->setWeight(500)->setColor("on_surface_variant", false);
    return h;
}

void detailLine(Node* parent, const std::string& k, const std::string& v) {
    auto* r = parent->add<Flex>(Flex::Direction::Row);
    r->setSpacing(10);
    auto* a = r->add<Label>(k);
    a->setSize(12)->setColor("on_surface_variant", false);
    a->fixedWidth = 90;
    r->add<Label>(v)->setSize(13)->setMaxWidth(560)->setMaxLines(3);
}

// One process's row: name, figures; opened, its details and actions
void processRow(const std::shared_ptr<State>& st, Node* list, const Proc& p, int indent) {
    const bool stopped = p.state == 'T' || p.state == 't';
    const bool open = st->open == p.pid;
    auto* b = list->add<Button>(Flex::Direction::Row);
    b->setPadding({20.0f + static_cast<float>(indent) * 28, 8, 20, 8})->setSpacing(14)->setRadius(Radii(14));
    b->pressedRadius = 10;
    if (open) b->setBackground("secondary_container", false);
    b->add<Icon>(stopped ? "pause_circle" : p.mine ? "memory" : "admin_panel_settings", 18)->setColor(stopped ? "tertiary" : "on_surface_variant", false);
    auto* name = b->add<Label>(p.name);
    name->setSize(14)->setMaxWidth(320);
    name->grow = true;
    name->givesWay = true;
    for (const auto& [text, w] : {std::pair{std::format("{:.1f}%", p.cpu), 64.0f}, {bytes(static_cast<double>(p.rss)), 88.0f}, {std::to_string(p.pid), 64.0f}}) {
        auto* l = b->add<Label>(text);
        l->setSize(13)->setColor("on_surface_variant", false);
        l->fixedWidth = w;
    }
    const int pid = p.pid;
    b->onClicked = [st, pid](uint32_t button) {
        if (button != BTN_LEFT) return;
        st->open = st->open == pid ? 0 : pid;
        st->armed = 0;
        if (st->show) st->show();
    };
    if (!open) return;
    auto* d = list->add<Flex>(Flex::Direction::Column);
    d->setPadding({54.0f + static_cast<float>(indent) * 28, 6, 20, 12})->setSpacing(4)->setAlignItems(Align::Start);
    detailLine(d, "Command", commandLine(p.pid).empty() ? "(a kernel thread)" : commandLine(p.pid));
    detailLine(d, "User", p.user);
    detailLine(d, "State", stopped ? "stopped" : p.state == 'R' ? "running" : p.state == 'S' ? "sleeping" : p.state == 'D' ? "waiting on a disk" : p.state == 'Z' ? "a zombie (finished, not reaped)" : std::string(1, p.state));
    detailLine(d, "Threads", std::to_string(p.threads));
    detailLine(d, "Parent", std::format("PID {}", p.ppid));
    auto* actions = d->add<Flex>(Flex::Direction::Row);
    actions->setSpacing(8)->setPadding({0, 6});
    const Proc copy = p;
    pillButton(actions, stopped ? "Resume" : "Stop", false, [copy, stopped] { signal(copy, stopped ? SIGCONT : SIGSTOP); }, stopped ? "play_arrow" : "pause");
    pillButton(actions, "End", false, [copy] { signal(copy, SIGTERM); }, "close");
    const bool armed = st->armed == p.pid;
    auto* kill = pillButton(actions, armed ? "Tap again to force" : "Force kill", false, {}, "dangerous");
    kill->setBackground(armed ? ColorRef("error") : ColorRef("error_container"), false);
    for (auto& c : kill->children())
        if (auto* l = dynamic_cast<Label*>(c.get())) l->setColor(armed ? "on_error" : "on_error_container", false);
        else if (auto* i = dynamic_cast<Icon*>(c.get())) i->setColor(armed ? "on_error" : "on_error_container", false);
    kill->onClicked = [st, copy, armed](uint32_t button) {
        if (button != BTN_LEFT) return;
        if (!armed) {
            st->armed = copy.pid;
            if (st->show) st->show();
            return;
        }
        st->armed = 0;
        signal(copy, SIGKILL);
    };
}

void processesTab(const std::shared_ptr<State>& st, Flex* c) {
    auto* tools = c->add<Flex>(Flex::Direction::Row);
    tools->setSpacing(10)->setAlignItems(Align::Center);
    auto* search = tools->add<Flex>(Flex::Direction::Row);
    pill(search->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    search->grow = true;
    search->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* field = search->add<TextField>();
    field->setPlaceholder("Search by name, PID or user");
    field->bare = true;
    field->grow = true;
    field->setText(st->query);
    field->onChanged = [st](const std::string& t) {
        st->query = t;
        if (st->show) EventLoop::get().defer([st] {
            if (*st->alive && st->show) st->show();
        });
    };
    connectedGroup<int>(tools, st->sort, {{0, "CPU"}, {1, "Memory"}, {2, "Name"}}, [st](int s) {
        st->sort = s;
        if (st->show) st->show();
    });

    auto procs = processes();
    if (procs.empty()) {
        caption(c, "Reading processes…");
        return;
    }
    auto order = [st](std::vector<Proc>& v) {
        if (st->sort == 0) std::ranges::sort(v, std::greater{}, &Proc::cpu);
        else if (st->sort == 1) std::ranges::sort(v, std::greater{}, &Proc::rss);
        else std::ranges::sort(v, [](const Proc& a, const Proc& b) { return lower(a.name) < lower(b.name); });
    };
    const std::string q = lower(st->query);
    auto matches = [&q](const Proc& p) {
        return q.empty() || lower(p.name).find(q) != std::string::npos || std::to_string(p.pid).find(q) != std::string::npos || lower(p.user).find(q) != std::string::npos;
    };

    // Apps: each window's process and everything it started
    std::map<int, std::vector<int>> children;
    std::map<int, const Proc*> byPid;
    for (const auto& p : procs) children[p.ppid].push_back(p.pid), byPid[p.pid] = &p;
    struct App {
        std::string id, name, icon, title;
        std::vector<int> pids;
        double cpu = 0;
        uint64_t rss = 0;
    };
    std::map<std::string, App> apps; // by app id
    std::set<int> inApp;
    for (const auto& w : Compositor::get().windows(true)) {
        if (w.pid <= 0 || !byPid.contains(w.pid)) continue;
        auto& a = apps[w.appId.empty() ? std::to_string(w.pid) : w.appId];
        if (a.id.empty()) {
            a.id = w.appId;
            const auto* app = Apps::get().forClass(w.appId);
            a.name = app ? app->name : w.appId.starts_with("material3ui-") && !w.title.empty() ? w.title : w.appId.empty() ? byPid[w.pid]->name : w.appId;
            a.icon = Apps::get().iconForClass(w.appId);
            a.title = w.title;
        }
        // The window's process, then down its tree
        std::vector<int> todo = {w.pid};
        while (!todo.empty()) {
            const int pid = todo.back();
            todo.pop_back();
            if (!inApp.insert(pid).second) continue;
            a.pids.push_back(pid);
            for (int ch : children[pid]) todo.push_back(ch);
        }
    }
    for (auto& [id, a] : apps)
        for (int pid : a.pids) a.cpu += byPid[pid]->cpu, a.rss += byPid[pid]->rss;

    // Header for the figures
    auto* head = c->add<Flex>(Flex::Direction::Row);
    head->setPadding({20, 2})->setSpacing(14);
    auto* hn = head->add<Label>("Name");
    hn->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
    hn->grow = true;
    for (const auto& [t, w] : {std::pair{"CPU", 64.0f}, {"Memory", 88.0f}, {"PID", 64.0f}}) {
        auto* l = head->add<Label>(t);
        l->setSize(12)->setWeight(600)->setColor("on_surface_variant", false);
        l->fixedWidth = w;
    }
    auto* list = c->add<Flex>(Flex::Direction::Column);
    list->setSpacing(2)->setAlignItems(Align::Stretch);

    // Apps
    std::vector<App*> shownApps;
    for (auto& [id, a] : apps) {
        bool any = q.empty() || lower(a.name).find(q) != std::string::npos || lower(a.title).find(q) != std::string::npos;
        for (int pid : a.pids) any = any || matches(*byPid[pid]);
        if (any) shownApps.push_back(&a);
    }
    std::ranges::sort(shownApps, [st](const App* x, const App* y) {
        return st->sort == 0 ? x->cpu > y->cpu : st->sort == 1 ? x->rss > y->rss : lower(x->name) < lower(y->name);
    });
    caption(list, std::format("  Apps ({})", shownApps.size()));
    for (App* a : shownApps) {
        const bool open = st->openGroup == a->id;
        auto* b = list->add<Button>(Flex::Direction::Row);
        b->setPadding({16, 8, 20, 8})->setSpacing(12)->setRadius(Radii(16));
        b->setBackground(open ? "secondary_container" : "surface_container_high", false);
        b->add<Icon>(open ? "expand_more" : "chevron_right", 18)->setColor("on_surface_variant", false);
        const std::string icon = a->icon.empty() ? "application-x-executable" : a->icon;
        b->add<ImageView>(24, [icon](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().icon(icon, static_cast<int>(std::lround(24 * scale)));
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        auto* texts = b->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->grow = true;
        texts->givesWay = true;
        texts->add<Label>(std::format("{}  ({})", a->name, a->pids.size()))->setSize(14)->setWeight(600)->setMaxWidth(360);
        if (!a->title.empty()) texts->add<Label>(a->title)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(360);
        for (const auto& [text, w] : {std::pair{std::format("{:.1f}%", a->cpu), 64.0f}, {bytes(static_cast<double>(a->rss)), 88.0f}, {std::string(), 64.0f}}) {
            auto* l = b->add<Label>(text);
            l->setSize(13)->setWeight(500)->setColor("on_surface_variant", false);
            l->fixedWidth = w;
        }
        const std::string id = a->id;
        b->onClicked = [st, id](uint32_t button) {
            if (button != BTN_LEFT) return;
            st->openGroup = st->openGroup == id ? "" : id;
            if (st->show) st->show();
        };
        if (!open) continue;
        // Its actions, then its processes
        auto* actions = list->add<Flex>(Flex::Direction::Row);
        actions->setSpacing(8)->setPadding({54, 4, 20, 6});
        const std::vector<int> pids = a->pids;
        pillButton(actions, "End task", false, [id] {
            // Closed as its windows' close buttons would (it may ask to save)
            auto& comp = Compositor::get();
            for (const auto& w : comp.windows(true))
                if (w.appId == id) comp.closeWindow(w.id);
        }, "close");
        pillButton(actions, "Force quit", false, [pids] {
            for (int pid : pids) ::kill(pid, SIGKILL);
        }, "dangerous");
        std::vector<Proc> mine;
        for (int pid : a->pids) mine.push_back(*byPid[pid]);
        order(mine);
        for (const auto& p : mine) processRow(st, list, p, 1);
    }

    // Yours, in the background; then the system's
    std::vector<Proc> background, system;
    for (const auto& p : procs) {
        if (inApp.contains(p.pid) || !matches(p)) continue;
        (p.mine ? background : system).push_back(p);
    }
    order(background);
    order(system);
    caption(list, std::format("  Background processes ({})", background.size()));
    size_t n = 0;
    for (const auto& p : background) {
        if (++n > 200) break;
        processRow(st, list, p, 0);
    }
    auto* sysHead = list->add<Button>(Flex::Direction::Row);
    sysHead->setPadding({16, 10})->setSpacing(10)->setRadius(Radii(14));
    sysHead->add<Icon>(st->showSystem || !q.empty() ? "expand_more" : "chevron_right", 18)->setColor("on_surface_variant", false);
    sysHead->add<Label>(std::format("System processes ({})", system.size()))->setSize(13)->setWeight(600)->setColor("on_surface_variant", false);
    sysHead->onClicked = [st](uint32_t button) {
        if (button != BTN_LEFT) return;
        st->showSystem = !st->showSystem;
        if (st->show) st->show();
    };
    if (st->showSystem || !q.empty()) {
        n = 0;
        for (const auto& p : system) {
            if (++n > 300) break;
            processRow(st, list, p, 0);
        }
    }
}

void performanceTab(const std::shared_ptr<State>& st, Flex* c) {
    auto& s = st->sampler;
    auto& stats = SystemStats::get();
    // Two to a row, each half
    Flex* row = nullptr;
    int inRow = 0;
    auto card = [&](const std::string& icon, const std::string& title, const std::string& value) {
        if (!row || inRow == 2) {
            row = c->add<Flex>(Flex::Direction::Row);
            row->setSpacing(12)->setAlignItems(Align::Stretch);
            inRow = 0;
        }
        ++inRow;
        auto* t = tile(row);
        t->grow = true;
        t->fixedWidth = 1; // grown from nothing, so both halves match
        tileHead(t, icon, title, value);
        return t;
    };
    {
        auto* t = card("memory", "CPU", std::format("{:.0f}%", s.cpu.last()));
        auto* g = t->add<AreaGraph>();
        g->series = &s.cpu;
        auto* cores = t->add<CoreBars>();
        cores->cores = &s.cores;
        caption(t, std::format("{} cores  ·  {:.2f} GHz{}", stats.cores(), s.mhz / 1000, stats.temperature() > 0 ? std::format("  ·  {:.0f} °C", stats.temperature()) : ""));
    }
    {
        auto* t = card("memory_alt", "Memory", std::format("{} of {}", bytes(static_cast<double>(stats.memUsed())), bytes(static_cast<double>(stats.memTotal()))));
        auto* g = t->add<AreaGraph>();
        g->series = &s.mem;
        caption(t, stats.swapTotal() ? std::format("Swap: {} of {} ({:.0f}%)", bytes(static_cast<double>(stats.swapUsed())), bytes(static_cast<double>(stats.swapTotal())), s.swap.last())
                                     : "No swap");
    }
    {
        auto* t = card("hard_drive", "Disks", std::format("↓ {}  ↑ {}", rate(s.diskRead.last()), rate(s.diskWrite.last())));
        auto* g = t->add<AreaGraph>();
        g->series = &s.diskRead;
        g->second = &s.diskWrite;
        g->max = 0;
        caption(t, "Reading, and writing (the lighter)");
        for (const auto& d : stats.disks()) {
            auto* r = t->add<Flex>(Flex::Direction::Row);
            r->setSpacing(10)->setAlignItems(Align::Center);
            auto* l = r->add<Label>(d.mount);
            l->setSize(13)->setMaxWidth(140);
            l->grow = true;
            r->add<Label>(std::format("{} free of {}", bytes(static_cast<double>(d.total - d.used)), bytes(static_cast<double>(d.total))))->setSize(12)->setColor("on_surface_variant", false);
        }
    }
    {
        auto* t = card("lan", "Network", std::format("↓ {}  ↑ {}", rate(s.netDown.last()), rate(s.netUp.last())));
        auto* g = t->add<AreaGraph>();
        g->series = &s.netDown;
        g->second = &s.netUp;
        g->max = 0;
        const auto& d = Network::get().details();
        caption(t, d.iface.empty() ? "Not connected" : std::format("{}  ·  {}", d.iface, d.ipv4.empty() ? "no address" : d.ipv4));
        if (!d.iface.empty()) caption(t, std::format("{} down, {} up since it connected", bytes(static_cast<double>(d.rxTotal)), bytes(static_cast<double>(d.txTotal))));
    }
    if (s.hasGpu()) {
        auto* t = card("developer_board", "GPU", std::format("{:.0f}%", s.gpu.last()));
        auto* g = t->add<AreaGraph>();
        g->series = &s.gpu;
        if (s.vramTotal > 0) caption(t, std::format("Video memory: {} of {}", bytes(s.vramUsed), bytes(s.vramTotal)));
    }
    caption(c, std::format("Up for {}  ·  the last {} seconds", [] {
        const long u = static_cast<long>(SystemStats::get().uptime());
        return u >= 86400 ? std::format("{}d {}h", u / 86400, u % 86400 / 3600) : std::format("{}h {}m", u / 3600, u % 3600 / 60);
    }(), kHistory));
}

void startupTab(const std::shared_ptr<State>& st, Flex* c) {
    caption(c, "Apps that start when you log in (XDG autostart). niri's own startup commands are in Niri Settings → Startup apps");
    auto* list = c->add<Flex>(Flex::Direction::Column);
    list->setSpacing(2)->setAlignItems(Align::Stretch);
    const auto all = autostarts();
    if (all.empty()) caption(list, "Nothing starts by itself");
    for (const auto& a : all) {
        auto* r = list->add<Flex>(Flex::Direction::Row);
        r->setPadding({16, 10})->setSpacing(14)->setAlignItems(Align::Center)->setRadius(Radii(16));
        r->setBackground("surface_container_high", false);
        const std::string icon = a.icon.empty() ? "application-x-executable" : a.icon;
        r->add<ImageView>(28, [icon](float scale, int& w, int& h) -> GLuint {
            auto* t = Images::get().icon(icon, static_cast<int>(std::lround(28 * scale)));
            if (!t) return 0;
            w = t->width, h = t->height;
            return t->id;
        });
        auto* texts = r->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->grow = true;
        texts->add<Label>(a.name)->setSize(14)->setWeight(600)->setMaxWidth(420);
        texts->add<Label>(a.comment.empty() ? a.exec : a.comment)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(420)->setMaxLines(2);
        if (a.system) texts->add<Label>("From the system")->setSize(11)->setColor("tertiary", false);
        auto* sw = r->add<Switch>();
        sw->setChecked(a.enabled, false);
        sw->onToggled = [a](bool on) { setAutostart(a, on); };
        if (!a.system && fs::path(a.path).parent_path() == fs::path(autostartDir()))
            iconButton(r, "delete", [st, path = a.path] {
                std::error_code ec;
                fs::remove(path, ec);
                if (st->show) st->show();
            }, 20)->tooltip = "Take it out";
    }
    // Add one: any app, started at login
    std::vector<std::pair<std::string, std::string>> items;
    for (const auto& app : Apps::get().all()) items.push_back({app.path, app.name});
    std::ranges::sort(items, [](const auto& a, const auto& b) { return lower(a.second) < lower(b.second); });
    auto* add = c->add<Flex>(Flex::Direction::Row);
    add->setSpacing(10)->setAlignItems(Align::Center)->setPadding({4, 8});
    add->add<Icon>("add_circle", 22)->setColor("primary", false);
    auto* l = add->add<Label>("Start an app when you log in");
    l->setSize(14)->setWeight(500);
    l->grow = true;
    dropdown(add, add, "Choose", items, [st](const std::string& path) {
        std::error_code ec;
        fs::create_directories(autostartDir(), ec);
        fs::copy_file(path, autostartDir() + "/" + fs::path(path).filename().string(), fs::copy_options::overwrite_existing, ec);
        if (st->show) st->show();
    });
}

void loadServices(const std::shared_ptr<State>& st) {
    if (st->servicesLoading) return;
    st->servicesLoading = true;
    std::vector<std::string> scope;
    if (st->userServices) scope.push_back("--user");
    auto units = std::vector<std::string>{"systemctl"};
    units.insert(units.end(), scope.begin(), scope.end());
    auto files = units;
    for (const char* a : {"list-units", "--type=service", "--all", "--no-pager", "--plain", "--no-legend"}) units.push_back(a);
    for (const char* a : {"list-unit-files", "--type=service", "--no-pager", "--plain", "--no-legend"}) files.push_back(a);
    m3::run(units, [st, files](int, const std::string& u) {
        m3::run(files, [st, u](int, const std::string& f) {
            if (!*st->alive) return;
            st->services = parseUnits(u, f);
            st->servicesLoading = false;
            if (st->tab == 3 && st->show) st->show();
        });
    });
}

void servicesTab(const std::shared_ptr<State>& st, Flex* c) {
    auto* tools = c->add<Flex>(Flex::Direction::Row);
    tools->setSpacing(10)->setAlignItems(Align::Center);
    auto* search = tools->add<Flex>(Flex::Direction::Row);
    pill(search->setBackground(ColorRef("on_surface", 0.08f), false))->setPadding({16, 4, 8, 4})->setSpacing(8);
    search->grow = true;
    search->add<Icon>("search", 20)->setColor("on_surface_variant", false);
    auto* field = search->add<TextField>();
    field->setPlaceholder("Search services");
    field->bare = true;
    field->grow = true;
    field->setText(st->serviceQuery);
    field->onChanged = [st](const std::string& t) {
        st->serviceQuery = t;
        EventLoop::get().defer([st] {
            if (*st->alive && st->show) st->show();
        });
    };
    connectedGroup<int>(tools, st->userServices ? 1 : 0, {{0, "System"}, {1, "Yours"}}, [st](int v) {
        st->userServices = v == 1;
        st->services.clear();
        loadServices(st);
        if (st->show) st->show();
    });
    iconButton(tools, "refresh", [st] { loadServices(st); }, 22)->tooltip = "Read them again";
    if (st->services.empty()) {
        loadServices(st);
        caption(c, "Reading services…");
        return;
    }
    const std::string q = lower(st->serviceQuery);
    int running = 0;
    for (const auto& s : st->services) running += s.active == "active";
    caption(c, std::format("{} services, {} running", st->services.size(), running));
    auto* list = c->add<Flex>(Flex::Direction::Column);
    list->setSpacing(2)->setAlignItems(Align::Stretch);
    size_t shown = 0;
    for (const auto& s : st->services) {
        if (!q.empty() && lower(s.unit).find(q) == std::string::npos && lower(s.description).find(q) == std::string::npos) continue;
        if (++shown > 400) break;
        const bool on = s.active == "active", failed = s.active == "failed", open = st->openService == s.unit;
        auto* b = list->add<Button>(Flex::Direction::Row);
        b->setPadding({16, 8})->setSpacing(12)->setRadius(Radii(14));
        if (open) b->setBackground("secondary_container", false);
        auto* dot = b->add<Flex>(Flex::Direction::Row);
        pill(dot->setBackground(failed ? "error" : on ? "primary" : "outline_variant", false));
        dot->fixedWidth = dot->fixedHeight = 10;
        auto* texts = b->add<Flex>(Flex::Direction::Column);
        texts->setAlignItems(Align::Start)->setSpacing(1);
        texts->grow = true;
        texts->givesWay = true;
        texts->add<Label>(s.unit.substr(0, s.unit.size() - 8))->setSize(14)->setWeight(500)->setMaxWidth(380);
        if (!s.description.empty()) texts->add<Label>(s.description)->setSize(12)->setColor("on_surface_variant", false)->setMaxWidth(380);
        auto* state = b->add<Label>(failed ? "failed" : on ? s.sub : "stopped");
        state->setSize(12)->setColor(failed ? "error" : "on_surface_variant", false);
        state->fixedWidth = 80;
        const bool masked = s.enabled.starts_with("masked") || s.load == "masked";
        auto* en = b->add<Label>(masked ? "masked" : s.enabled.starts_with("enabled") ? "enabled" : s.enabled);
        en->setSize(12)->setColor("on_surface_variant", false);
        en->fixedWidth = 80;
        const std::string unit = s.unit;
        b->onClicked = [st, unit](uint32_t button) {
            if (button != BTN_LEFT) return;
            st->openService = st->openService == unit ? "" : unit;
            if (st->show) st->show();
        };
        if (!open) continue;
        auto* actions = list->add<Flex>(Flex::Direction::Row);
        actions->setSpacing(8)->setPadding({38, 4, 16, 10});
        actions->wrap = true;
        const bool user = st->userServices;
        auto act = [st, unit, user](const std::string& verb) {
            std::vector<std::string> cmd = {"systemctl"};
            if (user) cmd.push_back("--user");
            cmd.push_back(verb);
            cmd.push_back(unit);
            // The system's ask polkit (the shell's own prompt) for your password
            m3::run(cmd, [st, verb, unit](int code, const std::string&) {
                if (code != 0) m3::run({"notify-send", "-a", "Task Manager", "-i", "dialog-error", "Couldn't " + verb + " " + unit}, nullptr);
                if (*st->alive) {
                    st->services.clear();
                    loadServices(st);
                }
            });
        };
        if (masked) {
            caption(actions, "Masked: it can't be started until it's unmasked (systemctl unmask)");
        } else if (on) {
            pillButton(actions, "Stop", false, [act] { act("stop"); }, "stop");
            pillButton(actions, "Restart", false, [act] { act("restart"); }, "restart_alt");
        } else {
            pillButton(actions, "Start", true, [act] { act("start"); }, "play_arrow");
        }
        if (masked) {
        } else if (s.enabled == "enabled") pillButton(actions, "Don't start at boot", false, [act] { act("disable"); }, "block");
        else if (s.enabled == "disabled") pillButton(actions, "Start at boot", false, [act] { act("enable"); }, "check_circle");
        pillButton(actions, "Log", false, [unit, user] {
            const char* t = std::getenv("TERMINAL");
            spawn(std::format("{} -e journalctl {}-u {} -e", t && *t ? t : "alacritty", user ? "--user " : "", unit));
        }, "receipt_long");
    }
    if (shown == 0) caption(list, "No service matches");
}

} // namespace

void taskManager(Flex* card, Popup& popup, const std::string& tab) {
    auto st = std::make_shared<State>();
    st->tab = tab == "performance" ? 1 : tab == "startup" ? 2 : tab == "services" ? 3 : 0;
    st->stats = SystemStats::get().lease();
    st->traffic = Network::get().watchTraffic();
    card->setPadding(Insets(12))->setSpacing(0);
    auto* columns = card->add<Flex>(Flex::Direction::Row);
    columns->setAlignItems(Align::Stretch)->setSpacing(12);
    columns->grow = true;
    st->side = columns->add<Flex>(Flex::Direction::Column);
    st->side->setSpacing(4)->setAlignItems(Align::Stretch)->setPadding({4, 8, 4, 8});
    st->side->fixedWidth = 220;
    auto* scroll = columns->add<ScrollView>();
    scroll->maxHeight = 1e6f;
    scroll->grow = true;
    scroll->blurEdges = true;
    st->content = scroll->add<Watcher>();
    st->content->setSpacing(12)->setAlignItems(Align::Stretch)->setPadding({4, 4, 16, 8});
    card->add<Keeper>(st);

    st->sidebar = [st] {
        auto* s = st->side;
        s->clearChildren();
        auto* brand = s->add<Label>("Tasks");
        brand->setSize(30)->setWeight(400);
        brand->alignSelf = Align::Start;
        s->add<Node>()->fixedHeight = 6;
        static const std::tuple<const char*, const char*> tabs[] = {{"view_list", "Processes"}, {"monitoring", "Performance"}, {"rocket_launch", "Startup"}, {"settings_suggest", "Services"}};
        for (int i = 0; i < 4; ++i) {
            const auto& [icon, name] = tabs[i];
            const bool here = st->tab == i;
            auto* b = s->add<Button>(Flex::Direction::Row);
            pill(b)->setPadding({16, 11})->setSpacing(14);
            b->pressedRadius = 12;
            if (here) b->setBackground("secondary_container", false);
            b->add<Icon>(icon, 22)->setFill(here)->setColor(here ? "on_secondary_container" : "on_surface_variant", false);
            b->add<Label>(name)->setSize(14)->setWeight(here ? 600 : 500)->setColor(here ? "on_secondary_container" : "on_surface", false);
            b->onClicked = [st, i](uint32_t button) {
                if (button != BTN_LEFT) return;
                st->tab = i;
                EventLoop::get().defer([st] {
                    if (!*st->alive) return;
                    st->sidebar();
                    st->show();
                });
            };
        }
        // At a glance, under the tabs
        s->add<Node>()->grow = true;
        st->glance = caption(s, "");
    };
    auto glance = [st] {
        auto& stats = SystemStats::get();
        if (st->glance)
            st->glance->setText(std::format("CPU {:.0f}%  ·  memory {:.0f}%", st->sampler.cpu.last(),
                                            stats.memTotal() ? 100.0 * static_cast<double>(stats.memUsed()) / static_cast<double>(stats.memTotal()) : 0.0));
    };
    st->show = [st] {
        auto* c = st->content;
        c->clearChildren();
        static const char* titles[] = {"Processes", "Performance", "Startup apps", "Services"};
        c->add<Label>(titles[st->tab])->setSize(26)->setWeight(500);
        switch (st->tab) {
        case 0: processesTab(st, c); break;
        case 1: performanceTab(st, c); break;
        case 2: startupTab(st, c); break;
        default: servicesTab(st, c); break;
        }
    };
    // Each second: sampled; the live tabs drawn again (not while a search is typed into)
    st->sampler.sample();
    st->timer = EventLoop::get().addTimer(1000, [st, glance, tick = 0]() mutable {
        if (!*st->alive) return;
        st->sampler.sample();
        glance();
        if (st->tab == 1) st->show();
        else if (st->tab == 0 && ++tick % 2 == 0 && !st->content->busy()) st->show(); // processes every two seconds
    }, true);
    st->sidebar();
    glance();
    st->show();
    (void)popup;
}

} // namespace m3::shell::panels
