#include "services/Kdl.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>

namespace m3::kdl {

namespace {

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string error;

    bool done() const { return i >= s.size(); }
    char peek(size_t ahead = 0) const { return i + ahead < s.size() ? s[i + ahead] : '\0'; }
    bool starts(std::string_view w) const { return s.substr(i).starts_with(w); }
    void fail(const std::string& what) {
        if (!error.empty()) return;
        size_t line = 1;
        for (size_t k = 0; k < i && k < s.size(); ++k) line += s[k] == '\n';
        error = std::format("line {}: {}", line, what);
        i = s.size();
    }

    void blockComment() {
        // Nested, as KDL allows
        i += 2;
        int depth = 1;
        while (!done() && depth > 0) {
            if (starts("/*")) {
                depth++;
                i += 2;
            } else if (starts("*/")) {
                depth--;
                i += 2;
            } else {
                i++;
            }
        }
    }
    // Spaces and comments inside a line (and escaped line breaks)
    void inlineSpace() {
        while (!done()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r') i++;
            else if (starts("/*")) blockComment();
            else if (c == '\\') {
                // A line continuation: \ then the end of the line
                i++;
                while (!done() && (peek() == ' ' || peek() == '\t')) i++;
                if (starts("//")) while (!done() && peek() != '\n') i++;
                if (peek() == '\n') i++;
            } else break;
        }
    }
    // Everything between nodes: line breaks, semicolons and line comments too
    void space() {
        while (!done()) {
            inlineSpace();
            const char c = peek();
            if (c == '\n' || c == ';') i++;
            else if (starts("//")) while (!done() && peek() != '\n') i++;
            else break;
        }
    }

    static bool identChar(char c) {
        return !std::isspace(static_cast<unsigned char>(c)) && c != '{' && c != '}' && c != '(' && c != ')' && c != '[' && c != ']' &&
               c != '=' && c != '"' && c != ';' && c != '\\' && c != '/' && c != ',' && c != '\0';
    }

    std::string quoted() {
        i++; // the opening quote
        std::string out;
        while (!done() && peek() != '"') {
            char c = s[i++];
            if (c != '\\') {
                out += c;
                continue;
            }
            const char e = done() ? '\0' : s[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                // \u{hex}: written out as UTF-8
                if (peek() != '{') break;
                const size_t close = s.find('}', i);
                if (close == std::string_view::npos) break;
                const unsigned long cp = std::strtoul(std::string(s.substr(i + 1, close - i - 1)).c_str(), nullptr, 16);
                i = close + 1;
                if (cp < 0x80) out += static_cast<char>(cp);
                else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += e; // \" \\ \/
            }
        }
        if (done()) fail("a string isn't closed");
        else i++;
        return out;
    }
    // r"…", r#"…"# (version 1) and #"…"# (version 2)
    bool rawAhead() const {
        size_t k = i + (peek() == 'r' ? 1 : 0);
        if (k == i && peek() != '#') return false;
        while (k < s.size() && s[k] == '#') k++;
        return k < s.size() && s[k] == '"' && (k > i + 1 || peek() == 'r');
    }
    std::string raw() {
        if (peek() == 'r') i++;
        size_t hashes = 0;
        while (peek() == '#') hashes++, i++;
        i++; // the quote
        const std::string close = "\"" + std::string(hashes, '#');
        const size_t end = s.find(close, i);
        if (end == std::string_view::npos) {
            fail("a raw string isn't closed");
            return {};
        }
        std::string out(s.substr(i, end - i));
        i = end + close.size();
        return out;
    }
    std::string bare() {
        const size_t start = i;
        while (!done() && identChar(peek())) i++;
        return std::string(s.substr(start, i - start));
    }
    // A (type) annotation: read past
    void annotation() {
        if (peek() != '(') return;
        const size_t close = s.find(')', i);
        i = close == std::string_view::npos ? s.size() : close + 1;
    }

    Value value() {
        annotation();
        Value v;
        if (peek() == '"') {
            v.type = Value::Type::String;
            v.text = quoted();
            return v;
        }
        if (rawAhead()) {
            v.type = Value::Type::String;
            v.text = raw();
            return v;
        }
        const std::string w = bare();
        if (w.empty()) {
            fail(std::format("unexpected '{}'", peek()));
            return v;
        }
        if (w == "true" || w == "#true" || w == "false" || w == "#false") {
            v.type = Value::Type::Bool;
            v.boolean = w.find("true") != std::string::npos;
            v.text = w;
            return v;
        }
        if (w == "null" || w == "#null") return v;
        // A number (signs, decimals, exponents, 0x/0o/0b, underscores), else
        // version 2's bare string
        std::string digits;
        for (char c : w)
            if (c != '_') digits += c;
        const char first = digits[0] == '+' || digits[0] == '-' ? (digits.size() > 1 ? digits[1] : '\0') : digits[0];
        if (std::isdigit(static_cast<unsigned char>(first))) {
            const bool neg = digits[0] == '-';
            std::string_view body = digits;
            if (digits[0] == '+' || digits[0] == '-') body.remove_prefix(1);
            int base = 10;
            if (body.starts_with("0x")) base = 16;
            else if (body.starts_with("0o")) base = 8;
            else if (body.starts_with("0b")) base = 2;
            v.type = Value::Type::Number;
            v.text = w;
            if (base != 10) {
                v.number = static_cast<double>(std::strtoll(std::string(body.substr(2)).c_str(), nullptr, base));
                if (neg) v.number = -v.number;
            } else {
                v.number = std::strtod(digits.c_str(), nullptr);
            }
            return v;
        }
        v.type = Value::Type::String;
        v.text = w;
        return v;
    }

