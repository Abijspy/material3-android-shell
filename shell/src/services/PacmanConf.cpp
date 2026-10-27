#include "services/PacmanConf.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace m3::pacmanconf {

namespace {

constexpr size_t npos = std::string::npos;

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    for (std::string l; std::getline(ss, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
    }
    return out;
}

std::string join(const std::vector<std::string>& ls) {
    std::string out;
    for (const auto& l : ls) out += l + '\n';
    return out;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

bool blank(const std::string& l) {
    return trim(l).empty();
}

// "[core]", or commented out: "#[core]", "# [core]"
bool header(const std::string& line, std::string& name, bool& enabled) {
    std::string t = trim(line);
    enabled = !t.starts_with('#');
    if (!enabled) {
        const size_t a = t.find_first_not_of('#');
        if (a == npos) return false; // "#" alone
        // "#[multilib]" or "# [multilib]"; "#       [repo-name]" is the
        // stock file's explanation
        const size_t b = t.find_first_not_of(" \t", a);
        if (b == npos || b - a > 1) return false;
        t = trim(t.substr(a));
    }
    if (t.size() < 3 || t.front() != '[' || t.back() != ']') return false;
    name = t.substr(1, t.size() - 2);
    // Commented: a header alone, not words in brackets
    return !name.empty() && (enabled || name.find_first_of(" \t") == npos);
}

bool isHeader(const std::string& line) {
    std::string n;
    bool e;
    return header(line, n, e);
}

// "Server = url", "#Include = path", "Color"
bool directive(const std::string& line, std::string& key, std::string& value, bool& enabled) {
    std::string t = trim(line);
    if (t.empty()) return false;
    enabled = t[0] != '#';
    if (!enabled) {
        const size_t a = t.find_first_not_of("# \t");
        if (a == npos) return false;
        t = t.substr(a);
    }
    size_t k = 0;
    while (k < t.size() && std::isalpha(static_cast<unsigned char>(t[k]))) ++k;
    if (k == 0) return false;
    key = t.substr(0, k);
    const std::string rest = trim(t.substr(k));
    if (rest.empty()) {
        value.clear();
        return true;
    }
    if (rest[0] != '=') return false; // a sentence in a comment
    value = trim(rest.substr(1));
    return true;
}

bool repoKey(const std::string& k) {
    return k == "Include" || k == "Server" || k == "SigLevel" || k == "Usage" || k == "CacheServer";
}

size_t findHeader(const std::vector<std::string>& ls, const std::string& name) {
    for (size_t i = 0; i < ls.size(); ++i) {
        std::string n;
        bool e;
        if (header(ls[i], n, e) && n == name) return i;
    }
    return npos;
}

// [options]: its header and where its lines end
bool optionsRange(const std::vector<std::string>& ls, size_t& from, size_t& to) {
    from = npos;
    for (size_t i = 0; i < ls.size(); ++i) {
        std::string n;
        bool e;
        if (header(ls[i], n, e) && n == "options" && e) {
            from = i;
            break;
        }
    }
    if (from == npos) return false;
    to = from + 1;
    while (to < ls.size() && !isHeader(ls[to])) ++to;
    return true;
}

std::string uncomment(const std::string& line) {
    const size_t a = line.find_first_not_of("# \t");
    return a == npos ? std::string() : line.substr(a);
}

} // namespace

bool official(const std::string& repo) {
    return repo == "core" || repo == "extra" || repo == "multilib" || repo == "core-testing" || repo == "extra-testing" ||
           repo == "multilib-testing";
}

Conf parse(const std::string& text) {
    Conf c;
    const auto ls = lines(text);
    bool options = false, open = false; // open: a disabled repo's lines still coming
    long cur = -1;
    for (const auto& line : ls) {
        std::string name;
        bool enabled;
        if (header(line, name, enabled)) {
            options = name == "options" && enabled;
            cur = -1;
            if (name == "options") continue;
            c.repos.push_back({name, enabled, {}, {}, {}});
            cur = static_cast<long>(c.repos.size()) - 1;
            open = true;
            continue;
        }
        std::string key, value;
        bool on;
        const bool isDirective = directive(line, key, value, on);
        if (options) {
            if (!isDirective || !on) continue;
            if (key == "ParallelDownloads") {
                try {
                    c.parallelDownloads = std::max(1, std::stoi(value));
                } catch (...) {
                }
            } else if (key == "IgnorePkg") {
                std::stringstream ws(value);
                for (std::string w; ws >> w;) c.ignored.push_back(w);
            }
            continue;
        }
        if (cur < 0) continue;
        auto& r = c.repos[static_cast<size_t>(cur)];
        // A disabled repo's lines run on to the first blank line or other comment
        if (!r.enabled && (!open || !isDirective || on || !repoKey(key))) {
            open = false;
            continue;
        }
        if (r.enabled && (!isDirective || !on)) continue;
        if (key == "Server") r.servers.push_back(value);
        else if (key == "Include") r.includes.push_back(value);
        else if (key == "SigLevel") r.sigLevel = value;
    }
    // pacman.conf's own example, not a repo anyone set up
    std::erase_if(c.repos, [](const Repo& r) {
        return r.name == "custom" && !r.enabled && r.servers == std::vector<std::string>{"file:///home/custompkgs"};
    });
    return c;
}

std::string setEnabled(const std::string& text, const std::string& repo, bool on) {
    auto ls = lines(text);
    const size_t at = findHeader(ls, repo);
    if (at == npos) {
        if (!on) return text;
        const std::vector<std::string> block = {"[" + repo + "]", "Include = /etc/pacman.d/mirrorlist"};
        if (repo.ends_with("-testing")) {
            const size_t stable = findHeader(ls, repo.substr(0, repo.size() - 8));
            if (stable != npos) {
                std::vector<std::string> b = block;
                b.emplace_back("");
                ls.insert(ls.begin() + static_cast<long>(stable), b.begin(), b.end());
                return join(ls);
            }
        }
        if (!ls.empty() && !blank(ls.back())) ls.emplace_back("");
        ls.insert(ls.end(), block.begin(), block.end());
        return join(ls);
    }
    std::string name;
    bool enabled;
    header(ls[at], name, enabled);
    if (enabled == on) return text;
    if (on) {
        ls[at] = "[" + repo + "]";
        for (size_t i = at + 1; i < ls.size(); ++i) {
            std::string key, value;
            bool lineOn;
            if (blank(ls[i]) || isHeader(ls[i]) || !directive(ls[i], key, value, lineOn) || !repoKey(key)) break;
            if (!lineOn) ls[i] = uncomment(ls[i]);
        }
    } else {
        ls[at] = "#[" + repo + "]";
        for (size_t i = at + 1; i < ls.size() && !isHeader(ls[i]); ++i)
            if (!blank(ls[i]) && trim(ls[i])[0] != '#') ls[i] = "#" + ls[i];
    }
    return join(ls);
}

std::string addRepo(const std::string& text, const std::string& name, const std::string& server, const std::string& sigLevel) {
    auto ls = lines(text);
    if (!ls.empty() && !blank(ls.back())) ls.emplace_back("");
    ls.push_back("[" + name + "]");
    if (!sigLevel.empty()) ls.push_back("SigLevel = " + sigLevel);
    ls.push_back("Server = " + server);
    return join(ls);
}

std::string removeRepo(const std::string& text, const std::string& name) {
    auto ls = lines(text);
    const size_t at = findHeader(ls, name);
    if (at == npos) return text;
    size_t end = at + 1;
    while (end < ls.size() && !blank(ls[end]) && !isHeader(ls[end])) ++end;
    if (end < ls.size() && blank(ls[end])) ++end;            // the blank line after it
    else if (at > 0 && blank(ls[at - 1]) && end == ls.size()) // last in the file: the one before
        return join({ls.begin(), ls.begin() + static_cast<long>(at - 1)});
    ls.erase(ls.begin() + static_cast<long>(at), ls.begin() + static_cast<long>(end));
    return join(ls);
}

std::string setOption(const std::string& text, const std::string& option, const std::string& value) {
    auto ls = lines(text);
    size_t from, to;
    if (!optionsRange(ls, from, to)) return text;
    size_t enabledAt = npos, commentedAt = npos, last = from;
    for (size_t i = from + 1; i < to; ++i) {
        std::string key, v;
        bool on;
        if (!directive(ls[i], key, v, on)) continue;
        last = i;
        if (key != option) continue;
        if (on && enabledAt == npos) enabledAt = i;
        if (!on && commentedAt == npos) commentedAt = i;
    }
    const std::string set = option + " = " + value;
    if (value.empty()) {
        if (enabledAt != npos) ls[enabledAt] = "#" + ls[enabledAt];
    } else if (enabledAt != npos) {
        ls[enabledAt] = set;
    } else if (commentedAt != npos) {
        ls[commentedAt] = set;
    } else {
        ls.insert(ls.begin() + static_cast<long>(last + 1), set);
    }
    return join(ls);
}

std::string setIgnored(const std::string& text, const std::vector<std::string>& packages) {
    std::string list;
    for (const auto& p : packages) list += (list.empty() ? "" : " ") + p;
    // One IgnorePkg line: the rest of them go
    auto ls = lines(setOption(text, "IgnorePkg", list));
    size_t from, to;
    if (!optionsRange(ls, from, to)) return join(ls);
    bool kept = false;
    for (size_t i = from + 1; i < to;) {
        std::string key, v;
        bool on;
        if (directive(ls[i], key, v, on) && on && key == "IgnorePkg") {
            if (kept) {
                ls.erase(ls.begin() + static_cast<long>(i));
                --to;
                continue;
            }
            kept = true;
        }
        ++i;
    }
    return join(ls);
}

std::vector<std::string> servers(const std::string& mirrorlist) {
    std::vector<std::string> out;
    for (const auto& l : lines(mirrorlist)) {
        std::string key, value;
        bool on;
        if (directive(l, key, value, on) && on && key == "Server" && !value.empty()) out.push_back(value);
    }
    return out;
}

std::string disableServer(const std::string& mirrorlist, const std::string& url) {
    auto ls = lines(mirrorlist);
    for (auto& l : ls) {
        std::string key, value;
        bool on;
        if (directive(l, key, value, on) && on && key == "Server" && value == url) {
            l = "#" + l;
            break;
        }
    }
    return join(ls);
}

std::string mirrorlist(const std::vector<std::string>& bases, const std::string& note) {
    std::string out = "##\n## Arch Linux repository mirrorlist\n## " + note + "\n##\n\n";
    for (std::string b : bases) {
        if (!b.ends_with('/')) b += '/';
        out += "Server = " + b + "$repo/os/$arch\n";
    }
    return out;
}

std::string host(const std::string& url) {
    size_t a = url.find("://");
    a = a == npos ? 0 : a + 3;
    const size_t b = url.find('/', a);
    return url.substr(a, b == npos ? npos : b - a);
}

} // namespace m3::pacmanconf
