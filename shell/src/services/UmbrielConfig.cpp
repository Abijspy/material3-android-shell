#include "services/UmbrielConfig.h"

#include "core/FileWatcher.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace m3 {

namespace fs = std::filesystem;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// An include's path: ~ and $VAR (or ${VAR}) filled in, from config.toml's folder
std::string expand(std::string p, const std::string& dir) {
    if (p.starts_with("~/")) p = paths::home() + p.substr(1);
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] != '$') {
            out += p[i];
            continue;
        }
        size_t j = i + 1;
        std::string name;
        if (j < p.size() && p[j] == '{') {
            const auto close = p.find('}', j);
            if (close == std::string::npos) break;
            name = p.substr(j + 1, close - j - 1);
            i = close;
        } else {
            while (j < p.size() && (std::isalnum(static_cast<unsigned char>(p[j])) || p[j] == '_')) name += p[j++];
            i = j - 1;
        }
        if (const char* v = std::getenv(name.c_str())) out += v;
    }
    return fs::path(out).is_absolute() ? out : (fs::path(dir) / out).string();
}

const nlohmann::json* at(const nlohmann::json& doc, const tomledit::Path& path) {
    const nlohmann::json* n = &doc;
    for (const auto& k : path) {
        if (!n->is_object()) return nullptr;
        const auto it = n->find(k);
        if (it == n->end()) return nullptr;
        n = &*it;
    }
    return n;
}

void merge(nlohmann::json& into, const nlohmann::json& from) {
    for (const auto& [k, v] : from.items()) {
        if (v.is_object() && into.contains(k) && into[k].is_object()) merge(into[k], v);
        else into[k] = v;
    }
}

} // namespace

UmbrielConfig& UmbrielConfig::get() {
    static UmbrielConfig instance;
    return instance;
}

UmbrielConfig::UmbrielConfig() {
    load();
    loadSchema();
    validate();
    // Edited elsewhere (an editor, Umbriel's own tools): read and checked again
    FileWatcher::get().watch(file(), [this] {
        load();
        validate();
        changed.emit();
    });
}

std::string UmbrielConfig::dir() const {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.config") + "/umbriel";
}

std::string UmbrielConfig::file() const {
    return dir() + "/config.toml";
}

bool UmbrielConfig::exists() const {
    std::error_code ec;
    return fs::exists(file(), ec);
}

std::string UmbrielConfig::packaged() const {
    std::vector<std::string> dirs;
    const char* data = std::getenv("XDG_DATA_DIRS");
    std::stringstream ss(data && *data ? data : "/usr/local/share:/usr/share");
    for (std::string d; std::getline(ss, d, ':');)
        if (!d.empty()) dirs.push_back(d);
    for (const auto& d : dirs) {
        const std::string f = d + "/umbriel/config.toml";
        std::error_code ec;
        if (fs::exists(f, ec)) return f;
    }
    return {};
}

void UmbrielConfig::load() {
    m_docs.clear();
    m_readError.clear();
    // Umbriel's own reads its packaged config when there's none of yours
    const std::string path = exists() ? file() : packaged();
    m_text = exists() ? readFile(file()) : std::string();
    const std::string text = path.empty() ? std::string() : readFile(path);
    std::string error;
    const auto main = tomledit::parse(text, &error);
    if (!error.empty()) {
        m_readError = error;
        return;
    }
    // Its includes first, in their order (the optional ones after), then it
    for (const auto* list : {at(main, {"include", "files"}), at(main, {"include", "optional", "files"})}) {
        if (!list || !list->is_array()) continue;
        for (const auto& f : *list) {
            if (!f.is_string()) continue;
            const std::string inc = expand(f.get<std::string>(), dir());
            std::error_code ec;
            if (!fs::exists(inc, ec)) continue;
            std::string why;
            auto data = tomledit::parse(readFile(inc), &why);
            if (why.empty()) m_docs.push_back({fs::path(inc).filename().string(), std::move(data)});
        }
    }
    m_docs.push_back({fs::path(path.empty() ? file() : path).filename().string(), main});
}

