#pragma once

#include <string>
#include <vector>

// pacman.conf and the mirrorlist as text: read, and changed a line at a time
// so the rest of the file (its comments, its order) stays as it was. Kept
// apart from the services for the unit tests.
namespace m3::pacmanconf {

struct Repo {
    std::string name;
    bool enabled = false;
    std::vector<std::string> servers;  // Server = lines
    std::vector<std::string> includes; // Include = lines (the mirrorlist)
    std::string sigLevel;
};

struct Conf {
    std::vector<Repo> repos;      // in the file's order, disabled ones too
    int parallelDownloads = 1;    // pacman's own default without the line
    std::vector<std::string> ignored; // IgnorePkg: held back from upgrades
};

Conf parse(const std::string& text);

// Arch's own: core, extra, multilib and their testing twins
bool official(const std::string& repo);

// A repo's header and lines commented out, or back in. A testing repo that
// isn't in the file at all is added above the repo it tests, as Arch's own
// pacman.conf has it; multilib is added at the end
std::string setEnabled(const std::string& text, const std::string& repo, bool on);
// A new repo at the end: its servers and signature level
std::string addRepo(const std::string& text, const std::string& name, const std::string& server, const std::string& sigLevel = {});
// A repo, its lines, and the blank line after it
std::string removeRepo(const std::string& text, const std::string& name);
// An [options] line set (Key = value), or commented out (empty value)
std::string setOption(const std::string& text, const std::string& key, const std::string& value);
std::string setIgnored(const std::string& text, const std::vector<std::string>& packages);

// The mirrorlist's servers in use, in order
std::vector<std::string> servers(const std::string& mirrorlist);
// One commented out
std::string disableServer(const std::string& mirrorlist, const std::string& url);
// A new mirrorlist: these mirrors' base URLs (".../archlinux/"), best first
std::string mirrorlist(const std::vector<std::string>& bases, const std::string& note);
// "https://mirror.example.org/archlinux/$repo/os/$arch" → "mirror.example.org"
std::string host(const std::string& url);

} // namespace m3::pacmanconf
