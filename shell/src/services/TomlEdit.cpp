#include "services/TomlEdit.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <sstream>
#include <toml++/toml.hpp>

namespace m3::tomledit {

namespace {

nlohmann::json toJson(const toml::node& n) {
    if (const auto* t = n.as_table()) {
        auto out = nlohmann::json::object();
        for (auto&& [k, v] : *t) out[std::string(k.str())] = toJson(v);
        return out;
    }
    if (const auto* a = n.as_array()) {
        auto out = nlohmann::json::array();
        for (auto&& v : *a) out.push_back(toJson(v));
        return out;
    }
    if (const auto* s = n.as_string()) return s->get();
    if (const auto* i = n.as_integer()) return i->get();
    if (const auto* f = n.as_floating_point()) return f->get();
    if (const auto* b = n.as_boolean()) return b->get();
    std::ostringstream os; // dates and times, as written
    if (const auto* d = n.as_date()) os << d->get();
    else if (const auto* t = n.as_time()) os << t->get();
    else if (const auto* dt = n.as_date_time()) os << dt->get();
    return os.str();
}

std::optional<toml::table> parseTable(const std::string& text, std::string* error) {
    try {
        return toml::parse(text);
    } catch (const toml::parse_error& e) {
        if (error) *error = std::format("line {}: {}", e.source().begin.line, e.description());
        return std::nullopt;
    }
}

// Where each line starts, in bytes
struct Lines {
    explicit Lines(const std::string& text) : text(text) {
        starts.push_back(0);
        for (size_t i = 0; i < text.size(); ++i)
            if (text[i] == '\n') starts.push_back(i + 1);
    }
    // toml++'s line and column (1-based, the column in characters) in bytes
    size_t offset(toml::source_index line, toml::source_index column) const {
        if (line == 0 || line > starts.size()) return text.size();
        size_t at = starts[line - 1];
        for (toml::source_index c = 1; c < column && at < text.size() && text[at] != '\n'; ++c) {
            ++at;
            while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) ++at; // the rest of a UTF-8 character
        }
        return at;
    }
    std::string line(size_t index) const { // 0-based, without its newline
        if (index >= starts.size()) return {};
        const size_t end = index + 1 < starts.size() ? starts[index + 1] - 1 : text.size();
        return text.substr(starts[index], end - starts[index]);
    }
    size_t count() const { return starts.size(); }
    const std::string& text;
    std::vector<size_t> starts;
};

std::string trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r"));
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

// A [header]'s keys, its brackets, spaces and quotes taken off
std::vector<std::string> headerPath(std::string line) {
    line = trim(line);
    std::vector<std::string> out;
    if (line.size() < 2 || line[0] != '[' || line[1] == '[') return out;
    const auto close = line.find(']');
    if (close == std::string::npos) return out;
    std::string inside = line.substr(1, close - 1), part;
    bool quoted = false;
    char quote = 0;
    for (char c : inside) {
        if (quoted) {
            if (c == quote) quoted = false;
            else part += c;
        } else if (c == '"' || c == '\'') {
            quoted = true;
            quote = c;
        } else if (c == '.') {
            out.push_back(trim(part));
            part.clear();
        } else {
            part += c;
        }
    }
    out.push_back(trim(part));
    return out;
}

// Is the table at `path` made by its own [header] (rather than by dotted
// keys, a deeper header, or the root)?
bool hasHeader(const Lines& lines, const toml::table& t, const Path& path) {
    if (path.empty() || t.is_inline()) return false;
    const auto line = t.source().begin.line;
    return line > 0 && headerPath(lines.line(line - 1)) == path;
}

// Every line that starts a [table] or [[array of tables]] (0-based)
void headerLines(const Lines& lines, const toml::node& n, std::set<size_t>& out) {
    if (const auto* t = n.as_table()) {
        if (!t->is_inline() && t->source().begin.line > 0) {
            const std::string l = trim(lines.line(t->source().begin.line - 1));
            if (!l.empty() && l[0] == '[') out.insert(t->source().begin.line - 1);
        }
        for (auto&& [k, v] : *t) headerLines(lines, v, out);
    } else if (const auto* a = n.as_array()) {
        for (auto&& v : *a) headerLines(lines, v, out);
    }
}

const toml::node* find(const toml::table& root, const Path& path, size_t count) {
    const toml::node* n = &root;
    for (size_t i = 0; i < count; ++i) {
        const auto* t = n->as_table();
        if (!t) return nullptr;
        n = t->get(path[i]);
        if (!n) return nullptr;
    }
    return n;
}

std::string dotted(const Path& path, size_t from) {
    std::string out;
    for (size_t i = from; i < path.size(); ++i) out += (out.empty() ? "" : ".") + key(path[i]);
    return out;
}

// A copy of `value` (an object) with `path` set inside it (or taken out)
void setIn(nlohmann::json& value, const Path& path, size_t from, const nlohmann::json* to) {
    if (from + 1 == path.size()) {
        if (to) value[path[from]] = *to;
        else value.erase(path[from]);
        return;
    }
    auto& inner = value[path[from]];
    if (!inner.is_object()) inner = nlohmann::json::object();
    setIn(inner, path, from + 1, to);
}

std::optional<std::string> checked(std::string out, const Path& path, const nlohmann::json* want, std::string* error) {
    std::string why;
    if (!parseTable(out, &why)) {
        if (error) *error = "the edit wouldn't read back: " + why;
        return std::nullopt;
    }
    const auto now = get(out, path);
    if (want ? now != *want : !now.is_null()) {
        if (error) *error = "the edit didn't take";
        return std::nullopt;
    }
    return out;
}

} // namespace

