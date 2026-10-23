#pragma once

#include "core/Signal.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// One Spotlight row
struct SearchResult {
    std::string title, subtitle;
    std::string icon;   // theme icon name or file path
    std::string glyph;  // Material Symbol (used when there is no icon)
    std::string emoji;  // drawn as text instead of an icon
    double score = 0;
    std::function<void()> activate;
};

// Installed applications from XDG .desktop files, ranked by how often they
// are launched (counts kept in ~/.local/state/material3ui/launches.json).
class Apps {
public:
    struct App {
        std::string id, name, generic, comment, exec, icon, keywords, wmClass;
        std::string mimeTypes, categories; // ';'-separated, as in the file
        std::string path;                  // the .desktop file
        bool terminal = false;
    };

    static Apps& get();
    const std::vector<App>& all() const { return m_apps; }
    void reload();
    Signal<> changed; // the list reloaded: apps were installed or removed
    void launch(const App& app);
    // An installed Quick Share app (Android's file sharing, open source:
    // RQuickShare, Packet), or nullptr
    const App* quickShare() const;
    int launches(const std::string& id) const;
    // Icon for a window class (Hyprland's "class"), via StartupWMClass or the desktop id
    std::string iconForClass(const std::string& cls) const;

private:
    Apps();
    void saveCounts() const;
    void watch(const std::string& dir);
    std::vector<App> m_apps;
    std::vector<std::pair<std::string, int>> m_counts;
};

// Subsequence fuzzy match: 0 for no match, higher is better
double fuzzyScore(const std::string& query, const std::string& text);

// Arithmetic with + - * / % ^, parentheses, constants and functions
// (sqrt, sin, cos, tan, log, ln, abs, round, floor, ceil)
std::optional<double> evaluate(const std::string& expression);
std::string formatNumber(double v);

// Spotlight providers. Modes by prefix: "=" calculator, ":" emoji,
// "#" clipboard history (cliphist), "@" files (fd), ">" run a command;
// otherwise applications, plus the calculator when the query is maths.
namespace search {
std::vector<SearchResult> query(const std::string& text);
// Providers that run a process report back asynchronously
void queryAsync(const std::string& text, std::function<void(std::vector<SearchResult>)> done);
} // namespace search

} // namespace m3
