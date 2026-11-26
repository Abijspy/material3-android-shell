#pragma once

#include <string>

namespace m3::ui {
class Flex;
}

namespace m3::shell {

// Markdown drawn as the shell's own widgets, added to `into`: headings,
// paragraphs with bold, italic, code, strikethrough and links, bullet,
// numbered and check lists, quotes, fenced code, tables, rules and
// pictures (a path relative to `baseDir`). `width`: the room for text
void renderMarkdown(ui::Flex* into, const std::string& markdown, float width, const std::string& baseDir);

// One line's inline Markdown as Pango markup (always valid: plain, escaped
// text when it can't be made so)
std::string markdownInline(const std::string& text);

} // namespace m3::shell
