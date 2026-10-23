#include "services/Search.h"

#include "core/FileWatcher.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace m3 {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> applicationDirs() {
    std::vector<std::string> dirs;
    const char* home = std::getenv("XDG_DATA_HOME");
    dirs.push_back(home && *home ? std::string(home) + "/applications" : paths::home() + "/.local/share/applications");
    for (const auto& d : paths::dataDirs()) dirs.push_back(d + "/applications");
    return dirs;
}

// Exec without field codes (%f %U …); "%%" is a literal percent
std::string cleanExec(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec[i] == '%' && i + 1 < exec.size()) {
            if (exec[i + 1] == '%') out += '%';
            ++i;
            continue;
        }
        out += exec[i];
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string countsFile() {
    return paths::stateDir() + "/launches.json";
}

} // namespace

// ── Apps ──

Apps& Apps::get() {
    static Apps instance;
    return instance;
}

Apps::Apps() {
    reload();
    // Apps installed or removed while the shell runs show up without a restart
    for (const auto& dir : applicationDirs()) watch(dir);
    std::ifstream in(countsFile());
    try {
        if (in) {
            const auto doc = nlohmann::json::parse(in); // items() only references it
            for (const auto& [id, n] : doc.items()) m_counts.emplace_back(id, n.get<int>());
        }
    } catch (...) {
    }
}

void Apps::reload() {
    m_apps.clear();
    std::set<std::string> seen; // the first directory wins (user overrides system)
    const char* desktopEnv = std::getenv("XDG_CURRENT_DESKTOP");
    const std::string desktop = desktopEnv ? desktopEnv : "Hyprland";
    for (const auto& dir : applicationDirs()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->path().extension() != ".desktop") continue;
            // Desktop file IDs use '-' for subdirectories
            std::string id = fs::relative(it->path(), dir, ec).string();
            std::ranges::replace(id, '/', '-');
            if (!seen.insert(id).second) continue;

            std::ifstream in(it->path());
            std::string line;
            bool inEntry = false, hidden = false, isApp = false;
            App app;
            app.id = id;
            app.path = it->path().string();
            while (std::getline(in, line)) {
                if (line.starts_with('[')) {
                    inEntry = line == "[Desktop Entry]";
                    continue;
                }
                if (!inEntry) continue;
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
                if (key == "Type") isApp = val == "Application";
                else if (key == "Name") app.name = val;
                else if (key == "GenericName") app.generic = val;
                else if (key == "Comment") app.comment = val;
                else if (key == "Exec") app.exec = cleanExec(val);
                else if (key == "Icon") app.icon = val;
                else if (key == "Keywords") app.keywords = val;
                else if (key == "StartupWMClass") app.wmClass = val;
                else if (key == "MimeType") app.mimeTypes = val;
                else if (key == "Categories") app.categories = val;
                else if (key == "Terminal") app.terminal = val == "true";
                else if ((key == "NoDisplay" || key == "Hidden") && val == "true") hidden = true;
                else if (key == "OnlyShowIn" && val.find(desktop) == std::string::npos) hidden = true;
                else if (key == "NotShowIn" && val.find(desktop) != std::string::npos) hidden = true;
            }
            if (isApp && !hidden && !app.name.empty() && !app.exec.empty()) m_apps.push_back(std::move(app));
        }
    }
    std::ranges::sort(m_apps, [](const App& a, const App& b) { return lower(a.name) < lower(b.name); });
}

void Apps::watch(const std::string& dir) {
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        FileWatcher::get().watchDir(dir, [this] {
            reload();
            changed.emit();
        });
        return;
    }
    // Not there yet (Flatpak's exports appear with its first app): watch the
    // nearest parent that exists, and step down as the path gets created
    fs::path parent = fs::path(dir).parent_path();
    while (parent.has_relative_path() && !fs::is_directory(parent, ec)) parent = parent.parent_path();
    auto done = std::make_shared<bool>(false);
    FileWatcher::get().watchDir(parent, [this, dir, parent, done] {
        if (*done) return;
        std::error_code ec;
        fs::path p = fs::path(dir);
        while (p != parent && !fs::is_directory(p, ec)) p = p.parent_path();
        if (p == parent) return;
        *done = true;
        watch(dir);
        if (p == fs::path(dir)) {
            reload();
            changed.emit();
        }
    });
}

