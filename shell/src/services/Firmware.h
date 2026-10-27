#pragma once

#include "core/Signal.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace m3 {

// Device firmware through fwupd (fwupdmgr): the devices and their versions,
// the updates the LVFS (Linux Vendor Firmware Service) has for them, and
// installing one. fwupd asks for the password itself, through polkit, which
// the shell's own agent answers. System firmware (UEFI) installs on the next
// restart. Without fwupd there's nothing; Settings offers to install it.
class Firmware {
public:
    struct Release {
        std::string version, summary, description; // description: plain text, from AppStream markup
        std::string urgency;                         // low, medium, high, critical (or empty)
        std::string remote;                          // lvfs, lvfs-testing…
        uint64_t size = 0;
        std::vector<std::string> flags;
        bool upgrade = false; // a newer version (fwupd's is-upgrade)
    };
    struct Device {
        std::string id, name, vendor, version, summary, plugin;
        std::vector<std::string> flags, icons;
        // Why it can't update right now: require-ac-power, system-power-too-low,
        // lid-is-closed…
        std::vector<std::string> problems;
        std::string updateState, updateError; // the last install: success, failed, needs-reboot…
        std::vector<Release> releases;        // updates for it, the newest first
        bool has(const std::string& flag) const;
        // updatable-hidden: updatable, shown only with an update
        bool updatable() const { return has("updatable") || has("updatable-hidden"); }
        // Installed, waiting for the restart that puts it in
        bool needsReboot() const { return updateState == "needs-reboot"; }
        // Its updates go in as the computer restarts (the flag says so; it
        // isn't a restart pending)
        bool appliesOnRestart() const { return has("needs-reboot") || has("needs-shutdown"); }
        // A Material Symbol for it, from fwupd's icon names
        std::string symbol() const;
    };
    struct Remote {
        std::string id, title, kind; // kind: download, local, directory
        bool enabled = false;
        std::time_t modified = 0; // its metadata, last downloaded
    };

    static Firmware& get();
    bool installed() const; // fwupdmgr is there

    // Downloads the latest metadata first (when `refresh`), then reads the
    // devices, their updates and the sources; `done` after (at once if it
    // can't)
    void check(bool refresh = true, std::function<void()> done = {});
    bool checking() const { return m_checking; }
    std::time_t lastChecked() const { return m_lastChecked; }
    const std::string& error() const { return m_error; }
    const std::vector<Device>& devices() const { return m_devices; }
    int updateCount() const;
    bool needsReboot() const;

    // Installs a device's newest release ("": every device's)
    void update(const std::string& deviceId);
    bool updating() const { return m_updating; }
    const std::string& status() const { return m_status; } // what it's doing, or how the last one ended

    const std::vector<Remote>& remotes() const { return m_remotes; }
    void setRemote(const std::string& id, bool enabled);

    // Automatic checks may download the metadata only if polkit lets the
    // session do that without a password (fwupd's default)
    bool refreshesQuietly();

    // fwupdmgr's JSON (get-devices, get-updates: {"Devices": […]}; get-remotes)
    static std::vector<Device> parseDevices(const std::string& json);
    static std::vector<Remote> parseRemotes(const std::string& json);
    // AppStream markup ("<p>…</p><ul><li>…</li></ul>") as plain lines
    static std::string plainText(const std::string& markup);
    // A problem's id as words ("Plug in the charger")
    static std::string problemText(const std::string& problem);

    Signal<> changed;

private:
    Firmware();
    bool m_checking = false, m_updating = false;
    int m_quiet = -1; // refreshesQuietly, once known
    std::time_t m_lastChecked = 0;
    std::string m_error, m_status;
    std::vector<Device> m_devices;
    std::vector<Remote> m_remotes;
};

} // namespace m3
