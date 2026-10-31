#include "services/Repos.h"

#include "core/Paths.h"
#include "core/Process.h"
#include "services/Packages.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace m3 {

namespace {

const std::string kConf = "/etc/pacman.conf";
const std::string kMirrors = "/etc/pacman.d/mirrorlist";
const char* kFlathub = "https://dl.flathub.org/repo/flathub.flatpakrepo";

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

std::string lastLine(const std::string& out) {
    std::string last;
    std::stringstream ss(out);
    for (std::string l; std::getline(ss, l);)
        if (l.find_first_not_of(" \t") != std::string::npos) last = l;
    return last;
}

bool allOf(const std::string& s, const char* chars) {
    return !s.empty() && s.find_first_not_of(chars) == std::string::npos;
}

constexpr const char* kNameChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-";

} // namespace

Repos& Repos::get() {
    static Repos instance;
    return instance;
}

Repos::Repos() {
    load();
    // A change written (Packages' task, password and all): read the files again
    Packages::get().changed.connectForever([this] {
        if (!m_waiting || Packages::get().busy()) return;
        m_waiting = false;
        load();
    });
}

void Repos::load() {
    m_text = readFile(kConf);
    m_conf = pacmanconf::parse(m_text);
    m_mirrorText = readFile(kMirrors);
    m_mirrors = pacmanconf::servers(m_mirrorText);
    changed.emit();
    if (!flatpak()) {
        m_remotes.clear();
        return;
    }
    run({"flatpak", "remotes", "--show-disabled", "--columns=name,title,url,options"}, [this](int, const std::string& out) {
        std::vector<Remote> list;
        std::stringstream ss(out);
        for (std::string line; std::getline(ss, line);) {
            std::vector<std::string> f;
            std::stringstream ls(line);
            for (std::string field; std::getline(ls, field, '\t');) f.push_back(field);
            if (f.empty() || f[0].empty() || f[0] == "Name") continue;
            Remote r;
            r.name = f[0];
            r.title = f.size() > 1 && !f[1].empty() ? f[1] : f[0];
            r.url = f.size() > 2 ? f[2] : "";
            const std::string options = f.size() > 3 ? f[3] : "";
            r.user = options.find("user") != std::string::npos;
            r.enabled = options.find("disabled") == std::string::npos;
            list.push_back(std::move(r));
        }
        m_remotes = std::move(list);
        changed.emit();
    });
}

void Repos::write(const std::string& target, const std::string& text, const std::string& label, const std::string& doneText,
                  const std::string& before) {
    m_error.clear();
    if (Packages::get().busy()) {
        m_error = "Another task is under way: try again once it's done";
        changed.emit();
        return;
    }
    std::error_code ec;
    fs::create_directories(paths::runtimeDir(), ec);
    const std::string tmp = paths::runtimeDir() + "/" + fs::path(target).filename().string() + ".new";
    std::ofstream(tmp) << text;
    auto apply = [this, target, tmp, label, doneText, before] {
        // The file before it kept beside it; the new one root's, as pacman's are
        const std::string script = before + "cp -p " + target + " " + target + ".m3-backup && install -m 644 -o root -g root " + quote(tmp) +
                                   " " + target + " && rm -f " + quote(tmp);
        m_waiting = true;
        Packages::get().runAsRoot(label, script, doneText);
    };
    if (target != kConf) return apply();
    // pacman's own reading of it first: nothing that would stop pacman
    run({"sh", "-c", "pacman-conf --config " + quote(tmp) + " --repo-list 2>&1"}, [this, apply](int code, const std::string& out) {
        if (code != 0) {
            m_error = "pacman would refuse that: " + lastLine(out);
            changed.emit();
            return;
        }
        apply();
    });
}

// ── pacman.conf ──

void Repos::setEnabled(const std::string& repo, bool on) {
    if (!on && (repo == "core" || repo == "extra")) {
        m_error = "core and extra hold the system itself: they stay on";
        changed.emit();
        return;
    }
    const std::string text = pacmanconf::setEnabled(m_text, repo, on);
    if (text == m_text) return;
    write(kConf, text, (on ? "Turning on " : "Turning off ") + repo + "…",
          on ? "Turned on " + repo + ": Update now to get its packages" : "Turned off " + repo);
}

void Repos::add(const std::string& name, const std::string& server, const std::string& keyId) {
    m_error.clear();
    if (!allOf(name, kNameChars) || name == "options") m_error = "A repository's name is letters, digits, dots and dashes";
    else if (std::ranges::any_of(m_conf.repos, [&](const auto& r) { return r.name == name; })) m_error = "There's a " + name + " already";
    else if (!server.starts_with("https://") && !server.starts_with("http://") && !server.starts_with("file://"))
        m_error = "Its server is an address: https://…";
    else if (server.find_first_of(" \t'\"\n") != std::string::npos) m_error = "Its server can't have spaces or quotes";
    else if (!keyId.empty() && (!allOf(keyId, "0123456789abcdefABCDEF") || keyId.size() < 8 || keyId.size() > 40))
        m_error = "A key's id is 8 to 40 hex digits (its fingerprint)";
    if (!m_error.empty()) {
        changed.emit();
        return;
    }
    // Its packager's key, fetched and trusted, before pacman needs it
    const std::string before = keyId.empty() ? "" : "pacman-key --recv-keys " + keyId + " && pacman-key --lsign-key " + keyId + " && ";
    write(kConf, pacmanconf::addRepo(m_text, name, server), "Adding " + name + "…", "Added " + name + ": Update now to get its packages",
          before);
}