nlohmann::json parse(const std::string& text, std::string* error) {
    const auto t = parseTable(text, error);
    return t ? toJson(*t) : nlohmann::json();
}

nlohmann::json get(const std::string& text, const Path& path) {
    const auto t = parseTable(text, nullptr);
    if (!t) return nullptr;
    const auto* n = find(*t, path, path.size());
    return n ? toJson(*n) : nlohmann::json();
}

std::string key(const std::string& name) {
    const bool bare = !name.empty() && std::ranges::all_of(name, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
    return bare ? name : literal(name);
}

std::string literal(const nlohmann::json& v) {
    if (v.is_string()) {
        std::string out = "\"";
        for (unsigned char c : v.get<std::string>()) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20) out += std::format("\\u{:04X}", c);
                else out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_float()) {
        const double d = v.get<double>();
        if (std::isnan(d)) return "nan";
        if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
        std::string s = std::format("{}", d);
        if (s.find_first_of(".eE") == std::string::npos) s += ".0"; // a float, not an integer
        return s;
    }
    if (v.is_array()) {
        std::string out = "[";
        for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + literal(v[i]);
        return out + "]";
    }
    if (v.is_object()) {
        if (v.empty()) return "{}";
        std::string out = "{ ";
        bool first = true;
        for (const auto& [k, item] : v.items()) {
            out += (first ? "" : ", ") + key(k) + " = " + literal(item);
            first = false;
        }
        return out + " }";
    }
    return "\"\"";
}

std::optional<std::string> set(const std::string& text, const Path& path, const nlohmann::json& value, std::string* error) {
    if (path.empty() || value.is_null()) {
        if (error) *error = "nothing to set";
        return std::nullopt;
    }
    const auto doc = parseTable(text, error);
    if (!doc) return std::nullopt;
    const Lines lines(text);

    // There already: the value replaced where it's written
    if (const auto* n = find(*doc, path, path.size())) {
        const auto* t = n->as_table();
        if (t && !t->is_inline()) {
            if (error) *error = "a table can't be replaced by a value";
            return std::nullopt;
        }
        const auto& r = n->source();
        const size_t from = lines.offset(r.begin.line, r.begin.column), to = lines.offset(r.end.line, r.end.column);
        return checked(text.substr(0, from) + literal(value) + text.substr(to), path, &value, error);
    }

    // Its deepest table there already
    size_t depth = 0;
    while (depth + 1 < path.size()) {
        const auto* n = find(*doc, path, depth + 1);
        if (!n) break;
        if (!n->as_table()) {
            if (error) *error = std::format("{} isn't a table", path[depth]);
            return std::nullopt;
        }
        ++depth;
    }
    const auto* deepest = find(*doc, path, depth)->as_table();
    // In an inline table: the table written again with it
    if (deepest->is_inline()) {
        auto whole = toJson(*deepest);
        setIn(whole, path, depth, &value);
        const auto& r = deepest->source();
        const size_t from = lines.offset(r.begin.line, r.begin.column), to = lines.offset(r.end.line, r.end.column);
        return checked(text.substr(0, from) + literal(whole) + text.substr(to), path, &value, error);
    }
    // The nearest table with a [header] of its own (or the top), which the
    // new line goes under
    size_t owner = depth;
    while (owner > 0) {
        Path prefix(path.begin(), path.begin() + static_cast<long>(owner));
        if (hasHeader(lines, *find(*doc, path, owner)->as_table(), prefix)) break;
        --owner;
    }
    std::set<size_t> headers;
    headerLines(lines, *doc, headers);
    // That table's lines: after its header, up to the next one
    size_t first = 0;
    if (owner > 0) first = find(*doc, path, owner)->source().begin.line; // the line after the header (0-based)
    const auto next = headers.upper_bound(owner > 0 ? first - 1 : static_cast<size_t>(-1));
    size_t end = owner == 0 ? (headers.empty() ? lines.count() : *headers.begin()) : (next == headers.end() ? lines.count() : *next);
    // After its last line with something on it
    size_t at = end;
    while (at > first && trim(lines.line(at - 1)).empty()) --at;

    // A table that isn't there yet gets a [header] of its own: after its
    // parent's lines, or at the end when its parent is the top; else a line
    // (a dotted key where the table is only dotted keys) after the table's last
    const bool newTable = depth == owner && path.size() - owner >= 2;
    std::string insert;
    size_t offset;
    if (newTable) {
        insert = "[" + dotted(Path(path.begin(), path.end() - 1), 0) + "]\n" + key(path.back()) + " = " + literal(value) + "\n";
        offset = owner == 0 ? text.size() : (at < lines.count() ? lines.starts[at] : text.size());
        const bool atEnd = offset == text.size();
        // One blank line before it: none added where the file ends in one
        if (!text.empty()) {
            if (atEnd && text.back() != '\n') insert = "\n\n" + insert;
            else if (!(atEnd && text.ends_with("\n\n"))) insert = "\n" + insert;
        }
        // A blank line before the header after it, unless one's there already
        if (!atEnd && !(at < end && trim(lines.line(at)).empty())) insert += "\n";
    } else {
        insert = dotted(path, owner) + " = " + literal(value) + "\n";
        offset = at < lines.count() ? lines.starts[at] : text.size();
        if (offset == text.size() && !text.empty() && text.back() != '\n') insert = "\n" + insert;
    }
    std::string out = text;
    out.insert(offset, insert);
    return checked(out, path, &value, error);
}