    // A block's nodes, up to its closing brace (or the end, at the top)
    std::vector<Node> nodes(bool inner) {
        std::vector<Node> out;
        while (true) {
            space();
            if (done()) {
                if (inner) fail("a block isn't closed");
                break;
            }
            if (peek() == '}') {
                if (!inner) fail("a closing brace without its block");
                else i++;
                break;
            }
            bool skip = false;
            if (starts("/-")) {
                skip = true;
                i += 2;
                inlineSpace();
            }
            Node n = node();
            if (!error.empty()) break;
            if (!skip) out.push_back(std::move(n));
        }
        return out;
    }

    Node node() {
        Node n;
        annotation();
        if (peek() == '"') n.name = quoted();
        else if (rawAhead()) n.name = raw();
        else n.name = bare();
        if (n.name.empty()) {
            fail(std::format("unexpected '{}'", peek()));
            return n;
        }
        while (!done()) {
            inlineSpace();
            const char c = peek();
            if (c == '\n' || c == ';' || c == '}' || c == '\0' || starts("//")) break;
            bool skip = false;
            if (starts("/-")) {
                skip = true;
                i += 2;
                inlineSpace();
            }
            if (peek() == '{') {
                i++;
                auto kids = nodes(true);
                if (!skip) n.children = std::move(kids);
                continue;
            }
            // A property (key=value), else an argument
            const size_t at = i;
            std::string key;
            if (peek() == '"') key = quoted();
            else if (!rawAhead() && peek() != '(') key = bare();
            if (!key.empty() && peek() == '=') {
                i++;
                Value v = value();
                if (!skip) n.props.emplace_back(std::move(key), std::move(v));
                continue;
            }
            i = at;
            Value v = value();
            if (!error.empty()) break;
            if (!skip) n.args.push_back(std::move(v));
        }
        return n;
    }
};

// Every node at `path` in document order, through every section named so
template <typename F>
void walk(const std::vector<Node>& nodes, const std::string_view* path, size_t depth, F&& f) {
    for (const auto& n : nodes) {
        if (n.name != *path) continue;
        if (depth == 1) f(n);
        else walk(n.children, path + 1, depth - 1, f);
    }
}

} // namespace

const Value* Node::prop(std::string_view key) const {
    const Value* found = nullptr;
    for (const auto& [k, v] : props)
        if (k == key) found = &v; // the last one, as KDL says
    return found;
}

std::vector<Node> parse(std::string_view text, std::string* error) {
    Parser p;
    p.s = text;
    // A byte-order mark
    if (text.starts_with("\xEF\xBB\xBF")) p.i = 3;
    auto out = p.nodes(false);
    if (error) *error = p.error;
    return out;
}

const Node* last(const std::vector<Node>& doc, std::initializer_list<std::string_view> path) {
    if (path.size() == 0) return nullptr;
    const Node* found = nullptr;
    walk(doc, path.begin(), path.size(), [&](const Node& n) { found = &n; });
    return found;
}

std::optional<double> number(const std::vector<Node>& doc, std::initializer_list<std::string_view> path) {
    const Node* n = last(doc, path);
    if (!n || n->args.empty() || n->args[0].type != Value::Type::Number) return std::nullopt;
    return n->args[0].number;
}

std::optional<std::string> string(const std::vector<Node>& doc, std::initializer_list<std::string_view> path) {
    const Node* n = last(doc, path);
    if (!n || n->args.empty() || n->args[0].type != Value::Type::String) return std::nullopt;
    return n->args[0].text;
}

std::optional<bool> flag(const std::vector<Node>& doc, std::initializer_list<std::string_view> path) {
    const Node* n = last(doc, path);
    if (!n) return std::nullopt;
    if (!n->args.empty() && n->args[0].type == Value::Type::Bool) return n->args[0].boolean;
    return true;
}

std::optional<bool> onOff(const std::vector<Node>& doc, std::initializer_list<std::string_view> path) {
    std::optional<bool> out;
    if (path.size() == 0) return out;
    walk(doc, path.begin(), path.size(), [&](const Node& section) {
        for (const auto& c : section.children) {
            if (c.name == "on") out = true;
            else if (c.name == "off") out = false;
        }
    });
    return out;
}

} // namespace m3::kdl
