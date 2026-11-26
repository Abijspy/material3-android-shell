// Markdown, drawn with the shell's widgets (the Text Editor's preview).
// Blocks become labels, rows and boxes; inline marks become Pango markup in
// those labels. CommonMark's common ground, not every corner of it: what
// READMEs and notes use.
#include "shell/Markdown.h"

#include "render/Images.h"
#include "ui/Controls.h"
#include "ui/Theme.h"

#include <pango/pango.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <vector>

namespace fs = std::filesystem;

namespace m3::shell {

using namespace m3::ui;

namespace {

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

bool validMarkup(const std::string& m) {
    return pango_parse_markup(m.c_str(), static_cast<int>(m.size()), 0, nullptr, nullptr, nullptr, nullptr);
}

bool wordy(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80;
}

std::string inlineRaw(const std::string& t) {
    std::string out;
    std::vector<std::string> open; // markers open, innermost last
    auto tagOf = [](const std::string& m) -> std::pair<const char*, const char*> {
        if (m == "**" || m == "__") return {"<b>", "</b>"};
        if (m == "~~") return {"<s>", "</s>"};
        return {"<i>", "</i>"};
    };
    const size_t n = t.size();
    for (size_t i = 0; i < n;) {
        const char c = t[i];
        // \* and friends: the character itself
        if (c == '\\' && i + 1 < n && std::ispunct(static_cast<unsigned char>(t[i + 1]))) {
            out += escape(std::string(1, t[i + 1]));
            i += 2;
            continue;
        }
        // `code`: as typed, in monospace
        if (c == '`') {
            size_t ticks = 0;
            while (i + ticks < n && t[i + ticks] == '`') ++ticks;
            const std::string fence(ticks, '`');
            const size_t end = t.find(fence, i + ticks);
            if (end != std::string::npos) {
                std::string code = t.substr(i + ticks, end - i - ticks);
                if (code.size() > 1 && code.front() == ' ' && code.back() == ' ') code = code.substr(1, code.size() - 2);
                out += "<tt>" + escape(code) + "</tt>";
                i = end + ticks;
                continue;
            }
            out += escape(fence);
            i += ticks;
            continue;
        }
        // ![alt](src) inline, and [text](url): the alt in italics, the text underlined
        if (c == '[' || (c == '!' && i + 1 < n && t[i + 1] == '[')) {
            const size_t start = c == '!' ? i + 2 : i + 1;
            int depth = 1;
            size_t j = start;
            for (; j < n && depth > 0; ++j) {
                if (t[j] == '\\') ++j;
                else if (t[j] == '[') ++depth;
                else if (t[j] == ']') --depth;
            }
            if (depth == 0 && j < n && t[j] == '(') {
                const size_t close = t.find(')', j);
                if (close != std::string::npos) {
                    const std::string label = t.substr(start, j - 1 - start);
                    out += c == '!' ? "<i>" + escape(label) + "</i>" : "<u>" + inlineRaw(label) + "</u>";
                    i = close + 1;
                    continue;
                }
            }
        }
        // Bare links
        if ((t.compare(i, 7, "http://") == 0 || t.compare(i, 8, "https://") == 0) && (i == 0 || !wordy(t[i - 1]))) {
            size_t j = i;
            while (j < n && !std::isspace(static_cast<unsigned char>(t[j])) && t[j] != '<' && t[j] != '>' && t[j] != ')') ++j;
            while (j > i && std::string_view(".,;:!?").find(t[j - 1]) != std::string_view::npos) --j;
            out += "<u>" + escape(t.substr(i, j - i)) + "</u>";
            i = j;
            continue;
        }
        // Emphasis: **bold**, __bold__, *italic*, _italic_, ~~struck~~
        if (c == '*' || c == '_' || (c == '~' && i + 1 < n && t[i + 1] == '~')) {
            const bool dbl = i + 1 < n && t[i + 1] == c;
            const std::string m = c == '~' ? "~~" : dbl ? std::string(2, c) : std::string(1, c);
            const bool prevWord = i > 0 && wordy(t[i - 1]);
            const bool nextWord = i + m.size() < n && wordy(t[i + m.size()]);
            const bool nextSpace = i + m.size() >= n || std::isspace(static_cast<unsigned char>(t[i + m.size()]));
            if (!open.empty() && open.back() == m && !(c == '_' && nextWord)) {
                out += tagOf(m).second;
                open.pop_back();
                i += m.size();
                continue;
            }
            const bool canOpen = !nextSpace && !(c == '_' && prevWord) && std::ranges::find(open, m) == open.end() &&
                                 t.find(m, i + m.size() + 1) != std::string::npos;
            if (canOpen) {
                out += tagOf(m).first;
                open.push_back(m);
                i += m.size();
                continue;
            }
            out += escape(m);
            i += m.size();
            continue;
        }
        out += escape(std::string(1, c));
        ++i;
    }
    while (!open.empty()) out += tagOf(open.back()).second, open.pop_back();
    return out;
}

Label* textLabel(Flex* into, const std::string& markup, float width, float size, int weight) {
    auto* l = into->add<Label>(markup);
    l->setMarkup(true)->setSize(size)->setWeight(weight)->setMaxWidth(std::max(40.0f, width))->setMaxLines(kAllLines);
    l->alignSelf = Align::Start;
    return l;
}

Flex* rule(Flex* into) {
    auto* r = into->add<Flex>(Flex::Direction::Row);
    r->setBackground("outline_variant", false);
    r->fixedHeight = 1;
    r->alignSelf = Align::Stretch;
    return r;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

std::vector<std::string> cells(std::string row) {
    row = trim(row);
    if (row.starts_with('|')) row.erase(0, 1);
    if (row.ends_with('|') && !row.ends_with("\\|")) row.pop_back();
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < row.size(); ++i) {
        if (row[i] == '\\' && i + 1 < row.size() && row[i + 1] == '|') cur += '|', ++i;
        else if (row[i] == '|') out.push_back(trim(cur)), cur.clear();
        else cur += row[i];
    }
    out.push_back(trim(cur));
    return out;
}

void picture(Flex* into, const std::string& alt, std::string src, float width, const std::string& baseDir) {
    std::error_code ec;
    const bool remote = src.find("://") != std::string::npos;
    if (!remote && !src.empty() && src[0] != '/') src = baseDir + "/" + src;
    if (remote || !fs::is_regular_file(src, ec)) {
        textLabel(into, "<i>" + escape("Picture: " + (alt.empty() ? fs::path(src).filename().string() : alt)) + "</i>", width, 13, 400)->setColor("on_surface_variant", false);
        return;
    }
    const float w = std::min(width, 560.0f), h = w * 0.6f;
    auto* v = into->add<ImageView>(w, h, [src, w, h](float scale, int& tw, int& th) -> GLuint {
        auto* t = Images::get().picture(src, static_cast<int>(w * scale), static_cast<int>(h * scale), false);
        if (!t) return 0;
        tw = t->width, th = t->height;
        return t->id;
    });
    v->radius = 10;
    v->alignSelf = Align::Start;
    if (!alt.empty()) textLabel(into, "<i>" + escape(alt) + "</i>", width, 12, 400)->setColor("on_surface_variant", false);
}

void render(Flex* into, const std::vector<std::string>& lines, float width, const std::string& baseDir, int depth);

const std::regex kHeading(R"(^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$)");
const std::regex kRule(R"(^ {0,3}([-*_])(\s*\1){2,}\s*$)");
const std::regex kItem(R"(^(\s*)([-*+]|\d{1,9}[.)])\s+(.*)$)");
const std::regex kSeparator(R"(^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$)");
const std::regex kImage(R"(^\s*!\[([^\]]*)\]\(([^)\s]+)(\s+"[^"]*")?\)\s*$)");

void render(Flex* into, const std::vector<std::string>& lines, float width, const std::string& baseDir, int depth) {
    std::string para; // the paragraph being gathered, as markup-to-be
    auto flush = [&] {
        if (trim(para).empty()) return para.clear();
        textLabel(into, markdownInline(para), width, 15, 400);
        para.clear();
    };
    std::smatch m;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const std::string t = trim(line);
        if (t.empty()) {
            flush();
            continue;
        }
        // ``` fenced code ```
        if (t.starts_with("```") || t.starts_with("~~~")) {
            flush();
            const std::string fence = t.substr(0, 3);
            std::string code;
            size_t j = i + 1;
            for (; j < lines.size() && !trim(lines[j]).starts_with(fence); ++j) code += (code.empty() && j == i + 1 ? "" : "\n") + lines[j];
            i = j;
            auto* box = into->add<Flex>(Flex::Direction::Column);
            box->setBackground("surface_container_high", false)->setRadius(Radii(12))->setPadding(Insets(12))->setAlignItems(Align::Start);
            box->alignSelf = Align::Stretch;
            auto* l = box->add<Label>(code.empty() ? " " : code);
            l->setFamily(Theme::get().monoFamily())->setSize(13)->setMaxWidth(std::max(40.0f, width - 24))->setMaxLines(kAllLines);
            continue;
        }
        // Setext headings: a paragraph underlined with === or ---
        if (!para.empty() && !t.empty() && (std::ranges::all_of(t, [](char c) { return c == '='; }) || (t.size() >= 2 && std::ranges::all_of(t, [](char c) { return c == '-'; })))) {
            const bool h1 = t[0] == '=';
            textLabel(into, markdownInline(trim(para)), width, h1 ? 26 : 21, 700);
            if (h1) rule(into);
            para.clear();
            continue;
        }
        if (std::regex_match(line, m, kHeading)) {
            flush();
            const size_t level = m[1].length();
            static const float sizes[] = {26, 21, 18, 16, 15, 14};
            textLabel(into, markdownInline(m[2].str()), width, sizes[level - 1], level <= 2 ? 700 : 600);
            if (level == 1) rule(into);
            continue;
        }
        if (std::regex_match(line, kRule)) {
            flush();
            rule(into);
            continue;
        }
        // Tables: a header row, then |---|---|
        if (t.find('|') != std::string::npos && i + 1 < lines.size() && std::regex_match(lines[i + 1], kSeparator)) {
            flush();
            std::vector<std::vector<std::string>> rows{cells(line)};
            size_t j = i + 2;
            for (; j < lines.size() && trim(lines[j]).find('|') != std::string::npos; ++j) rows.push_back(cells(lines[j]));
            i = j - 1;
            const size_t cols = rows[0].size();
            auto* table = into->add<Flex>(Flex::Direction::Column);
            table->setBackground("surface_container_low", false)->setRadius(Radii(12))->setPadding(Insets(4));
            table->alignSelf = Align::Start;
            const float cellW = std::max(60.0f, (width - 8) / static_cast<float>(cols));
            for (size_t r = 0; r < rows.size(); ++r) {
                if (r == 1) rule(table);
                auto* row = table->add<Flex>(Flex::Direction::Row);
                row->setAlignItems(Align::Start);
                for (size_t c = 0; c < cols; ++c) {
                    auto* cell = row->add<Flex>(Flex::Direction::Column);
                    cell->setPadding({10, 6});
                    cell->fixedWidth = cellW;
                    const std::string text = c < rows[r].size() ? rows[r][c] : "";
                    textLabel(cell, r == 0 ? "<b>" + markdownInline(text) + "</b>" : markdownInline(text), cellW - 20, 14, 400);
                }
            }
            continue;
        }
        // > quotes, drawn with a bar, what's in them rendered as Markdown
        if (t.starts_with('>')) {
            flush();
            std::vector<std::string> inner;
            size_t j = i;
            for (; j < lines.size() && trim(lines[j]).starts_with('>'); ++j) {
                std::string q = trim(lines[j]).substr(1);
                if (q.starts_with(' ')) q.erase(0, 1);
                inner.push_back(q);
            }
            i = j - 1;
            auto* row = into->add<Flex>(Flex::Direction::Row);
            row->setSpacing(12)->setAlignItems(Align::Stretch);
            row->alignSelf = Align::Stretch;
            auto* bar = row->add<Flex>(Flex::Direction::Column);
            bar->setBackground("primary", false)->setRadius(Radii(1.5f));
            bar->fixedWidth = 3;
            auto* body = row->add<Flex>(Flex::Direction::Column);
            body->setSpacing(8)->setAlignItems(Align::Start);
            body->grow = true;
            if (depth < 6) render(body, inner, width - 15, baseDir, depth + 1);
            for (auto& ch : body->children())
                if (auto* l = dynamic_cast<Label*>(ch.get())) l->setColor("on_surface_variant", false);
            continue;
        }
        // List items: bullets, numbers and checkboxes, indented by level
        if (std::regex_match(line, m, kItem)) {
            flush();
            const size_t indent = m[1].length();
            const std::string marker = m[2].str();
            std::string text = m[3].str();
            // Lines carrying on the item (indented, not a new item)
            size_t j = i + 1;
            for (; j < lines.size(); ++j) {
                const std::string nt = trim(lines[j]);
                if (nt.empty() || std::regex_match(lines[j], kItem) || lines[j].find_first_not_of(' ') < indent + 2) break;
                text += " " + nt;
            }
            i = j - 1;
            auto* row = into->add<Flex>(Flex::Direction::Row);
            row->setSpacing(8)->setAlignItems(Align::Start)->setPadding({static_cast<float>(std::min<size_t>(indent, 12)) * 8.0f, 0, 0, 0});
            row->alignSelf = Align::Start;
            const bool task = text.size() >= 3 && text[0] == '[' && text[2] == ']' && (text[1] == ' ' || text[1] == 'x' || text[1] == 'X');
            if (task) {
                const bool done = text[1] != ' ';
                row->add<Icon>(done ? "check_box" : "check_box_outline_blank", 18)->setFill(done)->setColor(done ? "primary" : "on_surface_variant", false);
                text = trim(text.substr(3));
                auto* l = textLabel(row, done ? "<s>" + markdownInline(text) + "</s>" : markdownInline(text), width - 30 - static_cast<float>(indent) * 8, 15, 400);
                if (done) l->setColor("on_surface_variant", false);
            } else {
                const bool numbered = std::isdigit(static_cast<unsigned char>(marker[0]));
                auto* mk = row->add<Label>(numbered ? marker : (indent >= 2 ? "◦" : "•"));
                mk->setSize(15)->setColor(numbered ? "on_surface_variant" : "primary", false);
                mk->fixedWidth = numbered ? 22 : 12;
                textLabel(row, markdownInline(text), width - 30 - static_cast<float>(indent) * 8, 15, 400);
            }
            continue;
        }
        if (std::regex_match(line, m, kImage)) {
            flush();
            picture(into, m[1].str(), m[2].str(), width, baseDir);
            continue;
        }
        // A paragraph's line; two spaces or a backslash at the end break it
        const bool hard = line.ends_with("  ") || line.ends_with('\\');
        std::string piece = t;
        if (piece.ends_with('\\')) piece.pop_back();
        para += (para.empty() || para.ends_with('\n') ? "" : " ") + piece + (hard ? "\n" : "");
    }
    flush();
}

} // namespace

std::string markdownInline(const std::string& text) {
    std::string m = inlineRaw(text);
    if (!validMarkup(m)) m = escape(text);
    return m;
}

void renderMarkdown(Flex* into, const std::string& markdown, float width, const std::string& baseDir) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t nl; (nl = markdown.find('\n', start)) != std::string::npos; start = nl + 1) {
        std::string l = markdown.substr(start, nl - start);
        if (l.ends_with('\r')) l.pop_back();
        lines.push_back(std::move(l));
    }
    if (start < markdown.size()) lines.push_back(markdown.substr(start));
    render(into, lines, width, baseDir, 0);
}

} // namespace m3::shell