std::optional<std::string> remove(const std::string& text, const Path& path, std::string* error) {
    if (path.empty()) return text;
    const auto doc = parseTable(text, error);
    if (!doc) return std::nullopt;
    const auto* n = find(*doc, path, path.size());
    if (!n) return text;
    const auto* parent = find(*doc, path, path.size() - 1)->as_table();
    const Lines lines(text);
    // In an inline table: the table written again without it
    if (parent->is_inline()) {
        size_t depth = path.size() - 1;
        while (depth > 0 && find(*doc, path, depth - 1)->as_table()->is_inline()) --depth;
        const auto* outer = find(*doc, path, depth)->as_table();
        auto whole = toJson(*outer);
        setIn(whole, path, depth, nullptr);
        const auto& r = outer->source();
        const size_t from = lines.offset(r.begin.line, r.begin.column), to = lines.offset(r.end.line, r.end.column);
        return checked(text.substr(0, from) + literal(whole) + text.substr(to), path, nullptr, error);
    }
    if (n->as_table() && !n->as_table()->is_inline()) {
        if (error) *error = "a whole table isn't taken out this way";
        return std::nullopt;
    }
    // Its key (a dotted key's last part), and the line or lines it's on
    const toml::key* k = nullptr;
    for (auto&& [name, v] : *parent)
        if (name.str() == path.back()) k = &name;
    if (!k) return text;
    const auto keyLine = k->source().begin.line, endLine = n->source().end.line;
    // Only when nothing else is on them: before it a dotted key's start at
    // most, after it a comment at most
    const std::string before = trim(lines.line(keyLine - 1).substr(0, lines.offset(keyLine, k->source().begin.column) - lines.starts[keyLine - 1]));
    const size_t valueEnd = lines.offset(endLine, n->source().end.column) - lines.starts[endLine - 1];
    const std::string after = trim(lines.line(endLine - 1).substr(std::min(valueEnd, lines.line(endLine - 1).size())));
    if ((!before.empty() && (before.back() != '.' || before.find('=') != std::string::npos)) || (!after.empty() && after[0] != '#')) {
        if (error) *error = "it shares its line with something else";
        return std::nullopt;
    }
    // A dotted key ("blur.enabled"): its line starts at the dotted key's start
    const size_t from = lines.starts[keyLine - 1];
    const size_t to = endLine < lines.count() ? lines.starts[endLine] : text.size();
    std::string out = text.substr(0, from) + text.substr(to);
    // Its [table] left with nothing in it (no keys, no comments): the
    // header goes too, with the blank line before it
    const Path table(path.begin(), path.end() - 1);
    if (!table.empty() && parent->size() == 1 && hasHeader(lines, *parent, table)) {
        const size_t header = parent->source().begin.line - 1; // 0-based
        const Lines after(out);
        bool empty = true;
        size_t i = header + 1;
        for (; i < after.count(); ++i) {
            const std::string l = trim(after.line(i));
            if (!l.empty() && l[0] == '[') break;
            if (!l.empty()) empty = false;
        }
        if (empty) {
            const bool blankBefore = header > 0 && trim(after.line(header - 1)).empty();
            const size_t start = after.starts[blankBefore ? header - 1 : header];
            const size_t stop = i < after.count() ? after.starts[i] : out.size();
            // Its blank lines go with it; one stays before a header after it
            const bool headerAfter = i < after.count();
            out = out.substr(0, start) + (headerAfter && blankBefore ? "\n" : "") + out.substr(stop);
        }
    }
    return checked(out, path, nullptr, error);
}

