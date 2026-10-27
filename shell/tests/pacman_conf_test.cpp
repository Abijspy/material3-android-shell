// pacman.conf as Arch ships it: repos read, switched, added and removed, the
// options set, and the mirrorlist
#include "services/PacmanConf.h"

#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

static const pacmanconf::Repo* find(const pacmanconf::Conf& c, const std::string& name) {
    for (const auto& r : c.repos)
        if (r.name == name) return &r;
    return nullptr;
}

static const char* kStock = R"([options]
#RootDir     = /
HoldPkg     = pacman glibc
Architecture = auto
#IgnorePkg   =
#IgnoreGroup =
Color
CheckSpace
ParallelDownloads = 5
DownloadUser = alpm
SigLevel    = Required DatabaseOptional
LocalFileSigLevel = Optional

#
# REPOSITORIES
#   - can be defined here or included from another file
#       [repo-name]
#       Server = ServerName
#

# The testing repositories are disabled by default. To enable, uncomment the
# repo name header and Include lines.

#[core-testing]
#Include = /etc/pacman.d/mirrorlist

[core]
Include = /etc/pacman.d/mirrorlist

#[extra-testing]
#Include = /etc/pacman.d/mirrorlist

[extra]
Include = /etc/pacman.d/mirrorlist

# If you want to run 32 bit applications on your x86_64 system,
# enable the multilib repositories as required here.

#[multilib-testing]
#Include = /etc/pacman.d/mirrorlist

#[multilib]
#Include = /etc/pacman.d/mirrorlist

# An example of a custom package repository.  See the pacman manpage for
# tips on creating your own repositories.
#[custom]
#SigLevel = Optional TrustAll
#Server = file:///home/custompkgs
)";

int main() {
    using namespace pacmanconf;
    const Conf c = parse(kStock);
    expect(c.repos.size() == 6, "six repos (the example isn't one)");
    expect(c.parallelDownloads == 5, "parallel downloads");
    expect(c.ignored.empty(), "nothing held back");
    if (const auto* core = find(c, "core")) {
        expect(core->enabled && core->includes.size() == 1 && core->includes[0] == "/etc/pacman.d/mirrorlist", "core on, with the mirrorlist");
    } else {
        expect(false, "core found");
    }
    if (const auto* m = find(c, "multilib")) expect(!m->enabled && m->includes.size() == 1, "multilib off, its line read");
    expect(find(c, "custom") == nullptr, "the stock example skipped");
    expect(find(c, "repo-name") == nullptr, "a header in a comment isn't a repo");

    // multilib on: its header and Include uncommented, nothing else
    const std::string on = setEnabled(kStock, "multilib", true);
    expect(has(on, "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n"), "multilib on");
    expect(has(on, "#[multilib-testing]\n#Include"), "its testing twin untouched");
    expect(has(on, "# An example of a custom"), "the comment after it untouched");
    const Conf c2 = parse(on);
    expect(find(c2, "multilib") && find(c2, "multilib")->enabled, "multilib reads as on");
    expect(setEnabled(on, "multilib", false) == std::string(kStock), "and off again: as it was");

    // extra off comments its lines out; on again restores them
    const std::string off = setEnabled(kStock, "extra", false);
    expect(has(off, "\n#[extra]\n#Include = /etc/pacman.d/mirrorlist\n"), "extra off");
    expect(setEnabled(off, "extra", true) == std::string(kStock), "extra on again");

    // A testing repo the file lacks goes above the repo it tests
    const std::string bare = "[options]\nColor\n\n[core]\nInclude = /etc/pacman.d/mirrorlist\n";
    const std::string testing = setEnabled(bare, "core-testing", true);
    expect(has(testing, "[core-testing]\nInclude = /etc/pacman.d/mirrorlist\n\n[core]"), "testing above its repo");
    const std::string ml = setEnabled(bare, "multilib", true);
    expect(has(ml, "Include = /etc/pacman.d/mirrorlist\n\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n"), "multilib at the end");

    // Your own repo: added, switched off, removed
    const std::string added = addRepo(kStock, "chaotic-aur", "https://cdn-mirror.chaotic.cx/$repo/$arch", "Required");
    const Conf c3 = parse(added);
    const auto* chaotic = find(c3, "chaotic-aur");
    expect(chaotic && chaotic->enabled && chaotic->servers.size() == 1 && chaotic->sigLevel == "Required", "added");
    const std::string chaoticOff = setEnabled(added, "chaotic-aur", false);
    expect(has(chaoticOff, "#[chaotic-aur]\n#SigLevel = Required\n#Server = "), "switched off");
    const auto* off3 = find(parse(chaoticOff), "chaotic-aur");
    expect(off3 && !off3->enabled && off3->servers.size() == 1, "reads as off, its server kept");
    expect(removeRepo(added, "chaotic-aur") == std::string(kStock), "removed: as it was");
    const std::string middle = addRepo(bare, "mine", "file:///srv/repo");
    expect(removeRepo(setEnabled(middle, "multilib", true), "mine") == setEnabled(bare, "multilib", true), "removed from the middle");

    // Options
    const std::string par = setOption(kStock, "ParallelDownloads", "10");
    expect(parse(par).parallelDownloads == 10 && has(par, "\nParallelDownloads = 10\n"), "parallel downloads set");
    expect(parse(setOption(kStock, "ParallelDownloads", "")).parallelDownloads == 1, "commented out: one at a time");
    const std::string held = setIgnored(kStock, {"linux", "mesa"});
    expect(parse(held).ignored == std::vector<std::string>{"linux", "mesa"} && has(held, "\nIgnorePkg = linux mesa\n"), "held back");
    expect(parse(setIgnored(held, {})).ignored.empty(), "let go");
    expect(parse(setIgnored(held + "", {"nvidia"})).ignored == std::vector<std::string>{"nvidia"}, "replaced");
    const std::string twice = setOption(held, "ParallelDownloads", "3");
    expect(parse(setIgnored("[options]\nIgnorePkg = a\nIgnorePkg = b\n", {"c"})).ignored == std::vector<std::string>{"c"}, "one IgnorePkg line");
    expect(parse(twice).ignored.size() == 2, "other options left alone");
    const std::string fresh = setOption("[options]\nColor\n\n[core]\n", "ParallelDownloads", "4");
    expect(has(fresh, "[options]\nColor\nParallelDownloads = 4\n"), "a new option after the others");

    // The mirrorlist
    const std::string mirrors = "## India\nServer = https://a.example/arch/$repo/os/$arch\n#Server = https://b.example/$repo/os/$arch\n"
                                "Server = https://c.example/$repo/os/$arch\n";
    expect(servers(mirrors).size() == 2, "servers in use");
    expect(servers(disableServer(mirrors, "https://a.example/arch/$repo/os/$arch")).size() == 1, "one switched off");
    const std::string ranked = mirrorlist({"https://x.example/archlinux/", "https://y.example/arch"}, "Fastest");
    expect(servers(ranked) == std::vector<std::string>{"https://x.example/archlinux/$repo/os/$arch", "https://y.example/arch/$repo/os/$arch"},
           "a new mirrorlist");
    expect(host("https://mirror.example.org/archlinux/$repo/os/$arch") == "mirror.example.org", "host");

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
