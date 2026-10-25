// Reading CUPS's lpstat, apart from the rest so the unit test can link it
// alone (no event loop, no processes)
#include "services/Printers.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace m3 {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

} // namespace

// What someone types for a printer that doesn't announce itself: an IP or
// a host name (a port, a path), or a whole ipp:// or ipps:// address.
// Driverless printing speaks IPP, so other schemes (socket://, http://)
// aren't taken
std::string Printers::ippAddress(const std::string& typed) {
    std::string a = trimmed(typed);
    if (a.empty()) return {};
    std::string scheme = "ipp://";
    if (a.starts_with("ipp://") || a.starts_with("ipps://")) {
        scheme = a.substr(0, a.find("://") + 3);
        a = a.substr(scheme.size());
    } else if (a.find("://") != std::string::npos) {
        return {};
    }
    const auto slash = a.find('/');
    const std::string host = a.substr(0, slash);
    // Letters, digits, dots, dashes; a port after a colon; IPv6 in brackets
    if (host.empty() || !std::ranges::all_of(host, [](char ch) {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-' || ch == ':' || ch == '[' || ch == ']' || ch == '_';
        }))
        return {};
    // Without a path: where IPP Everywhere printers take jobs
    const std::string path = slash == std::string::npos || slash + 1 == a.size() ? "ipp/print" : a.substr(slash + 1);
    return scheme + host + "/" + path;
}

// "printer HP is idle.  enabled since …", "printer HP now printing HP-3.  …",
// "printer HP disabled since … -" then its reason indented; the details
// (Description:, Location:) indented under each
std::vector<Printers::Printer> Printers::parsePrinters(const std::string& lpstatP, const std::string& lpstatD) {
    std::vector<Printer> out;
    std::stringstream ss(lpstatP);
    for (std::string line; std::getline(ss, line);) {
        if (line.starts_with("printer ")) {
            Printer p;
            std::stringstream ls(line.substr(8));
            ls >> p.name;
            const std::string rest = line.substr(8 + p.name.size());
            p.enabled = rest.find("disabled") == std::string::npos;
            p.state = !p.enabled ? "stopped" : rest.find("now printing") != std::string::npos ? "printing" : "idle";
            // "now printing HP-3.  enabled since…"
            if (const auto at = rest.find("now printing "); at != std::string::npos) {
                std::stringstream js(rest.substr(at + 13));
                js >> p.job;
                if (p.job.ends_with('.')) p.job.pop_back();
            }
            out.push_back(std::move(p));
            continue;
        }
        if (out.empty() || line.empty() || !std::isspace(static_cast<unsigned char>(line[0]))) continue;
        const std::string t = trimmed(line);
        auto& p = out.back();
        if (t.starts_with("Description:")) p.description = trimmed(t.substr(12));
        else if (t.starts_with("Location:")) p.location = trimmed(t.substr(9));
        else if (!p.enabled && p.message.empty() && !t.ends_with(":") && t.find(':') == std::string::npos) p.message = t;
    }
    // "system default destination: HP" (or "no system default destination")
    const std::string key = "default destination: ";
    if (const auto at = lpstatD.find(key); at != std::string::npos) {
        const std::string name = trimmed(lpstatD.substr(at + key.size(), lpstatD.find('\n', at) - at - key.size()));
        for (auto& p : out) p.isDefault = p.name == name;
    }
    return out;
}

// "HP-12   abishek   1024   Sat 25 Oct 2026 10:00:00 AM IST"
std::vector<Printers::Job> Printers::parseJobs(const std::string& lpstatO) {
    std::vector<Job> out;
    std::stringstream ss(lpstatO);
    for (std::string line; std::getline(ss, line);) {
        std::stringstream ls(line);
        Job j;
        std::string size;
        if (!(ls >> j.id >> j.user >> size)) continue;
        const auto dash = j.id.rfind('-');
        if (dash == std::string::npos) continue;
        j.printer = j.id.substr(0, dash);
        j.size = std::atoll(size.c_str());
        out.push_back(std::move(j));
    }
    return out;
}

// "Rank    Owner   Job     File(s)          Total Size" then
// "1st     abishek 1       hello.txt        1024 bytes"
void Printers::parseTitles(const std::string& lpqA, std::vector<Job>& jobs) {
    std::stringstream ss(lpqA);
    for (std::string line; std::getline(ss, line);) {
        std::stringstream ls(line);
        std::vector<std::string> words;
        for (std::string w; ls >> w;) words.push_back(w);
        if (words.size() < 6 || words.back() != "bytes") continue; // the header, "no entries"
        std::string title;
        for (size_t i = 3; i + 2 < words.size(); ++i) title += (title.empty() ? "" : " ") + words[i];
        for (auto& j : jobs)
            if (j.id.ends_with("-" + words[2])) j.title = title;
    }
}

std::string Printers::summary(bool installed, bool running, const std::vector<Printer>& printers, const std::vector<Job>& jobs) {
    if (!installed) return "Not set up";
    if (!running) return "Not running";
    if (printers.empty()) return "No printers";
    const auto stateOf = [&](const std::string& name) {
        for (const auto& p : printers)
            if (p.name == name) return p.state;
        return std::string();
    };
    if (!jobs.empty()) {
        const std::string count = std::to_string(jobs.size()) + (jobs.size() == 1 ? " job" : " jobs");
        if (std::ranges::any_of(printers, [](const Printer& p) { return p.state == "printing"; })) return "Printing · " + count;
        // Held by a paused printer: paused comes first, it's why they wait
        if (std::ranges::any_of(jobs, [&](const Job& j) { return stateOf(j.printer) == "stopped"; })) return "Paused · " + count + " waiting";
        return count + " waiting";
    }
    const Printer* def = &printers.front();
    for (const auto& p : printers)
        if (p.isDefault) def = &p;
    return def->state == "stopped" ? "Paused" : "Ready";
}

} // namespace m3
