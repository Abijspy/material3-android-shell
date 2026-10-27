#pragma once

#include "core/Signal.h"
#include "services/PacmanConf.h"

#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace m3 {

// Where software comes from: pacman's repositories (/etc/pacman.conf: Arch's
// own, testing, multilib and your own), held-back packages and parallel
// downloads, the mirrors (/etc/pacman.d/mirrorlist, and finding the fastest
// near you from archlinux.org's mirror status), and Flatpak's remotes.
// pacman's files are root's: each change is made on a copy, checked with
// pacman-conf, then put in place with one password (Packages' task, so its
// progress and result show in Settings), the file before it kept as
// <file>.m3-backup.
class Repos {
public:
    struct Mirror {
        std::string url;  // its base: "https://mirror.example.org/archlinux/"
        std::string country, code;
        double score = 0; // archlinux.org's: lower is better
        double speed = 0; // bytes a second, measured here
    };
    struct Country {
        std::string code, name;
        int mirrors = 0;
    };
    struct Remote {
        std::string name, title, url;
        bool enabled = true, user = false; // user: --user, else the system's
    };

    static Repos& get();
    void load(); // reads the files again (and Flatpak's remotes)

    const pacmanconf::Conf& conf() const { return m_conf; }
    const std::vector<std::string>& mirrors() const { return m_mirrors; } // the Server lines in use
    const std::string& error() const { return m_error; }                 // why the last change wasn't made

    // pacman.conf
    void setEnabled(const std::string& repo, bool on);
    // Your own repo; keyId (optional): its packager's key, fetched and trusted
    void add(const std::string& name, const std::string& server, const std::string& keyId);
    void remove(const std::string& repo);
    void setParallelDownloads(int n);
    void hold(const std::string& package, bool held);
    bool hasBackup() const; // pacman.conf.m3-backup
    void restoreBackup();   // swapped with the file: undo, and undo that

    // Mirrors
    void removeMirror(const std::string& server);
    // archlinux.org's mirror status, for the countries and candidates;
    // `then` once it's in
    void loadStatus(std::function<void()> then = {});
    bool statusLoaded() const { return !m_status.empty(); }
    bool loadingStatus() const { return m_loadingStatus; }
    const std::vector<Country>& countries() const { return m_countries; }
    // Where the mirrors in use are (most of them), "" for none known
    std::string currentCountry() const;
    // The best dozen there by score (all countries: ""), each timed
    // downloading core's database; the fastest ten kept
    void rank(const std::string& countryCode);
    bool ranking() const { return m_ranking; }
    const std::string& rankStatus() const { return m_rankStatus; }
    const std::vector<Mirror>& ranked() const { return m_ranked; }
    void useRanked(); // the mirrorlist, rewritten with them

    // Flatpak
    bool flatpak() const;
    const std::vector<Remote>& remotes() const { return m_remotes; }
    void addRemote(const std::string& name, const std::string& url, bool user = false);
    void addFlathub();
    void removeRemote(const Remote& remote);
    void setRemoteEnabled(const Remote& remote, bool enabled);

    Signal<> changed;

private:
    Repos();
    // `text` for `target` (root's), checked, then written with a password
    void write(const std::string& target, const std::string& text, const std::string& label, const std::string& doneText,
               const std::string& before = {});
    void flatpakTask(const std::string& label, const std::string& args, const std::string& doneText);

    std::string m_text, m_mirrorText; // pacman.conf and the mirrorlist as read
    pacmanconf::Conf m_conf;
    std::vector<std::string> m_mirrors;
    std::vector<Mirror> m_status, m_ranked; // archlinux.org's usable mirrors; the ranking's
    std::vector<Country> m_countries;
    std::vector<Remote> m_remotes;
    std::string m_error, m_rankStatus, m_rankCountry;
    bool m_ranking = false, m_loadingStatus = false, m_waiting = false;
};

} // namespace m3
