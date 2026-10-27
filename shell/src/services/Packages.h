#pragma once

#include "core/EventLoop.h"
#include "core/Signal.h"

#include <ctime>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace m3 {

// Arch packages: pending updates (official repos via checkupdates, AUR via
// yay/paru, Flatpak), search across the three sources, and installs.
// Upgrades and installs run in the background; root is asked for through
// polkit (pkexec), which the shell's own agent answers with its dialog. A
// terminal remains as a fallback for what needs answering by hand.
class Packages {
public:
    enum class Source { Repo, Aur, Flatpak, Shell }; // Shell: Material3UI itself

    struct Update {
        std::string name, from, to;
        Source source;
    };
    struct Result {
        std::string name, version, description, repo; // repo: "extra", "aur", remote name
        Source source;
        bool installed = false;
    };

    static Packages& get();

    // Updates; `done` once the shell's own is looked up too
    void check(std::function<void()> done = {});
    bool checking() const { return m_checking; }
    const std::vector<Update>& updates() const { return m_updates; }
    int count(Source s) const;
    std::time_t lastChecked() const { return m_lastChecked; }
    const std::string& error() const { return m_error; }
    const std::string& aurHelper() const { return m_helper; }
    void upgrade();
    void upgradeInTerminal(); // interactive: for conflicts the background run can't answer
    // A background task: what it is doing, or how the last one ended
    bool busy() const { return m_busy; }
    const std::string& status() const { return m_status; }
    // How the shell itself updates: "stable" (release tags) or "canary"
    // (the newest commits on main), as install_state.json records it
    std::string channel() const;
    // The git checkout install.sh made (empty if unknown)
    std::string installDir() const;
    void setChannel(const std::string& channel);
    // Runs `script` with root, asking through polkit; `done` gets success
    void runAsRoot(const std::string& label, const std::string& script, const std::string& doneText);

    // What an update left waiting for a restart: the computer (a new kernel,
    // systemd, glibc, drivers, firmware), the session (Hyprland's own
    // libraries) or just the shell (its program was replaced). Read from the
    // system, so it holds whatever ran the update.
    struct Restart {
        bool system = false, session = false, shell = false;
        std::vector<std::string> why; // package names
        bool any() const { return system || session || shell; }
    };
    const Restart& restart() const { return m_restart; }
    void checkRestart();

    // Automatic checks (Settings → Software & updates): every
    // /updates/checkEvery hours (0: never), firmware's too, and a
    // notification for what's new since the last one
    void startAutoCheck();
    // Which sources the checks and Update now take in: /updates/aur,
    // /updates/flatpak, /updates/firmware
    static bool includes(Source s);
    static bool includesFirmware();
    int pending() const; // updates waiting, firmware's included
    // Set by the shell: open a Settings page (a notification's Review)
    std::function<void(const std::string& page)> openSettings;
    // A background task (pacman, pkexec…): its label while it runs, then
    // doneText or why it didn't finish; one at a time
    void runTask(const std::string& label, const std::string& script, const std::string& doneText);
    // Search and install
    void search(Source source, const std::string& query);
    bool searching() const { return m_searching; }
    const std::vector<Result>& results() const { return m_results; }
    void install(const Result& r);
    // Removes a pacman package (with what it alone needed) or a Flatpak app
    void uninstall(Source source, const std::string& name);

    Signal<> changed;

private:
    Packages();
    void refreshInstalled();
    // A newer release (stable channel) or new commits (latest) of the shell
    void checkShell(std::function<void()> done);
    void autoCheck();
    std::set<std::string> currentKeys() const; // what's pending, as "source:name version"
    void loadCache();
    void saveCache() const;
    std::string m_shellDir; // the git checkout update.sh runs from
    std::string terminal() const;
    // yay or paru, told to ask for root through pkexec and not to stop for review
    std::string helperCommand(const std::string& args) const;

    Restart m_restart;
    bool m_checking = false, m_searching = false, m_busy = false;
    std::string m_status;
    std::vector<Update> m_updates;
    std::vector<Result> m_results;
    std::set<std::string> m_native, m_flatpaks;
    std::time_t m_lastChecked = 0;
    std::string m_error, m_helper;
    int m_token = 0;
    std::set<std::string> m_seen; // updates already told about
    bool m_upgrading = false;
    EventLoop::TimerId m_autoTimer = 0;
};

} // namespace m3