int Apps::launches(const std::string& id) const {
    auto it = std::ranges::find(m_counts, id, &std::pair<std::string, int>::first);
    return it != m_counts.end() ? it->second : 0;
}

std::string Apps::iconForClass(const std::string& cls) const {
    const std::string c = lower(cls);
    for (const auto& a : m_apps)
        if (lower(a.wmClass) == c) return a.icon;
    for (const auto& a : m_apps) {
        std::string id = lower(a.id);
        if (id.ends_with(".desktop")) id.resize(id.size() - 8);
        // "org.gnome.Nautilus" ↔ "nautilus", "firefox" ↔ "firefox"
        if (id == c || id.ends_with("." + c)) return a.icon;
    }
    return c; // many apps name their icon after their class
}

void Apps::launch(const App& app) {
    const char* term = std::getenv("TERMINAL");
    spawn(app.terminal ? std::format("{} -e {}", term && *term ? term : "alacritty", app.exec) : app.exec);
    auto it = std::ranges::find(m_counts, app.id, &std::pair<std::string, int>::first);
    if (it != m_counts.end())
        ++it->second;
    else
        m_counts.emplace_back(app.id, 1);
    saveCounts();
}

void Apps::saveCounts() const {
    std::error_code ec;
    fs::create_directories(paths::stateDir(), ec);
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, n] : m_counts) j[id] = n;
    std::ofstream(countsFile()) << j.dump();
}

// ── Fuzzy matching ──

double fuzzyScore(const std::string& query, const std::string& text) {
    if (query.empty()) return 1;
    const std::string q = lower(query), t = lower(text);
    if (t == q) return 100;
    if (t.starts_with(q)) return 80 - static_cast<double>(t.size() - q.size()) * 0.1;
    if (auto pos = t.find(q); pos != std::string::npos) {
        // Matches at word starts beat matches mid-word
        const bool wordStart = pos > 0 && (t[pos - 1] == ' ' || t[pos - 1] == '-' || t[pos - 1] == '_');
        return (wordStart ? 60 : 45) - static_cast<double>(pos) * 0.2;
    }
    // Every query character in order, rewarding runs
    double score = 0;
    size_t ti = 0, run = 0;
    for (char c : q) {
        const auto found = t.find(c, ti);
        if (found == std::string::npos) return 0;
        run = found == ti ? run + 1 : 0;
        score += 1 + static_cast<double>(run);
        ti = found + 1;
    }
    return std::min(40.0, score * 2);
}

// ── Calculator ──

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : m_s(s) {}

    std::optional<double> parse() {
        auto v = expr();
        skip();
        if (!v || m_i != m_s.size()) return std::nullopt;
        return v;
    }