namespace {

// An object's entries as lines, its objects as dotted keys; what a rule
// matches ("match") first, as it reads
void flatten(const nlohmann::json& value, const std::string& prefix, std::string& out) {
    auto one = [&](const std::string& k, const nlohmann::json& v) {
        const std::string name = prefix.empty() ? key(k) : prefix + "." + key(k);
        if (v.is_object() && !v.empty()) flatten(v, name, out);
        else out += name + " = " + literal(v) + "\n";
    };
    if (value.contains("match")) one("match", value["match"]);
    for (const auto& [k, v] : value.items())
        if (k != "match") one(k, v);
}

size_t tableCount(const toml::table& doc, const std::string& name) {
    const auto* a = doc.get(name) ? doc.get(name)->as_array() : nullptr;
    return a ? a->size() : 0;
}

} // namespace

std::optional<std::string> appendTable(const std::string& text, const std::string& name, const nlohmann::json& value, std::string* error) {
    if (!value.is_object() || value.empty()) {
        if (error) *error = "nothing to add";
        return std::nullopt;
    }
    const auto doc = parseTable(text, error);
    if (!doc) return std::nullopt;
    if (const auto* n = doc->get(name); n && !n->is_array_of_tables()) {
        if (error) *error = name + " isn't a list of tables";
        return std::nullopt;
    }
    const size_t before = tableCount(*doc, name);
    std::string block = "[[" + key(name) + "]]\n";
    flatten(value, "", block);
    std::string out = text;
    if (!out.empty() && out.back() != '\n') out += '\n';
    if (!out.empty() && !out.ends_with("\n\n")) out += '\n';
    out += block;
    std::string why;
    const auto check = parseTable(out, &why);
    if (!check || tableCount(*check, name) != before + 1) {
        if (error) *error = "the edit wouldn't read back: " + why;
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> removeTable(const std::string& text, const std::string& name, size_t index, std::string* error) {
    const auto doc = parseTable(text, error);
    if (!doc) return std::nullopt;
    const auto* n = doc->get(name);
    const auto* list = n ? n->as_array() : nullptr;
    if (!list || !n->is_array_of_tables() || index >= list->size()) {
        if (error) *error = "there's no such " + name;
        return std::nullopt;
    }
    const Lines lines(text);
    const auto* t = (*list)[index].as_table();
    const size_t header = t->source().begin.line - 1; // its [[name]] line, 0-based
    if (trim(lines.line(header)) .rfind("[[", 0) != 0) {
        if (error) *error = "it isn't written as [[" + name + "]]";
        return std::nullopt;
    }
    std::set<size_t> headers;
    headerLines(lines, *doc, headers);
    const auto next = headers.upper_bound(header);
    size_t end = next == headers.end() ? lines.count() : *next;
    // Its own lines: not the comments and blank lines that lead the next one in
    while (end > header + 1 && next != headers.end() && (trim(lines.line(end - 1)).empty() || trim(lines.line(end - 1))[0] == '#')) --end;
    // With the comment written just above it, and a blank line before that
    size_t start = header;
    while (start > 0 && !trim(lines.line(start - 1)).empty() && trim(lines.line(start - 1))[0] == '#') --start;
    const bool blankBefore = start > 0 && trim(lines.line(start - 1)).empty();
    const size_t from = lines.starts[blankBefore ? start - 1 : start];
    const size_t to = end < lines.count() ? lines.starts[end] : text.size();
    std::string out = text.substr(0, from) + text.substr(to);
    std::string why;
    const auto check = parseTable(out, &why);
    if (!check || tableCount(*check, name) != list->size() - 1) {
        if (error) *error = "the edit wouldn't read back: " + why;
        return std::nullopt;
    }
    return out;
}

} // namespace m3::tomledit