void Repos::remove(const std::string& repo) {
    if (pacmanconf::official(repo)) return;
    const std::string text = pacmanconf::removeRepo(m_text, repo);
    if (text != m_text) write(kConf, text, "Removing " + repo + "…", "Removed " + repo);
}

void Repos::setParallelDownloads(int n) {
    n = std::clamp(n, 1, 20);
    if (n == m_conf.parallelDownloads) return;
    write(kConf, pacmanconf::setOption(m_text, "ParallelDownloads", std::to_string(n)), "Saving…",
          std::format("Downloads {} at a time", n));
}

void Repos::hold(const std::string& package, bool held) {
    m_error.clear();
    if (!allOf(package, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@._+-")) {
        m_error = "A package's name is letters, digits and @._+-";
        changed.emit();
        return;
    }
    auto list = m_conf.ignored;
    const bool there = std::ranges::find(list, package) != list.end();
    if (held == there) return;
    if (held) list.push_back(package);
    else std::erase(list, package);
    write(kConf, pacmanconf::setIgnored(m_text, list), held ? "Holding back " + package + "…" : "Letting " + package + " update…",
          held ? package + " won't be updated until you let it" : package + " updates again");
}

bool Repos::hasBackup() const {
    return access((kConf + ".m3-backup").c_str(), R_OK) == 0;
}

void Repos::restoreBackup() {
    if (!hasBackup() || Packages::get().busy()) return;
    // Swapped: restoring again undoes it
    const std::string script = "t=$(mktemp) && cp -p " + kConf + " \"$t\" && cp -p " + kConf + ".m3-backup " + kConf + " && mv \"$t\" " +
                               kConf + ".m3-backup && chmod 644 " + kConf + ".m3-backup";
    m_waiting = true;
    Packages::get().runAsRoot("Restoring pacman.conf…", script, "pacman.conf is back as it was before the last change");
}

// ── Mirrors ──

void Repos::removeMirror(const std::string& server) {
    if (m_mirrors.size() <= 1) {
        m_error = "pacman needs one mirror at least";
        changed.emit();
        return;
    }
    const std::string text = pacmanconf::disableServer(m_mirrorText, server);
    if (text != m_mirrorText) write(kMirrors, text, "Removing a mirror…", "Removed " + pacmanconf::host(server));
}

void Repos::loadStatus(std::function<void()> then) {
    if (statusLoaded() || m_loadingStatus) {
        if (then && statusLoaded()) then();
        return;
    }
    utsname u{};
    if (uname(&u) == 0 && std::string(u.machine) != "x86_64") {
        m_rankStatus = "Finding mirrors is for Arch Linux on x86_64";
        changed.emit();
        return;
    }
    m_loadingStatus = true;
    m_rankStatus = "Looking up Arch's mirrors…";
    changed.emit();
    // Half a megabyte as it is, a tenth of that compressed
    run({"curl", "-fsS", "--compressed", "--max-time", "60", "https://archlinux.org/mirrors/status/json/"}, [this, then](int code, const std::string& out) {
        m_loadingStatus = false;
        std::vector<Mirror> list;
        try {
            if (code == 0)
                for (const auto& m : nlohmann::json::parse(out).value("urls", nlohmann::json::array())) {
                    // Up, over HTTPS, fully synced, and scored
                    if (!m.value("active", false) || m.value("protocol", "") != "https") continue;
                    const auto pct = m.find("completion_pct"), score = m.find("score"), sync = m.find("last_sync");
                    if (pct == m.end() || !pct->is_number() || pct->get<double>() < 0.99) continue;
                    if (score == m.end() || !score->is_number() || sync == m.end() || !sync->is_string()) continue;
                    list.push_back({m.value("url", ""), m.value("country", ""), m.value("country_code", ""), score->get<double>(), 0});
                }
        } catch (...) {
        }
        std::ranges::sort(list, {}, &Mirror::score);
        m_status = std::move(list);
        std::map<std::string, Country> byCode;
        for (const auto& m : m_status) {
            if (m.code.empty()) continue;
            auto& c = byCode[m.code];
            c.code = m.code;
            c.name = m.country;
            ++c.mirrors;
        }
        m_countries.clear();
        for (auto& [_, c] : byCode) m_countries.push_back(c);
        std::ranges::sort(m_countries, {}, &Country::name);
        m_rankStatus = m_status.empty() ? "Couldn't reach archlinux.org" : "";
        changed.emit();
        if (then && !m_status.empty()) then();
    });
}

std::string Repos::currentCountry() const {
    std::map<std::string, std::string> codeOf; // host → country
    for (const auto& m : m_status) codeOf[pacmanconf::host(m.url)] = m.code;
    std::map<std::string, int> counts;
    for (const auto& s : m_mirrors)
        if (const auto it = codeOf.find(pacmanconf::host(s)); it != codeOf.end() && !it->second.empty()) ++counts[it->second];
    std::string best;
    int most = 0;
    for (const auto& [code, n] : counts)
        if (n > most) {
            best = code;
            most = n;
        }
    return best;
}

void Repos::rank(const std::string& code) {
    if (m_ranking) return;
    m_rankCountry = code;
    if (!statusLoaded()) {
        loadStatus([this] { rank(m_rankCountry); });
        return;
    }
    std::string urls;
    int n = 0;
    for (const auto& m : m_status) {
        if (!code.empty() && m.code != code) continue;
        urls += m.url + '\n';
        if (++n == 12) break;
    }
    if (n == 0) {
        m_rankStatus = "No mirror there is up to date right now";
        changed.emit();
        return;
    }
    m_ranking = true;
    m_ranked.clear();
    m_rankStatus = std::format("Timing {} mirrors…", n);
    changed.emit();
    // Six at a time, each downloading core's database (a few hundred KB)
    const std::string script = "xargs -P 6 -n 1 sh -c 'curl -o /dev/null -s -w \"%{speed_download} %{http_code} $1\\n\" --max-time 6 "
                               "\"$1core/os/x86_64/core.db\" || echo \"0 0 $1\"' _";
    run({"sh", "-c", script},
        [this](int, const std::string& out) {
            std::map<std::string, double> speed;
            std::stringstream ss(out);
            for (std::string line; std::getline(ss, line);) {
                std::stringstream ls(line);
                double bytes = 0;
                int http = 0;
                std::string url;
                if (!(ls >> bytes >> http >> url) || http != 200) continue;
                speed[url] = std::max(speed[url], bytes);
            }
            std::vector<Mirror> ranked;
            for (const auto& m : m_status)
                if (const auto it = speed.find(m.url); it != speed.end() && it->second > 0) {
                    Mirror x = m;
                    x.speed = it->second;
                    ranked.push_back(x);
                }
            std::ranges::sort(ranked, std::greater{}, &Mirror::speed);
            if (ranked.size() > 10) ranked.resize(10);
            m_ranked = std::move(ranked);
            m_ranking = false;
            m_rankStatus = m_ranked.empty() ? "None of them answered in time" : "";
            changed.emit();
        },
        urls);
}

void Repos::useRanked() {
    if (m_ranked.empty()) return;
    std::vector<std::string> bases;
    for (const auto& m : m_ranked) bases.push_back(m.url);
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char date[32];
    std::strftime(date, sizeof date, "%Y-%m-%d", &tm);
    std::string where = "worldwide";
    for (const auto& c : m_countries)
        if (c.code == m_rankCountry) where = "in " + c.name;
    write(kMirrors, pacmanconf::mirrorlist(bases, std::string("The fastest ") + where + ", found by Material3UI on " + date), "Saving the mirrors…",
          "Mirrors saved: the fastest are used now");
}

// ── Flatpak ──

bool Repos::flatpak() const {
    return access("/usr/bin/flatpak", X_OK) == 0;
}

void Repos::flatpakTask(const std::string& label, const std::string& args, const std::string& doneText) {
    // Flatpak asks for the password itself (polkit) for the system's remotes
    m_waiting = true;
    Packages::get().runTask(label, "flatpak " + args, doneText);
}

void Repos::addRemote(const std::string& name, const std::string& url, bool user) {
    m_error.clear();
    if (!allOf(name, kNameChars)) m_error = "A remote's name is letters, digits, dots and dashes";
    else if (!url.starts_with("https://") || url.find_first_of(" \t'\"\n") != std::string::npos) m_error = "Its address starts with https://";
    if (!m_error.empty()) {
        changed.emit();
        return;
    }
    flatpakTask("Adding " + name + "…",
                "remote-add --if-not-exists " + std::string(user ? "--user " : "") + (url.ends_with(".flatpakrepo") ? "--from " : "") + quote(name) +
                    " " + quote(url),
                "Added " + name);
}

void Repos::installFlatpak() {
    Packages::get().runAsRoot(Packages::installingLabel("Flatpak"),
                              std::string("pacman -S --needed --noconfirm flatpak && flatpak remote-add --if-not-exists flathub ") + kFlathub,
                              "Installed Flatpak, with Flathub", [this](bool) { load(); });
}

void Repos::addFlathub() {
    addRemote("flathub", kFlathub);
}

void Repos::removeRemote(const Remote& r) {
    flatpakTask("Removing " + r.title + "…", "remote-delete " + std::string(r.user ? "--user " : "") + quote(r.name), "Removed " + r.title);
}

void Repos::setRemoteEnabled(const Remote& r, bool enabled) {
    flatpakTask((enabled ? "Turning on " : "Turning off ") + r.title + "…",
                "remote-modify " + std::string(r.user ? "--user " : "") + (enabled ? "--enable " : "--disable ") + quote(r.name),
                (enabled ? "Turned on " : "Turned off ") + r.title);
}

} // namespace m3
