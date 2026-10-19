#include "services/Packages.h"

#include "core/Paths.h"

#include "build_config.h"

#include "core/EventLoop.h"
#include "core/Process.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <nlohmann/json.hpp>
#include <regex>
#include <fstream>
#include <sstream>

namespace m3 {

namespace {

// One shell script for everything, so a check is a single process; sections
// are separated by markers
const char* kCheck = R"(
if command -v checkupdates >/dev/null 2>&1; then
    checkupdates --nocolor 2>/dev/null; [ $? -eq 1 ] && echo "#ERROR"
else
    echo "#MISSING"
fi
echo "#AUR"
for h in yay paru; do
    if command -v "$h" >/dev/null 2>&1 && "$h" --version >/dev/null 2>&1; then
        echo "#HELPER $h"; "$h" -Qua --color never 2>/dev/null; break
    fi
done
echo "#FLATPAK"
command -v flatpak >/dev/null 2>&1 && flatpak remote-ls --updates --app --columns=application,version 2>/dev/null
exit 0
)";

std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

int rank(const std::string& name, const std::string& q) {
    std::string n = name;
    std::ranges::transform(n, n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n == q) return 0;
    if (n.starts_with(q)) return 1;
    if (n.find(q) != std::string::npos) return 2;
    return 3;
}

} // namespace

Packages& Packages::get() {
    static Packages instance;
    return instance;
}

Packages::Packages() {
    refreshInstalled();
}

std::string Packages::terminal() const {
    const char* t = std::getenv("TERMINAL");
    return t && *t ? t : "alacritty";
}

int Packages::count(Source s) const {
    return static_cast<int>(std::ranges::count(m_updates, s, &Update::source));
}

void Packages::check() {
    if (m_checking) return;
    m_checking = true;
    changed.emit();
    run({"sh", "-c", kCheck}, [this](int, const std::string& out) {
        std::vector<Update> list;
        Source section = Source::Repo;
        bool missing = false, failed = false;
        std::stringstream ss(out);
        std::string line;
        static const std::regex upd(R"(^(\S+)\s+(\S+)\s+->\s+(\S+))");
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            if (line == "#MISSING") missing = true;
            else if (line == "#ERROR") failed = true;
            else if (line == "#AUR") section = Source::Aur;
            else if (line == "#FLATPAK") section = Source::Flatpak;
            else if (line.starts_with("#HELPER ")) m_helper = line.substr(8);
            else if (section == Source::Flatpak) {
                const auto tab = line.find('\t');
                list.push_back({line.substr(0, tab), "", tab == std::string::npos ? "latest" : line.substr(tab + 1), section});
            } else if (std::smatch m; std::regex_search(line, m, upd) && line.find("[ignored]") == std::string::npos) {
                list.push_back({m[1], m[2], m[3], section});
            }
        }
        m_updates = std::move(list);
        m_error = missing ? "Install pacman-contrib to check for updates"
                : failed && m_updates.empty() ? "Could not reach the package mirrors" : "";
        m_lastChecked = std::time(nullptr);
        m_checking = false;
        changed.emit();
        checkShell(); // appends to the list just set
    });
}

std::string Packages::helperCommand(const std::string& args) const {
    if (m_helper == "paru") return "paru " + args + " --noconfirm --skipreview --sudo pkexec";
    return m_helper + " " + args + " --noconfirm --sudo pkexec --answerclean None --answerdiff None --answeredit None";
}

void Packages::runTask(const std::string& label, const std::string& script, const std::string& doneText) {
    if (m_busy) return;
    m_busy = true;
    m_status = label;
    changed.emit();
    run({"sh", "-c", "{ " + script + "; } 2>&1"}, [this, doneText](int code, const std::string& out) {
        m_busy = false;
        // pkexec: 126 when the dialog was dismissed, 127 when not authorised
        std::string last;
        std::stringstream ss(out);
        for (std::string l; std::getline(ss, l);)
            if (!l.empty()) last = l;
        m_status = code == 0 ? doneText
                 : code == 126 || code == 127 ? std::string("Cancelled: no password given")
                 : "Didn't finish: " + (last.empty() ? std::to_string(code) : last);
        run({"notify-send", "-a", "Software & updates", m_status}, nullptr);
        changed.emit();
        refreshInstalled();
        check();
    });
}

