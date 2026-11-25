#include "services/Search.h"

#include "core/Config.h"
#include "core/FileWatcher.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Alarms.h"
#include "services/Clipboard.h"
#include "services/Currency.h"
#include "services/Packages.h"
#include "services/Units.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <cstring>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <regex>
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
    const std::string desktop = desktopEnv ? desktopEnv : "niri";
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
            // Its shortcuts: the order Actions= lists, each from its section
            std::vector<std::string> order;
            std::map<std::string, App::Action> sections;
            std::string action; // the [Desktop Action …] being read
            while (std::getline(in, line)) {
                if (line.starts_with('[')) {
                    inEntry = line == "[Desktop Entry]";
                    action = line.starts_with("[Desktop Action ") && line.ends_with(']') ? line.substr(16, line.size() - 17) : "";
                    continue;
                }
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
                if (!action.empty()) {
                    auto& a = sections[action];
                    a.id = action;
                    if (key == "Name") a.name = val;
                    else if (key == "Exec") a.exec = cleanExec(val);
                    else if (key == "Icon") a.icon = val;
                    continue;
                }
                if (!inEntry) continue;
                if (key == "Actions") {
                    std::stringstream ss(val);
                    for (std::string a; std::getline(ss, a, ';');)
                        if (!a.empty()) order.push_back(a);
                }
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
            for (const auto& a : order)
                if (auto s = sections.find(a); s != sections.end() && !s->second.name.empty() && !s->second.exec.empty())
                    app.actions.push_back(s->second);
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

const Apps::App* Apps::forClass(const std::string& cls) const {
    const std::string c = lower(cls);
    for (const auto& a : m_apps)
        if (!a.wmClass.empty() && lower(a.wmClass) == c) return &a;
    for (const auto& a : m_apps) {
        std::string id = lower(a.id);
        if (id.ends_with(".desktop")) id.resize(id.size() - 8);
        if (id == c || id.ends_with("." + c)) return &a;
    }
    return nullptr;
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

void Apps::launch(const App& app, const App::Action& action) {
    App as = app;
    as.exec = action.exec;
    launch(as); // counted as the app's
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

std::vector<SearchResult> noteResults(const std::string& q);

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

std::string urlEncoded(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

void addConversion(std::vector<SearchResult>& out, const std::string& text, double score) {
    const auto c = units::convert(text);
    if (!c) return;
    out.push_back({c->text, std::format("{} {} in {} · Enter copies the result", formatNumber(c->value), c->from, c->to), "", "straighten", "",
                   score, [t = c->text] { copy(t); }});
}

} // namespace

const std::vector<std::pair<std::string, std::string>>& engines() {
    static const std::vector<std::pair<std::string, std::string>> e = {
        {"duckduckgo", "DuckDuckGo"}, {"google", "Google"}, {"bing", "Bing"},
        {"brave", "Brave Search"},    {"startpage", "Startpage"}, {"ecosia", "Ecosia"},
    };
    return e;
}

std::string webSearchUrl(const std::string& engine, const std::string& q) {
    const std::string e = urlEncoded(q);
    if (engine == "google") return "https://www.google.com/search?q=" + e;
    if (engine == "bing") return "https://www.bing.com/search?q=" + e;
    if (engine == "brave") return "https://search.brave.com/search?q=" + e;
    if (engine == "startpage") return "https://www.startpage.com/do/search?q=" + e;
    if (engine == "ecosia") return "https://www.ecosia.org/search?q=" + e;
    return "https://duckduckgo.com/?q=" + e;
}

namespace {
// Files opened lately, as apps record them (~/.local/share/recently-used.xbel):
// newest first, only those still there; read again when the file changes
struct Recent {
    std::string path;
    std::string when;                // ISO time, sorts as text
    std::string mime;                // as recorded
    std::vector<std::string> apps;   // who opened it: names and programs, lower case
};

std::string recentFile() {
    const char* x = std::getenv("XDG_DATA_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/recently-used.xbel";
}

std::string unescapeXml(std::string s) {
    for (const auto& [from, to] : {std::pair{"&apos;", "'"}, {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}}) {
        for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += std::strlen(to)) s.replace(at, std::strlen(from), to);
    }
    return s;
}

std::string escapeXml(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += c;
        }
    }
    return out;
}

// A program's name from an Exec line: "'/usr/bin/gedit %U'" → "gedit"
std::string programOf(std::string exec) {
    exec = unescapeXml(exec);
    std::erase(exec, '\'');
    std::erase(exec, '"');
    const auto sp = exec.find(' ');
    if (sp != std::string::npos) exec.resize(sp);
    return lower(fs::path(exec).filename().string());
}

std::string uriDecode(const std::string& raw) {
    std::string path;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) && std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
            path += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            path += raw[i];
        }
    }
    return path;
}

