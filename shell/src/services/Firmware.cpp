#include "services/Firmware.h"

#include "core/Process.h"
#include "services/Packages.h"

#include <algorithm>
#include <sstream>
#include <unistd.h>

namespace m3 {

namespace {

const char* kTool = "/usr/bin/fwupdmgr";

// No questions: fwupdmgr can't be answered here (an unreported update, old
// metadata, a restart): Settings asks those itself
const char* kQuiet = " --no-unreported-check --no-metadata-check";

// The last line written, progress bars' carriage returns too
std::string lastLine(const std::string& out) {
    std::string last;
    std::string line;
    for (char c : out) {
        if (c == '\n' || c == '\r') {
            if (line.find_first_not_of(" \t") != std::string::npos) last = line;
            line.clear();
        } else {
            line += c;
        }
    }
    if (line.find_first_not_of(" \t") != std::string::npos) last = line;
    return last;
}

} // namespace

Firmware& Firmware::get() {
    static Firmware instance;
    return instance;
}

Firmware::Firmware() {
    // fwupd installed from Settings (a Packages task): look again
    Packages::get().changed.connectForever([this] {
        if (!m_lastChecked && !m_checking && !Packages::get().busy() && installed()) check(true);
    });
}

bool Firmware::installed() const {
    return access(kTool, X_OK) == 0;
}

int Firmware::updateCount() const {
    return static_cast<int>(std::ranges::count_if(m_devices, [](const Device& d) {
        return std::ranges::any_of(d.releases, &Release::upgrade);
    }));
}

bool Firmware::needsReboot() const {
    return std::ranges::any_of(m_devices, &Device::needsReboot);
}

bool Firmware::refreshesQuietly() {
    if (m_quiet >= 0) return m_quiet == 1;
    m_quiet = 0;
    // fwupd 2's action, or the one before it
    run({"sh", "-c",
         "pkaction --action-id org.freedesktop.fwupd.refresh-remote --verbose 2>/dev/null || "
         "pkaction --action-id org.freedesktop.fwupd.update-metadata --verbose 2>/dev/null"},
        [this](int code, const std::string& out) {
            // "implicit active:   yes"
            std::stringstream ss(out);
            for (std::string l; std::getline(ss, l);)
                if (l.find("implicit active:") != std::string::npos) m_quiet = code == 0 && l.ends_with("yes") ? 1 : 0;
        });
    return false; // known next time
}

void Firmware::check(bool refresh, std::function<void()> done) {
    if (!installed() || m_checking || m_updating) {
        if (done) done();
        return;
    }
    m_checking = true;
    changed.emit();
    std::string script;
    // 0 downloaded, 2 already up to date
    if (refresh) script += std::string(kTool) + " refresh" + kQuiet + " >/dev/null 2>&1; echo \"#REFRESH $?\"; ";
    script += "echo '#DEVICES'; " + std::string(kTool) + " get-devices --json" + kQuiet + " 2>/dev/null; ";
    script += "echo '#UPDATES'; " + std::string(kTool) + " get-updates --json" + kQuiet + " 2>/dev/null; ";
    script += "echo '#REMOTES'; " + std::string(kTool) + " get-remotes --json 2>/dev/null; exit 0";
    run({"sh", "-c", script}, [this, done](int, const std::string& out) {
        std::string devices, updates, remotes, *into = nullptr;
        int refreshed = 0;
        std::stringstream ss(out);
        for (std::string l; std::getline(ss, l);) {
            if (l.starts_with("#REFRESH ")) refreshed = std::atoi(l.c_str() + 9);
            else if (l == "#DEVICES") into = &devices;
            else if (l == "#UPDATES") into = &updates;
            else if (l == "#REMOTES") into = &remotes;
            else if (into) *into += l + '\n';
        }
        auto list = parseDevices(devices);
        // get-devices' releases are what each runs now; the updates' devices
        // carry what's newer (a release there that isn't flagged either way,
        // and isn't the same version, counts as one)
        for (auto& d : list) d.releases.clear();
        for (auto& u : parseDevices(updates)) {
            for (auto& r : u.releases)
                if (!r.upgrade && std::ranges::find(r.flags, "is-downgrade") == r.flags.end() && r.version != u.version) r.upgrade = true;
            auto it = std::ranges::find(list, u.id, &Device::id);
            if (it != list.end()) it->releases = std::move(u.releases);
            else list.push_back(std::move(u));
        }
        // Ones with updates first, then the updatable, then by name
        std::ranges::stable_sort(list, [](const Device& a, const Device& b) {
            const int ra = a.releases.empty() ? a.updatable() ? 1 : 2 : 0, rb = b.releases.empty() ? b.updatable() ? 1 : 2 : 0;
            return ra != rb ? ra < rb : a.name < b.name;
        });
        m_devices = std::move(list);
        m_remotes = parseRemotes(remotes);
        m_error = m_devices.empty() && devices.find('{') == std::string::npos ? "fwupd didn't answer"
                : refreshed != 0 && refreshed != 2                         ? "Couldn't download the latest firmware list"
                                                                            : "";
        m_lastChecked = std::time(nullptr);
        m_checking = false;
        changed.emit();
        if (done) done();
    });
}

void Firmware::update(const std::string& deviceId) {
    if (m_updating || m_checking || !installed()) return;
    std::string name = "the firmware";
    if (const auto it = std::ranges::find(m_devices, deviceId, &Device::id); it != m_devices.end()) name = it->name;
    m_updating = true;
    m_status = "Updating " + name + "… keep the computer on";
    changed.emit();
    std::vector<std::string> argv = {kTool, "update"};
    if (!deviceId.empty()) argv.push_back(deviceId);
    for (const char* a : {"--assume-yes", "--no-reboot-check", "--no-unreported-check", "--no-metadata-check"}) argv.emplace_back(a);
    std::string cmd;
    for (const auto& a : argv) cmd += (cmd.empty() ? "" : " ") + ("'" + a + "'");
    run({"sh", "-c", cmd + " 2>&1"}, [this, name](int code, const std::string& out) {
        m_updating = false;
        const std::string last = lastLine(out);
        m_status = code == 0 ? "Updated " + name
                 : last.find("auth") != std::string::npos || last.find("Auth") != std::string::npos ? std::string("Cancelled: no password given")
                 : "Didn't finish: " + (last.empty() ? std::to_string(code) : last);
        changed.emit();
        check(false);
    });
}

void Firmware::setRemote(const std::string& id, bool enabled) {
    if (m_checking || m_updating) return;
    // Its id as fwupd named it: letters, digits and dashes
    if (id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) return;
    const std::string cmd = std::string(kTool) + (enabled ? " enable-remote " : " disable-remote ") + id + " --assume-yes 2>&1";
    run({"sh", "-c", cmd}, [this, id, enabled](int code, const std::string& out) {
        m_status = code == 0 ? std::string(enabled ? "Turned on " : "Turned off ") + id : "Didn't change " + id + ": " + lastLine(out);
        // A source turned on: its list, downloaded
        check(code == 0 && enabled);
    });
}

} // namespace m3