void Packages::runAsRoot(const std::string& label, const std::string& script, const std::string& doneText) {
    // One prompt for the whole script
    std::string quoted = "'";
    for (char c : script) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    runTask(label, "pkexec /usr/bin/sh -c " + quoted + "'", doneText);
}

namespace {

// "4.10.0" > "4.9.2"
bool newerVersion(const std::string& a, const std::string& b) {
    std::stringstream sa(a), sb(b);
    for (int i = 0; i < 4; ++i) {
        int x = 0, y = 0;
        char dot = 0;
        sa >> x;
        sb >> y;
        if (x != y) return x > y;
        sa >> dot;
        sb >> dot;
    }
    return false;
}

} // namespace

namespace {

std::string stateFile() {
    return paths::configDir() + "/install_state.json";
}

nlohmann::json readState() {
    try {
        std::ifstream in(stateFile());
        if (in) return nlohmann::json::parse(in);
    } catch (...) {
    }
    return nlohmann::json::object();
}

} // namespace

std::string Packages::channel() const {
    return readState().value("channel", "stable");
}

void Packages::setChannel(const std::string& channel) {
    auto state = readState();
    if (state.value("channel", "stable") == channel) return;
    state["channel"] = channel;
    // update.sh reads it from here too
    std::ofstream(stateFile()) << state.dump(2) << "\n";
    changed.emit();
    checkShell(); // what's pending depends on the channel
}

void Packages::checkShell() {
    // Where install.sh put the checkout, and which channel it follows
    const auto state = readState();
    m_shellDir = state.value("install_dir", "");
    const std::string channel = state.value("channel", "stable");
    if (m_shellDir.empty()) return;
    const std::string script = "d='" + m_shellDir + "'; git -C \"$d\" fetch -q --tags origin 2>/dev/null || exit 1; " +
                               (channel == "stable"
                                    ? std::string("t=$(git -C \"$d\" describe --tags \"$(git -C \"$d\" rev-list --tags --max-count=1)\" "
                                                  "2>/dev/null); echo \"tag ${t#v}\"")
                                    : std::string("echo \"commits $(git -C \"$d\" rev-list --count HEAD..origin/main)\""));
    // A channel switch takes back what the other channel found
    std::erase_if(m_updates, [](const Update& u) { return u.source == Source::Shell; });
    run({"sh", "-c", script}, [this](int code, const std::string& out) {
        if (code != 0) return;
        std::erase_if(m_updates, [](const Update& u) { return u.source == Source::Shell; });
        std::stringstream ss(out);
        std::string kind, value;
        ss >> kind >> value;
        if (kind == "tag" && !value.empty() && newerVersion(value, M3_VERSION))
            m_updates.insert(m_updates.begin(), {"Material3UI Shell", M3_VERSION, value, Source::Shell});
        else if (kind == "commits" && std::atoi(value.c_str()) > 0)
            m_updates.insert(m_updates.begin(), {"Material3UI Shell", M3_VERSION, value + " new commits", Source::Shell});
        changed.emit();
    });
}

void Packages::upgrade() {
    // Official packages first, then the AUR (built as the user; its helper
    // asks through pkexec to install), then Flatpaks (polkit itself)
    std::string script = "pkexec pacman -Syu --noconfirm";
    if (!m_helper.empty()) script += " && " + helperCommand("-Sua");
    script += " && { command -v flatpak >/dev/null && flatpak update -y --noninteractive || true; }";
    // Last, the shell itself: update.sh rebuilds it, installs through
    // pkexec and restarts it (this process ends there; it notifies)
    if (count(Source::Shell) > 0 && !m_shellDir.empty())
        script += " && M3_SUDO=pkexec '" + m_shellDir + "/update.sh' all";
    runTask("Updating… this can take a while", script, "Updated");
}

void Packages::upgradeInTerminal() {
    const std::string cmd = (m_helper.empty() ? "sudo pacman -Syu" : m_helper + " -Syu") +
                            std::string("; command -v flatpak >/dev/null && flatpak update; echo; read -p 'Done. Press Enter to close…' _");
    run({terminal(), "-e", "sh", "-c", cmd}, [this](int, const std::string&) {
        refreshInstalled();
        check();
    });
}