private:
    void skip() {
        while (m_i < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_i]))) ++m_i;
    }
    bool eat(char c) {
        skip();
        if (m_i < m_s.size() && m_s[m_i] == c) {
            ++m_i;
            return true;
        }
        return false;
    }
    std::optional<double> expr() {
        auto v = term();
        while (v) {
            if (eat('+')) {
                auto r = term();
                if (!r) return std::nullopt;
                *v += *r;
            } else if (eat('-')) {
                auto r = term();
                if (!r) return std::nullopt;
                *v -= *r;
            } else {
                break;
            }
        }
        return v;
    }
    std::optional<double> term() {
        auto v = power();
        while (v) {
            if (eat('*') || eat('x')) {
                auto r = power();
                if (!r) return std::nullopt;
                *v *= *r;
            } else if (eat('/')) {
                auto r = power();
                if (!r) return std::nullopt;
                *v /= *r;
            } else if (eat('%')) {
                auto r = power();
                if (!r) return std::nullopt;
                *v = std::fmod(*v, *r);
            } else {
                break;
            }
        }
        return v;
    }
    std::optional<double> power() {
        auto base = unary();
        if (base && eat('^')) {
            auto e = power(); // right associative
            if (!e) return std::nullopt;
            return std::pow(*base, *e);
        }
        return base;
    }
    std::optional<double> unary() {
        if (eat('-')) {
            auto v = unary();
            return v ? std::optional(-*v) : std::nullopt;
        }
        if (eat('+')) return unary();
        return primary();
    }
    std::optional<double> primary() {
        skip();
        if (eat('(')) {
            auto v = expr();
            if (!v || !eat(')')) return std::nullopt;
            return v;
        }
        if (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '.')) {
            size_t used = 0;
            try {
                const double v = std::stod(m_s.substr(m_i), &used);
                m_i += used;
                return v;
            } catch (...) {
                return std::nullopt;
            }
        }
        std::string name;
        while (m_i < m_s.size() && std::isalpha(static_cast<unsigned char>(m_s[m_i]))) name += m_s[m_i++];
        if (name.empty()) return std::nullopt;
        if (name == "pi") return M_PI;
        if (name == "e") return M_E;
        static const std::map<std::string, double (*)(double)> fns = {
            {"sqrt", [](double x) { return std::sqrt(x); }}, {"sin", [](double x) { return std::sin(x); }},
            {"cos", [](double x) { return std::cos(x); }},   {"tan", [](double x) { return std::tan(x); }},
            {"log", [](double x) { return std::log10(x); }}, {"ln", [](double x) { return std::log(x); }},
            {"abs", [](double x) { return std::fabs(x); }},  {"round", [](double x) { return std::round(x); }},
            {"floor", [](double x) { return std::floor(x); }}, {"ceil", [](double x) { return std::ceil(x); }},
        };
        auto fn = fns.find(name);
        if (fn == fns.end() || !eat('(')) return std::nullopt;
        auto arg = expr();
        if (!arg || !eat(')')) return std::nullopt;
        return fn->second(*arg);
    }

    const std::string& m_s;
    size_t m_i = 0;
};

} // namespace

std::optional<double> evaluate(const std::string& expression) {
    auto v = Parser(expression).parse();
    if (!v || !std::isfinite(*v)) return std::nullopt;
    return v;
}

std::string formatNumber(double v) {
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) return std::format("{}", static_cast<long long>(std::llround(v)));
    std::string s = std::format("{:.10g}", v);
    return s;
}

// ── Providers ──

namespace search {

namespace {

struct Emoji {
    std::string glyph, group, name;
};

const std::vector<Emoji>& emojis() {
    static std::vector<Emoji> list = [] {
        std::vector<Emoji> out;
        std::ifstream in(paths::dataFile("emojis.txt"));
        std::string line;
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            Emoji e;
            std::getline(ss, e.glyph, '\t');
            std::getline(ss, e.group, '\t');
            std::getline(ss, e.name, '\t');
            if (!e.glyph.empty()) out.push_back(std::move(e));
        }
        return out;
    }();
    return list;
}

void copy(const std::string& text) {
    run({"wl-copy", "--", text}, nullptr);
}

std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(' ');
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(' ') - a + 1);
}

bool looksLikeMath(const std::string& s) {
    return s.find_first_of("0123456789") != std::string::npos && s.find_first_of("+-*/^%()") != std::string::npos;
}

void addCalc(std::vector<SearchResult>& out, const std::string& expr, double score) {
    if (auto v = evaluate(expr)) {
        const std::string text = formatNumber(*v);
        out.push_back({text, "= " + expr + " · Enter copies the result", "", "calculate", "", score,
                       [text] { copy(text); }});
    }
}

} // namespace