nlohmann::json UmbrielConfig::value(const tomledit::Path& path) const {
    // Tables merge across the files; anything else is the last file's
    nlohmann::json out;
    for (const auto& d : m_docs) {
        const auto* v = at(d.data, path);
        if (!v) continue;
        if (v->is_object() && out.is_object()) merge(out, *v);
        else out = *v;
    }
    return out;
}

std::string UmbrielConfig::from(const tomledit::Path& path) const {
    for (auto it = m_docs.rbegin(); it != m_docs.rend(); ++it)
        if (at(it->data, path)) return it->name;
    return {};
}

bool UmbrielConfig::write(const std::string& text) {
    std::error_code ec;
    fs::create_directories(dir(), ec);
    // Once a session, the file as it was
    if (!m_backedUp && exists()) {
        const std::string backups = paths::configDir() + "/backups";
        fs::create_directories(backups, ec);
        const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        fs::copy_file(file(), std::format("{}/umbriel-config-{}.toml", backups, stamp), fs::copy_options::overwrite_existing, ec);
        m_backedUp = !ec;
    }
    const std::string tmp = file() + ".m3tmp";
    {
        std::ofstream out(tmp);
        out << text;
        if (!out) {
            m_lastError = "couldn't write " + tmp;
            return false;
        }
    }
    fs::rename(tmp, file(), ec);
    if (ec) {
        m_lastError = "couldn't replace config.toml: " + ec.message();
        return false;
    }
    m_text = text;
    load();
    validate();
    changed.emit();
    return true;
}

namespace {

std::string dottedOf(const tomledit::Path& path) {
    std::string out;
    for (const auto& k : path) out += (out.empty() ? "" : ".") + k;
    return out;
}

// What `umbriel config validate --json` printed: each diagnostic (its log
// lines may come first; the report is the line that's JSON)
nlohmann::json diagnosticsOf(const std::string& out) {
    std::string report;
    std::istringstream lines(out);
    for (std::string line; std::getline(lines, line);)
        if (line.starts_with("{")) report = line;
    try {
        return nlohmann::json::parse(report).value("diagnostics", nlohmann::json::array());
    } catch (const std::exception&) {
        return nlohmann::json::array();
    }
}

} // namespace

bool UmbrielConfig::set(const tomledit::Path& path, const nlohmann::json& value) {
    return change([path, value, this](const std::string& text) -> std::optional<std::string> {
        return tomledit::set(text, path, value, &m_lastError);
    }, dottedOf(path));
}

bool UmbrielConfig::unset(const tomledit::Path& path) {
    if (!exists()) return true;
    return change([path, this](const std::string& text) -> std::optional<std::string> {
        return tomledit::remove(text, path, &m_lastError);
    }, dottedOf(path));
}

std::vector<UmbrielConfig::Table> UmbrielConfig::tables(const std::string& name) const {
    std::vector<Table> out;
    for (size_t d = 0; d < m_docs.size(); ++d) {
        const auto it = m_docs[d].data.find(name);
        if (it == m_docs[d].data.end() || !it->is_array()) continue;
        const bool mine = d + 1 == m_docs.size() && exists();
        for (size_t i = 0; i < it->size(); ++i) out.push_back({m_docs[d].name, (*it)[i], mine ? static_cast<int>(i) : -1});
    }
    return out;
}

nlohmann::json UmbrielConfig::own(const tomledit::Path& path) const {
    if (m_docs.empty() || !exists()) return nullptr;
    const auto* v = at(m_docs.back().data, path);
    return v ? *v : nlohmann::json();
}

bool UmbrielConfig::addTable(const std::string& name, const nlohmann::json& value) {
    return change([name, value, this](const std::string& text) -> std::optional<std::string> {
        return tomledit::appendTable(text, name, value, &m_lastError);
    }, name);
}

bool UmbrielConfig::removeTable(const std::string& name, int index) {
    if (index < 0) return false;
    return change([name, index, this](const std::string& text) -> std::optional<std::string> {
        return tomledit::removeTable(text, name, static_cast<size_t>(index), &m_lastError);
    }, name);
}