void Packages::refreshInstalled() {
    run({"sh", "-c", "pacman -Qq; echo '#FLATPAK'; command -v flatpak >/dev/null && flatpak list --app --columns=application; exit 0"},
        [this](int, const std::string& out) {
            m_native.clear();
            m_flatpaks.clear();
            bool flat = false;
            std::stringstream ss(out);
            std::string line;
            while (std::getline(ss, line)) {
                if (line == "#FLATPAK") flat = true;
                else if (!line.empty()) (flat ? m_flatpaks : m_native).insert(line);
            }
            for (auto& r : m_results) r.installed = (r.source == Source::Flatpak ? m_flatpaks : m_native).contains(r.name);
            changed.emit();
        });
}

void Packages::search(Source source, const std::string& query) {
    const int token = ++m_token;
    std::string q = query;
    q.erase(0, q.find_first_not_of(' '));
    if (q.size() < 2) {
        m_results.clear();
        m_searching = false;
        changed.emit();
        return;
    }
    std::string lowerQ = q;
    std::ranges::transform(lowerQ, lowerQ.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    m_searching = true;
    changed.emit();
    auto finish = [this, token, lowerQ](std::vector<Result> r) {
        if (token != m_token) return; // a newer search won
        std::ranges::stable_sort(r, [&](const Result& a, const Result& b) { return rank(a.name, lowerQ) < rank(b.name, lowerQ); });
        if (r.size() > 50) r.resize(50);
        for (auto& x : r) x.installed = (x.source == Source::Flatpak ? m_flatpaks : m_native).contains(x.name);
        m_results = std::move(r);
        m_searching = false;
        changed.emit();
    };

    if (source == Source::Repo) {
        run({"pacman", "-Ss", "--color", "never", "--", q}, [finish](int, const std::string& out) {
            std::vector<Result> r;
            std::stringstream ss(out);
            std::string line, desc;
            static const std::regex head(R"(^(\S+)/(\S+)\s+(\S+))");
            while (std::getline(ss, line)) {
                std::smatch m;
                if (!std::regex_search(line, m, head)) continue;
                std::getline(ss, desc);
                desc.erase(0, desc.find_first_not_of(' '));
                r.push_back({m[2], m[3], desc, m[1], Source::Repo});
            }
            finish(std::move(r));
        });
    } else if (source == Source::Aur) {
        // aurweb's RPC needs no helper to search
        run({"curl", "-fsS", "--max-time", "10", "https://aur.archlinux.org/rpc/v5/search/" + urlEncode(q) + "?by=name-desc"},
            [this, finish, token](int code, const std::string& out) {
                std::vector<Result> r;
                try {
                    if (code == 0)
                        for (const auto& p : nlohmann::json::parse(out).value("results", nlohmann::json::array()))
                            r.push_back({p.value("Name", ""), p.value("Version", ""),
                                         p["Description"].is_string() ? p["Description"].get<std::string>() : "", "aur", Source::Aur});
                } catch (...) {
                }
                if (code != 0 && token == m_token) m_error = "The AUR is unreachable";
                finish(std::move(r));
            });
    } else {
        run({"flatpak", "search", "--columns=application,name,version,description,remotes", q},
            [finish](int, const std::string& out) {
                std::vector<Result> r;
                std::stringstream ss(out);
                std::string line;
                while (std::getline(ss, line)) {
                    std::vector<std::string> f;
                    std::stringstream ls(line);
                    std::string field;
                    while (std::getline(ls, field, '\t')) f.push_back(field);
                    if (f.size() < 2 || f[0] == "Application ID") continue;
                    const std::string remote = f.size() > 4 ? f[4].substr(0, f[4].find(',')) : "flathub";
                    r.push_back({f[0], f.size() > 2 ? f[2] : "", f.size() > 3 ? f[3] : f[1], remote, Source::Flatpak});
                }
                finish(std::move(r));
            });
    }
}

void Packages::uninstall(Source source, const std::string& name) {
    if (source == Source::Flatpak)
        runTask("Uninstalling " + name + "…", "flatpak uninstall -y --noninteractive " + name, "Uninstalled " + name);
    else
        runTask("Uninstalling " + name + "…", "pkexec pacman -Rns --noconfirm " + name, "Uninstalled " + name);
}

void Packages::install(const Result& r) {
    std::string cmd;
    if (r.source == Source::Repo) cmd = "pkexec pacman -S --needed --noconfirm " + r.name;
    else if (r.source == Source::Aur) cmd = m_helper.empty() ? "false" : helperCommand("-S --needed " + r.name);
    else cmd = "flatpak install -y --noninteractive " + r.repo + " " + r.name;
    runTask("Installing " + r.name + "…", cmd, "Installed " + r.name);
}

} // namespace m3