std::vector<SearchResult> query(const std::string& raw) {
    std::vector<SearchResult> out;
    const std::string text = trimmed(raw);

    if (text.starts_with('=')) {
        addCalc(out, text.substr(1), 100);
        if (out.empty()) out.push_back({"Type an expression", "e.g. 2^10, sqrt(2), (4+5)*3", "", "calculate", "", 0, {}});
        return out;
    }
    if (text.starts_with(':')) {
        const std::string q = trimmed(text.substr(1));
        for (const auto& e : emojis()) {
            const double s = q.empty() ? 1 : std::max(fuzzyScore(q, e.name), fuzzyScore(q, e.group) * 0.5);
            if (s > 0) out.push_back({e.name, e.group, "", "", e.glyph, s, [g = e.glyph] { copy(g); }});
        }
        std::ranges::stable_sort(out, std::greater{}, &SearchResult::score);
        if (out.size() > 60) out.resize(60);
        return out;
    }
    if (text.starts_with('>')) {
        const std::string cmd = trimmed(text.substr(1));
        out.push_back({cmd.empty() ? "Run a command" : cmd, "Run in a shell", "", "terminal", "", 100,
                       [cmd] {
                           if (!cmd.empty()) spawn(cmd);
                       }});
        return out;
    }
    if (text.starts_with('#') || text.starts_with('@')) return out; // async providers

    for (const auto& app : Apps::get().all()) {
        double s = text.empty() ? 1 : std::max({fuzzyScore(text, app.name), fuzzyScore(text, app.generic) * 0.7,
                                                fuzzyScore(text, app.keywords) * 0.6, fuzzyScore(text, app.id) * 0.5});
        if (s <= 0) continue;
        s += std::min(20.0, std::log2(1.0 + Apps::get().launches(app.id)) * 4);
        out.push_back({app.name, !app.comment.empty() ? app.comment : app.generic, app.icon, "apps", "", s,
                       [app] { Apps::get().launch(app); }});
    }
    if (looksLikeMath(text)) addCalc(out, text, 200);
    std::ranges::stable_sort(out, std::greater{}, &SearchResult::score);
    if (out.size() > 50) out.resize(50);
    return out;
}

void queryAsync(const std::string& raw, std::function<void(std::vector<SearchResult>)> done) {
    const std::string text = trimmed(raw);
    if (text.starts_with('#')) {
        const std::string q = trimmed(text.substr(1));
        run({"cliphist", "list"}, [q, done](int code, const std::string& output) {
            std::vector<SearchResult> out;
            if (code != 0) {
                out.push_back({"Clipboard history is unavailable", "Install cliphist and run wl-paste --watch cliphist store",
                               "", "content_paste_off", "", 0, {}});
                return done(out);
            }
            std::stringstream ss(output);
            std::string line;
            double rank = 1000;
            while (std::getline(ss, line) && out.size() < 80) {
                const auto tab = line.find('\t');
                if (tab == std::string::npos) continue;
                const std::string preview = line.substr(tab + 1);
                const double s = q.empty() ? rank-- : fuzzyScore(q, preview);
                if (s <= 0) continue;
                out.push_back({preview, "Enter copies it again", "", preview.starts_with("[[ binary") ? "image" : "content_paste",
                               "", s, [line] {
                                   // Decode the entry and put it back on the clipboard
                                   run({"cliphist", "decode"}, [](int, const std::string& data) {
                                       run({"wl-copy"}, nullptr, data);
                                   }, line);
                               }});
            }
            std::ranges::stable_sort(out, std::greater{}, &SearchResult::score);
            done(out);
        });
        return;
    }
    if (text.starts_with('@')) {
        const std::string q = trimmed(text.substr(1));
        if (q.size() < 2) return done({{"Search files", "Type at least two characters", "", "search", "", 0, {}}});
        run({"fd", "--max-results", "40", "--hidden", "--exclude", ".git", "--", q, paths::home()},
            [done](int code, const std::string& output) {
                std::vector<SearchResult> out;
                if (code < 0) {
                    out.push_back({"File search needs fd", "Install the fd package", "", "search_off", "", 0, {}});
                    return done(out);
                }
                std::stringstream ss(output);
                std::string path;
                while (std::getline(ss, path)) {
                    const fs::path p(path);
                    const bool dir = path.ends_with('/');
                    std::string shown = path;
                    if (shown.starts_with(paths::home())) shown = "~" + shown.substr(paths::home().size());
                    out.push_back({dir ? p.parent_path().filename().string() : p.filename().string(), shown, "",
                                   dir ? "folder" : "description", "", 1, [path] { run({"xdg-open", path}, nullptr); }});
                }
                done(out);
            });
        return;
    }
    done({});
}

} // namespace search

} // namespace m3
