#pragma once

#include "core/Signal.h"

#include <ctime>
#include <set>
#include <string>
#include <vector>

namespace m3 {

// Arch packages: pending updates (official repos via checkupdates, AUR via
// yay/paru, Flatpak), search across the three sources, and installs.
// Upgrades and installs open the user's terminal so sudo and pacman prompts
// stay interactive.
class Packages {
public:
    enum class Source { Repo, Aur, Flatpak };

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

    // Search and install
    void search(Source source, const std::string& query);
    bool searching() const { return m_searching; }
    const std::vector<Result>& results() const { return m_results; }
    void install(const Result& r);

    Signal<> changed;

private:
    Packages();
    void refreshInstalled();
    std::string terminal() const;

    bool m_checking = false, m_searching = false;
    std::vector<Update> m_updates;
    std::vector<Result> m_results;
    std::set<std::string> m_native, m_flatpaks;
    std::time_t m_lastChecked = 0;
    std::string m_error, m_helper;
    int m_token = 0;
};

} // namespace m3