bool UmbrielConfig::change(std::function<std::optional<std::string>(const std::string&)> edit, std::string dotted) {
    if (!m_readError.empty()) {
        m_lastError = "config.toml can't be read: " + m_readError;
        return false;
    }
    m_queue.push_back({std::move(edit), std::move(dotted)});
    if (!m_checking) next();
    return true;
}

void UmbrielConfig::next() {
    if (m_queue.empty()) return;
    const Edit e = std::move(m_queue.front());
    m_queue.pop_front();
    m_lastError.clear();
    // None of yours yet: Umbriel's packaged one, as its guide says to start
    std::string text = m_text;
    if (!exists())
        if (const std::string p = packaged(); !p.empty()) text = readFile(p);
    const auto out = e.apply(text);
    if (!out || (*out == m_text && exists())) {
        if (!out) log::warn("umbriel config: {} not changed: {}", e.dotted, m_lastError);
        changed.emit();
        return next();
    }
    // Without Umbriel here to ask, as it is
    if (!haveCommand("umbriel")) {
        write(*out);
        return next();
    }
    // A copy beside config.toml (its includes found from the same folder),
    // which the installed Umbriel checks first
    std::error_code ec;
    fs::create_directories(dir(), ec);
    const std::string candidate = dir() + "/.config.toml.m3check";
    {
        std::ofstream f(candidate);
        f << *out;
    }
    m_checking = true;
    const std::string result = *out, dotted = e.dotted;
    run({"umbriel", "config", "validate", "--json", "-c", candidate}, [this, candidate, result, dotted](int, const std::string& report) {
        m_checking = false;
        std::error_code ec;
        fs::remove(candidate, ec);
        // Refused: a new error, or Umbriel saying it would ignore this very
        // option (what it said of the file before is no fault of this change)
        std::string refusal;
        for (const auto& d : diagnosticsOf(report)) {
            const std::string message = d.value("message", ""), severity = d.value("severity", "error");
            const bool aboutIt = !dotted.empty() && message.find(dotted) != std::string::npos;
            if ((severity == "error" || aboutIt) && !m_known.contains(message)) refusal = message;
        }
        if (!refusal.empty()) {
            m_lastError = "Umbriel " + (umbrielVersion().empty() ? std::string() : umbrielVersion() + " ") + "wouldn't take it: " + refusal;
            log::warn("umbriel config: {} not written: {}", dotted, refusal);
            changed.emit();
        } else {
            write(result);
        }
        next();
    });
}

void UmbrielConfig::loadSchema() {
    if (!haveCommand("umbriel")) return;
    // Quick (the binary's own table), and once: which options this Umbriel has
    std::string out;
    if (FILE* p = popen("umbriel config schema --json 2>/dev/null", "r")) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        pclose(p);
    }
    try {
        const auto j = nlohmann::json::parse(out);
        m_version = j.value("version", "");
        for (const auto& o : j.value("options", nlohmann::json::array()))
            if (o.contains("path") && o["path"].is_string()) m_schema[o["path"].get<std::string>()] = o;
    } catch (const std::exception&) {
        m_schema.clear();
    }
}

const nlohmann::json* UmbrielConfig::schema(const std::string& dotted) const {
    const auto it = m_schema.find(dotted);
    return it == m_schema.end() ? nullptr : &it->second;
}

void UmbrielConfig::validate() {
    if (!haveCommand("umbriel")) {
        m_problems.clear();
        return;
    }
    run({"umbriel", "config", "validate", "--json", "-c", file()}, [this](int, const std::string& out) {
        std::vector<std::string> problems;
        m_known.clear();
        for (const auto& d : diagnosticsOf(out)) {
            m_known.insert(d.value("message", ""));
            std::string where = d.contains("line") && d["line"].is_number() ? std::format("line {}: ", d["line"].get<int>()) : "";
            const std::string file = d.contains("file") && d["file"].is_string() ? fs::path(d["file"].get<std::string>()).filename().string() : "";
            if (!file.empty() && file != "config.toml") where = file + ", " + where;
            problems.push_back(std::format("{}: {}{}", d.value("severity", "error"), where, d.value("message", "")));
        }
        if (problems == m_problems) return;
        m_problems = std::move(problems);
        changed.emit();
    });
}

} // namespace m3