std::string uriEncode(const std::string& path) {
    std::string out = "file://";
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::strchr("/-_.~!$&'()*+,;=:@", c)) out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

const std::vector<Recent>& recentFiles() {
    static std::vector<Recent> list;
    static fs::file_time_type seen{};
    const std::string file = recentFile();
    std::error_code ec;
    const auto mtime = fs::last_write_time(file, ec);
    if (ec || mtime == seen) return list;
    seen = mtime;
    list.clear();
    std::ifstream in(file);
    const std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    static const std::regex href(R"re(href="file://([^"]+)")re");
    static const std::regex stamp(R"re((?:visited|modified)="([^"]+)")re");
    static const std::regex mime(R"re(<mime:mime-type\s+type="([^"]+)")re");
    static const std::regex app(R"re(<bookmark:application\s+name="([^"]*)"\s+exec="([^"]*)")re");
    for (size_t at = 0; (at = xml.find("<bookmark ", at)) != std::string::npos;) {
        const size_t end = std::min(xml.find("</bookmark>", at), xml.size());
        const std::string block = xml.substr(at, end - at);
        at = end;
        const std::string tag = block.substr(0, block.find('>'));
        std::smatch m;
        if (!std::regex_search(tag, m, href)) continue;
        Recent r;
        r.path = uriDecode(m[1].str());
        for (auto s = std::sregex_iterator(tag.begin(), tag.end(), stamp); s != std::sregex_iterator(); ++s) r.when = std::max(r.when, (*s)[1].str());
        if (std::regex_search(block, m, mime)) r.mime = m[1].str();
        for (auto a = std::sregex_iterator(block.begin(), block.end(), app); a != std::sregex_iterator(); ++a) {
            r.apps.push_back(lower(unescapeXml((*a)[1].str())));
            r.apps.push_back(programOf((*a)[2].str()));
        }
        if (fs::is_regular_file(r.path, ec)) list.push_back(std::move(r));
    }
    std::ranges::sort(list, std::greater{}, &Recent::when);
    return list;
}

SearchResult recentRow(const std::string& path, double score) {
    const fs::path p(path);
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool picture = ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif";
    std::string shown = p.parent_path().string();
    if (shown.starts_with(paths::home())) shown = "~" + shown.substr(paths::home().size());
    SearchResult r{p.filename().string(), "Recent · " + shown, "",
                   picture ? "image" : ext == ".pdf" ? "picture_as_pdf" : ext == ".mp4" || ext == ".mkv" || ext == ".webm" ? "movie" : "description", "", score,
                   [path] { run({"xdg-open", path}, nullptr); }};
    if (picture) r.image = path;
    return r;
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
    if (text.starts_with('#') || text.starts_with('@') || text.starts_with('?')) return out; // async providers

    // Recent files: the last three on an empty search, any whose name has
    // what's typed in it after that (Settings → Spotlight)
    if (Config::get().value<bool>("/spotlight/recent", true)) {
        std::string q = text;
        for (auto& c : q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        int shown = 0;
        for (const auto& f : recentFiles()) {
            if (q.empty() ? shown >= 3 : shown >= 4) break;
            if (!q.empty()) {
                std::string name = fs::path(f.path).filename().string();
                for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (q.size() < 3 || name.find(q) == std::string::npos) continue;
            }
            out.push_back(recentRow(f.path, q.empty() ? 1000.0 - shown : 150.0 - shown));
            ++shown;
        }
    }
    for (const auto& app : Apps::get().all()) {
        double s = text.empty() ? 1 : std::max({fuzzyScore(text, app.name), fuzzyScore(text, app.generic) * 0.7,
                                                fuzzyScore(text, app.keywords) * 0.6, fuzzyScore(text, app.id) * 0.5});
        if (s <= 0) continue;
        s += std::min(20.0, std::log2(1.0 + Apps::get().launches(app.id)) * 4);
        out.push_back({app.name, !app.comment.empty() ? app.comment : app.generic, app.icon, "apps", "", s,
                       [app] { Apps::get().launch(app); }});
    }
    // Apps' shortcuts ("New private window"): under their app as it's typed,
    // or found by their own name, once there's enough to go on
    if (text.size() >= 3)
        for (const auto& app : Apps::get().all())
            for (const auto& a : app.actions) {
                const double s = std::max(fuzzyScore(text, app.name) * 0.8, fuzzyScore(text, a.name) * 0.9);
                if (s <= 0.3) continue;
                out.push_back({a.name, app.name, !a.icon.empty() ? a.icon : app.icon, "bolt", "", s,
                               [app, a] { Apps::get().launch(app, a); }});
            }
    // Answers (Settings → Spotlight): a timer, maths, and unit conversions
    const auto& cfg = Config::get();
    {
        int64_t ms = 0;
        std::string label;
        if (Alarms::parseTimerQuery(text, ms, label)) {
            // "10 min", "1 h 30 min", "45 s"
            const int64_t secs = ms / 1000;
            std::string length;
            if (secs >= 3600) length += std::format("{} h", secs / 3600);
            if (secs % 3600 >= 60) length += std::format("{}{} min", length.empty() ? "" : " ", (secs % 3600) / 60);
            if (secs % 60) length += std::format("{}{} s", length.empty() ? "" : " ", secs % 60);
            out.push_back({label.empty() ? std::format("Start a {} timer", length) : std::format("Remind me in {}", length),
                           label.empty() ? std::string("Enter starts it · Alarms & timers") : "“" + label + "” · Enter starts it", "", "timer", "", 230,
                           [ms, label, length] {
                               Alarms::get().startTimer(ms, label);
                               run({"notify-send", "-a", "Clock", "-i", "alarm-symbolic", label.empty() ? std::string("Timer started") : "Reminder set",
                                    label.empty() ? length : label + " in " + length},
                                   nullptr);
                           }});
        }
    }
    if (cfg.value<bool>("/spotlight/calculator", true) && looksLikeMath(text)) addCalc(out, text, 200);
    if (cfg.value<bool>("/spotlight/units", true)) addConversion(out, text, 210);
    // Money: the ECB's rates of the day, fetched the first time one's asked
    if (cfg.value<bool>("/spotlight/currency", true) && currency::looksLike(text)) {
        auto& money = currency::Service::get();
        if (const auto* rates = money.rates()) {
            if (auto c = currency::convert(text, *rates, money.local()))
                out.push_back({c->text, std::format("{} {} in {} · rates of {} (European Central Bank) · Enter copies", formatNumber(c->value),
                                                    c->from, c->to, rates->date),
                               "", "currency_exchange", "", 205, [t = c->text] { copy(t); }});
        } else {
            out.push_back({"Getting exchange rates…", "The European Central Bank's, once a day", "", "currency_exchange", "", 205, {}});
        }
    }
    // Your notes that match (Notes)
    if (Config::get().value<bool>("/spotlight/notes", true))
        for (auto& n : noteResults(text)) out.push_back(std::move(n));
    std::ranges::stable_sort(out, std::greater{}, &SearchResult::score);
    if (out.size() > 50) out.resize(50);
    // Last of all: what you typed, searched for on the web
    if (!text.empty() && cfg.value<bool>("/spotlight/web", true)) {
        const std::string engine = cfg.value<std::string>("/spotlight/engine", "duckduckgo");
        std::string name = "DuckDuckGo";
        for (const auto& [id, n] : engines())
            if (id == engine) name = n;
        out.push_back({std::format("Search the web for “{}”", text), "With " + name, "", "travel_explore", "", 0,
                       [url = webSearchUrl(engine, text)] { run({"xdg-open", url}, nullptr); }});
    }
    return out;
}

namespace {

// "[[ binary data 12 KiB png 800x600 ]]" → "PNG · 800×600 · 12 KiB"
std::string imageDetails(const std::string& preview) {
    std::stringstream ss(preview);
    std::vector<std::string> words;
    for (std::string w; ss >> w;) words.push_back(w);
    // [[ binary data <n> <unit> <type> <w>x<h> ]]
    if (words.size() < 8) return "Image";
    std::string type = words[5];
    for (auto& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string size = words[6];
    if (const auto x = size.find('x'); x != std::string::npos) size.replace(x, 1, "×");
    return std::format("{} · {} · {} {}", type, size, words[3], words[4]);
}

} // namespace

std::vector<SearchResult> clipboard(const std::string& q) {
    std::vector<SearchResult> out;
    auto& cb = Clipboard::get();
    if (!cb.available()) {
        // cliphist missing: Return opens Settings → Tools, where it installs
        if (haveCommand("cliphist")) return {{"Nothing copied yet", "What you copy shows up here", "", "content_paste_off", "", 0, {}}};
        return {{"Clipboard history needs cliphist", "Return opens Settings → Tools, where it installs", "", "content_paste_off", "", 0, [] {
                     if (Packages::get().openSettings) Packages::get().openSettings("tools");
                 }}};
    }
    double rank = 1000;
    for (const auto& e : cb.entries()) {
        if (out.size() >= 80) break;
        SearchResult r;
        if (e.image) {
            r.title = "Image";
            r.subtitle = imageDetails(e.preview);
            r.glyph = "image";
        } else if (!e.file.empty()) {
            r.title = fs::path(e.file).filename().string();
            r.subtitle = e.video ? "Video file" : "Picture file";
            r.glyph = e.video ? "movie" : "image";
        } else {
            r.title = e.preview;
            r.subtitle = "Enter copies it again";
            r.glyph = "content_paste";
        }
        r.image = e.thumb;
        r.video = e.video;
        // Pictures match "image", "picture", "video"… as well as their name
        const std::string kind = e.image ? "image picture screenshot" : e.video ? "video" : !e.file.empty() ? "image picture" : "";
        r.score = q.empty() ? rank-- : std::max(fuzzyScore(q, r.title), fuzzyScore(q, kind) * 0.8);
        if (r.score <= 0) continue;
        const Clipboard::Entry entry = e;
        r.activate = [entry] { Clipboard::get().copy(entry); };
        out.push_back(std::move(r));
    }
    if (out.empty() && q.empty()) out.push_back({"Nothing copied yet", "Copied text and pictures show up here", "", "content_paste", "", 0, {}});
    std::ranges::stable_sort(out, std::greater{}, &SearchResult::score);
    return out;
}

namespace {
// "define <word>" (one word, or a short phrase): the word, else ""
std::string definitionWord(const std::string& text) {
    std::string t = text;
    for (auto& ch : t) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (!t.starts_with("define ")) return {};
    std::string w = trimmed(t.substr(7));
    return w.size() >= 2 && w.size() <= 40 ? w : "";
}
} // namespace


bool asyncWord(const std::string& text) {
    return Config::get().value<bool>("/spotlight/define", true) && !definitionWord(trimmed(text)).empty();
}


// Notes (material3ui-notes): the ones whose title or text match, a click
// opens one
std::vector<SearchResult> noteResults(const std::string& q) {
    std::vector<SearchResult> out;
    if (q.size() < 2) return out;
    const char* x = std::getenv("XDG_DATA_HOME");
    const std::string file = (x && *x ? std::string(x) : paths::home() + "/.local/share") + "/material3ui/notes/notes.json";
    static std::time_t loadedAt = 0;
    static nlohmann::json notes;
    std::error_code ec;
    const auto mtime = fs::exists(file, ec) ? fs::last_write_time(file, ec).time_since_epoch().count() : 0;
    if (mtime != loadedAt) {
        loadedAt = static_cast<std::time_t>(mtime);
        std::ifstream in(file);
        try {
            notes = in ? nlohmann::json::parse(in).value("notes", nlohmann::json::array()) : nlohmann::json::array();
        } catch (...) {
            notes = nlohmann::json::array();
        }
    }
    std::string lq = q;
    for (auto& c : lq) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& n : notes) {
        if (n.value("trashed", std::time_t{0})) continue;
        std::string title = n.value("title", ""), body = n.value("body", "");
        for (const auto& i : n.value("items", nlohmann::json::array())) body += (body.empty() ? "" : " · ") + i.value("text", "");
        std::string hay = title + "\n" + body;
        for (auto& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const size_t at = hay.find(lq);
        if (at == std::string::npos) continue;
        std::string snippet = body.substr(0, 90);
        std::ranges::replace(snippet, '\n', ' ');
        const std::string id = n.value("id", "");
        out.push_back({title.empty() ? snippet : title, "Note" + (title.empty() ? std::string() : "  ·  " + snippet), "", "sticky_note_2", "",
                       at < title.size() ? 40.0 : 25.0, [id] { spawnArgs({ownCommand("material3ui-notes").empty() ? "material3ui-notes" : ownCommand("material3ui-notes"), "note:" + id}); }});
        if (out.size() >= 4) break;
    }
    return out;
}

// "?…": inside your documents. Text files through grep; PDFs through their
// text, extracted once (pdftotext) and kept in ~/.cache/material3ui/pdf-text
std::string pdfTextDir() {
    const char* x = std::getenv("XDG_CACHE_HOME");
    return (x && *x ? std::string(x) : paths::home() + "/.cache") + "/material3ui/pdf-text";
}
std::vector<std::string> contentDirs() {
    std::vector<std::string> out;
    const std::string home = paths::home();
    for (const auto& [var, d] : std::initializer_list<std::pair<const char*, const char*>>{{"XDG_DOCUMENTS_DIR", "Documents"}, {"XDG_DESKTOP_DIR", "Desktop"}, {"XDG_DOWNLOAD_DIR", "Downloads"}}) {
        const char* v = std::getenv(var);
        const std::string dir = v && *v ? v : home + "/" + d;
        std::error_code ec;
        if (fs::is_directory(dir, ec)) out.push_back(dir);
    }
    return out;
}
// The PDFs without their text yet: extracted in the background, a few at a time
void indexPdfs() {
    static bool running = false;
    if (running || !haveCommand("pdftotext")) return;
    std::error_code ec;
    fs::create_directories(pdfTextDir(), ec);
    auto todo = std::make_shared<std::vector<std::pair<std::string, std::string>>>();
    for (const auto& dir : contentDirs())
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it.depth() > 4) it.disable_recursion_pending();
            std::string e = it->path().extension().string();
            for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (e != ".pdf" || !it->is_regular_file(ec) || it->file_size(ec) > 80'000'000) continue;
            const std::string key = std::to_string(std::hash<std::string>{}(it->path().string() + std::to_string(it->last_write_time(ec).time_since_epoch().count())));
            const std::string out = pdfTextDir() + "/" + key + ".txt";
            if (!fs::exists(out, ec)) todo->push_back({it->path().string(), out});
            if (todo->size() >= 300) break;
        }
    if (todo->empty()) return;
    running = true;
    auto next = std::make_shared<std::function<void()>>();
    *next = [todo, next] {
        if (todo->empty()) {
            running = false;
            return;
        }
        auto [pdf, out] = todo->back();
        todo->pop_back();
        // The first line names the PDF, for search to give it back
        run({"sh", "-c", "printf '%s\\n' \"$1\" > \"$2\" && pdftotext -l 60 -q \"$1\" - >> \"$2\" || rm -f \"$2\"", "sh", pdf, out}, [next](int, const std::string&) { (*next)(); });
    };
    (*next)();
}

void queryAsync(const std::string& raw, std::function<void(std::vector<SearchResult>)> done) {
    const std::string text = trimmed(raw);
    // A word's meaning (dictionaryapi.dev, free, no key): each sense, an example under it
    if (const std::string word = asyncWord(text) ? definitionWord(text) : ""; !word.empty()) {
        static std::map<std::string, std::vector<SearchResult>> cache;
        if (auto it = cache.find(word); it != cache.end()) return done(it->second);
        run({"curl", "-fsS", "--max-time", "8", "https://api.dictionaryapi.dev/api/v2/entries/en/" + urlEncoded(word)},
            [word, done](int code, const std::string& out) {
                std::vector<SearchResult> rows;
                try {
                    if (code != 0) throw std::runtime_error("none");
                    const auto j = nlohmann::json::parse(out);
                    const auto& e = j.at(0);
                    const std::string phonetic = e.value("phonetic", "");
                    for (const auto& m : e.value("meanings", nlohmann::json::array())) {
                        const std::string part = m.value("partOfSpeech", "");
                        for (const auto& d : m.value("definitions", nlohmann::json::array())) {
                            const std::string def = d.value("definition", "");
                            if (def.empty()) continue;
                            std::string sub = word + (phonetic.empty() ? "" : "  " + phonetic) + (part.empty() ? "" : " · " + part);
                            if (const std::string ex = d.value("example", ""); !ex.empty()) sub += " · “" + ex + "”";
                            rows.push_back({def, sub, "", "menu_book", "", 100.0 - static_cast<double>(rows.size()), [def] { copy(def); }, {}, false, true});
                            if (rows.size() >= 6) break;
                        }
                        if (rows.size() >= 6) break;
                    }
                } catch (...) {
                    rows.clear();
                }
                if (rows.empty())
                    rows.push_back({std::format("No definition for “{}”", word), code == 0 ? "Not in the dictionary" : "The dictionary couldn't be reached", "",
                                    "menu_book", "", 0, {}});
                else
                    cache[word] = rows;
                done(rows);
            });
        return;
    }
    if (text.starts_with('#')) {
        const std::string q = trimmed(text.substr(1));
        Clipboard::get().refresh([q, done] { done(clipboard(q)); });
        return;
    }
    if (text.starts_with('?')) {
        const std::string q = trimmed(text.substr(1));
        if (q.size() < 3) return done({{"Search inside your documents", "Type at least three characters: text files, PDFs and your notes", "", "manage_search", "", 0, {}}});
        indexPdfs();
        std::vector<std::string> cmd = {"grep", "-rIil", "-m1", "--exclude-dir=.git", "--exclude-dir=node_modules"};
        for (const char* ext : {"txt", "md", "org", "rst", "tex", "csv", "json", "yaml", "yml", "toml", "ini", "conf", "log", "html", "xml", "srt", "py", "sh", "c", "cpp", "h", "js", "ts", "rs", "go", "lua"})
            cmd.push_back(std::string("--include=*.") + ext);
        cmd.push_back("--");
        cmd.push_back(q);
        for (const auto& d : contentDirs()) cmd.push_back(d);
        run(cmd, [done, q](int, const std::string& output) {
            std::vector<SearchResult> out = noteResults(q);
            std::stringstream ss(output);
            for (std::string path; std::getline(ss, path) && out.size() < 30;) {
                std::string shown = path.starts_with(paths::home()) ? "~" + path.substr(paths::home().size()) : path;
                out.push_back({fs::path(path).filename().string(), "Text  ·  " + shown, "", "description", "", 2, [path] { run({"xdg-open", path}, nullptr); }});
            }
            // PDFs, by their extracted text
            run({"grep", "-ril", "-m1", "--", q, pdfTextDir()}, [done, out](int, const std::string& hits) mutable {
                std::stringstream hs(hits);
                for (std::string txt; std::getline(hs, txt) && out.size() < 40;) {
                    std::ifstream in(txt);
                    std::string pdf;
                    std::getline(in, pdf);
                    std::error_code ec;
                    if (pdf.empty() || !fs::exists(pdf, ec)) continue;
                    std::string shown = pdf.starts_with(paths::home()) ? "~" + pdf.substr(paths::home().size()) : pdf;
                    out.push_back({fs::path(pdf).filename().string(), "PDF  ·  " + shown, "", "picture_as_pdf", "", 2, [pdf] { run({"xdg-open", pdf}, nullptr); }});
                }
                if (out.empty()) out.push_back({"Nothing found inside your documents", "Text files and PDFs in Documents, Downloads and the desktop, and your notes", "", "search_off", "", 0, {}});
                done(out);
            });
        });
        return;
    }
    if (text.starts_with('@')) {
        const std::string q = trimmed(text.substr(1));
        if (q.size() < 2) return done({{"Search files", "Type at least two characters", "", "search", "", 0, {}}});
        run({"fd", "--max-results", "40", "--hidden", "--exclude", ".git", "--", q, paths::home()},
            [done, q](int code, const std::string& output) {
                std::vector<SearchResult> out;
                // Recent files that match first
                std::string lq = q;
                for (auto& c : lq) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                std::set<std::string> listed;
                for (const auto& f : recentFiles()) {
                    std::string name = fs::path(f.path).filename().string();
                    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (name.find(lq) == std::string::npos || listed.size() >= 5) continue;
                    listed.insert(f.path);
                    out.push_back(recentRow(f.path, 2));
                }
                if (code < 0) {
                    out.push_back({"File search needs fd", "Install the fd package", "", "search_off", "", 0, {}});
                    return done(out);
                }
                std::stringstream ss(output);
                std::string path;
                while (std::getline(ss, path)) {
                    const fs::path p(path);
                    const bool dir = path.ends_with('/');
                    if (listed.contains(path)) continue; // already above, as recent
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

// ── Recent files, by app ──

std::vector<std::string> recentPaths(size_t max) {
    std::vector<std::string> out;
    for (const auto& r : recentFiles()) {
        out.push_back(r.path);
        if (out.size() >= max) break;
    }
    return out;
}

std::vector<std::string> recentFor(const Apps::App& app, size_t max) {
    std::vector<std::string> out;
    std::vector<std::string> names = {lower(app.name), programOf(app.exec)};
    std::string id = app.id.ends_with(".desktop") ? app.id.substr(0, app.id.size() - 8) : app.id;
    names.push_back(lower(id));
    if (!app.wmClass.empty()) names.push_back(lower(app.wmClass));
    std::erase(names, std::string{});
    const std::string kinds = ";" + app.mimeTypes + ";";
    for (const auto& r : recentFiles()) {
        const bool by = std::ranges::any_of(r.apps, [&](const std::string& a) { return std::ranges::find(names, a) != names.end(); });
        const bool kind = !r.mime.empty() && kinds.find(";" + r.mime + ";") != std::string::npos;
        if (!by && !kind) continue;
        out.push_back(r.path);
        if (out.size() >= max) break;
    }
    return out;
}

namespace {
// The MIME type the recent list records, from the name (the common kinds)
std::string mimeFor(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    if (!ext.empty()) ext = lower(ext.substr(1));
    static const std::pair<const char*, const char*> table[] = {
        {"txt", "text/plain"}, {"md", "text/markdown"}, {"c", "text/x-csrc"}, {"h", "text/x-chdr"}, {"cpp", "text/x-c++src"},
        {"hpp", "text/x-c++hdr"}, {"py", "text/x-python"}, {"sh", "application/x-shellscript"}, {"js", "text/javascript"},
        {"json", "application/json"}, {"html", "text/html"}, {"css", "text/css"}, {"xml", "application/xml"}, {"rs", "text/rust"},
        {"go", "text/x-go"}, {"java", "text/x-java"}, {"toml", "application/toml"}, {"yaml", "application/x-yaml"}, {"yml", "application/x-yaml"},
        {"ini", "text/plain"}, {"conf", "text/plain"}, {"log", "text/x-log"}, {"csv", "text/csv"}, {"pdf", "application/pdf"},
        {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"webp", "image/webp"}, {"gif", "image/gif"},
        {"avif", "image/avif"}, {"heic", "image/heif"}, {"jxl", "image/jxl"}, {"svg", "image/svg+xml"}, {"tif", "image/tiff"},
        {"tiff", "image/tiff"}, {"bmp", "image/bmp"}, {"mp4", "video/mp4"}, {"mkv", "video/x-matroska"}, {"webm", "video/webm"},
        {"mov", "video/quicktime"}, {"avi", "video/x-msvideo"}, {"mp3", "audio/mpeg"}, {"flac", "audio/flac"}, {"ogg", "audio/ogg"},
        {"opus", "audio/ogg"}, {"m4a", "audio/mp4"}, {"wav", "audio/x-wav"}, {"ttf", "font/ttf"}, {"otf", "font/otf"},
        {"woff", "font/woff"}, {"woff2", "font/woff2"}};
    for (const auto& [e, m] : table)
        if (ext == e) return m;
    return "application/octet-stream";
}

std::string isoNow() {
    const std::time_t t = std::time(nullptr);
    char b[40];
    std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return b;
}
} // namespace

// Recorded as GTK records it, so other apps' recent lists show it too. An
// entry already there is touched (its times, and this app among those that
// opened it); the file is written whole, then moved into place
void recordRecent(const std::string& path, const std::string& appName, const std::string& exec) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) return;
    const std::string file = recentFile();
    std::string xml;
    if (std::ifstream in(file); in) xml.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t close = xml.rfind("</xbel>");
    if (xml.find("<xbel") == std::string::npos || close == std::string::npos) {
        if (!xml.empty()) return; // not a list we understand: left alone
        xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\"\n      xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\"\n"
              "      xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\"\n>\n</xbel>\n";
    }
    const std::string now = isoNow(), uri = uriEncode(path);
    const std::string appLine = std::format("          <bookmark:application name=\"{}\" exec=\"{}\" modified=\"{}\" count=\"1\"/>\n",
                                            escapeXml(appName), escapeXml("'" + exec + " %u'"), now);
    const size_t at = xml.find("href=\"" + escapeXml(uri) + "\"");
    if (at != std::string::npos) {
        // There: its times now, and this app in its list
        const size_t tagStart = xml.rfind("<bookmark ", at), tagEnd = xml.find('>', at);
        const size_t end = xml.find("</bookmark>", at);
        if (tagStart == std::string::npos || tagEnd == std::string::npos || end == std::string::npos) return;
        std::string tag = xml.substr(tagStart, tagEnd - tagStart);
        static const std::regex times(R"re((modified|visited)="[^"]*")re");
        tag = std::regex_replace(tag, times, "$1=\"" + now + "\"");
        std::string block = xml.substr(tagEnd, end - tagEnd);
        if (block.find("name=\"" + escapeXml(appName) + "\"") == std::string::npos) {
            const size_t apps = block.find("</bookmark:applications>");
            if (apps != std::string::npos) block.insert(apps, appLine);
        }
        xml = xml.substr(0, tagStart) + tag + block + xml.substr(end);
    } else {
        const std::string entry = std::format(
            "  <bookmark href=\"{}\" added=\"{}\" modified=\"{}\" visited=\"{}\">\n    <info>\n      <metadata owner=\"http://freedesktop.org\">\n"
            "        <mime:mime-type type=\"{}\"/>\n        <bookmark:applications>\n{}        </bookmark:applications>\n      </metadata>\n    </info>\n  </bookmark>\n",
            escapeXml(uri), now, now, now, mimeFor(path), appLine);
        xml.insert(xml.rfind("</xbel>"), entry);
    }
    fs::create_directories(fs::path(file).parent_path(), ec);
    const std::string tmp = file + ".m3tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << xml;
        if (!out) return;
    }
    fs::rename(tmp, file, ec);
}


} // namespace search

void Apps::launchWith(const App& app, const std::string& file) {
    std::string quoted = "'";
    for (char c : file) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
    App as = app;
    as.exec = app.exec + " " + quoted;
    launch(as);
}



} // namespace m3
