#pragma once

#include "core/Signal.h"

#include <ctime>
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

    // Updates
    void check();
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
    void setChannel(const std::string& channel);
    // Runs `script` with root, asking through polkit; `done` gets success
    void runAsRoot(const std::string& label, const std::string& script, const std::string& doneText);

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
    void checkShell();
    std::string m_shellDir; // the git checkout update.sh runs from
    std::string terminal() const;
    void runTask(const std::string& label, const std::string& script, const std::string& doneText);
    // yay or paru, told to ask for root through pkexec and not to stop for review
    std::string helperCommand(const std::string& args) const;

    bool m_checking = false, m_searching = false, m_busy = false;
    std::string m_status;
    std::vector<Update> m_updates;
    std::vector<Result> m_results;
    std::set<std::string> m_native, m_flatpaks;
    std::time_t m_lastChecked = 0;
    std::string m_error, m_helper;
    int m_token = 0;
};

} // namespace m3
