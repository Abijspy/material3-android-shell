// Umbriel's config.toml edited in place: values replaced where they're
// written, new ones beside their table's others, removed with their line,
// and everything else (comments, order, spacing) left as it was
#include "services/TomlEdit.h"

#include <cstdio>

using namespace m3;
using nlohmann::json;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

static const char* kConfig = R"(# Umbriel's config
[include]
files = ["material3ui.toml"]

[general]
autostart = ["noctalia", "kitty"]  # what starts with the session
mod_key = "Super"

[appearance]
corner_radius = 10
blur.enabled = true
"é" = "ü" # a quoted key, before a value

[keybinds]
"Mod+T" = "spawn kitty"
"Mod+Q" = { action = "window-close", repeat = false }
list = [
  1,
  2,
]
)";

int main() {
    const std::string base = kConfig;
    std::string err;

    // Reading
    expect(tomledit::get(base, {"general", "mod_key"}) == "Super", "a string read");
    expect(tomledit::get(base, {"appearance", "blur", "enabled"}) == true, "a dotted key read");
    expect(tomledit::get(base, {"keybinds", "Mod+Q", "action"}) == "window-close", "an inline table's entry read");
    expect(tomledit::get(base, {"layout", "gap"}).is_null(), "what isn't there is null");
    expect(tomledit::get("[x\n", {"x"}).is_null(), "an unreadable file reads as null");

    // Literals
    expect(tomledit::literal(json(0.5)) == "0.5" && tomledit::literal(json(2.0)) == "2.0", "floats stay floats");
    expect(tomledit::literal(json("a \"b\"\n")) == "\"a \\\"b\\\"\\n\"", "strings escaped");
    expect(tomledit::literal(json::array({"a", 1})) == "[\"a\", 1]", "arrays");
    expect(tomledit::key("Mod+T") == "\"Mod+T\"" && tomledit::key("gap") == "gap", "keys quoted only when they must be");

    // A value replaced where it's written, its comment kept
    auto out = tomledit::set(base, {"general", "autostart"}, json::array({"material3ui"}), &err);
    expect(out && has(*out, "autostart = [\"material3ui\"]  # what starts with the session"), "a value replaced, its comment kept");
    expect(out && has(*out, "# Umbriel's config\n[include]") && has(*out, "mod_key = \"Super\""), "the rest left as it was");
    out = tomledit::set(base, {"appearance", "corner_radius"}, 14, &err);
    expect(out && has(*out, "corner_radius = 14\n"), "an integer replaced");
    out = tomledit::set(base, {"appearance", "é"}, "ö", &err);
    expect(out && has(*out, "\"é\" = \"ö\" # a quoted key"), "after a character of more than a byte");
    out = tomledit::set(base, {"keybinds", "list"}, json::array({3}), &err);
    expect(out && has(*out, "list = [3]\n") && !has(*out, "  1,"), "a multi-line array replaced");

    // A new key: beside its table's others, before the blank line and the next table
    out = tomledit::set(base, {"appearance", "border_width"}, 3, &err);
    expect(out && has(*out, "\"é\" = \"ü\" # a quoted key, before a value\nborder_width = 3\n\n[keybinds]"), "a new key after its table's last");
    // In a table made by dotted keys: a dotted key, in the table above it
    out = tomledit::set(base, {"appearance", "blur", "passes"}, 3, &err);
    expect(out && has(*out, "blur.passes = 3\n") && !has(*out, "[appearance.blur]"), "a dotted key where the table is dotted keys");
    // A quoted key
    out = tomledit::set(base, {"keybinds", "Mod+X"}, "spawn foo", &err);
    expect(out && has(*out, "\"Mod+X\" = \"spawn foo\"\n") && tomledit::get(*out, {"keybinds", "Mod+X"}) == "spawn foo", "a quoted key added");
    // A new table: under a header of its own, after its parent's lines or at the end
    out = tomledit::set(base, {"appearance", "shadow", "softness"}, 20, &err);
    expect(out && has(*out, "[appearance.shadow]\nsoftness = 20\n\n[keybinds]"), "a new table after its parent's");
    out = tomledit::set(base, {"layout", "gap"}, 12, &err);
    expect(out && out->ends_with("]\n\n[layout]\ngap = 12\n"), "a new top table at the end");
    out = tomledit::set("", {"general", "xwayland"}, false, &err);
    expect(out && *out == "[general]\nxwayland = false\n", "into an empty file");
    out = tomledit::set("[general]\nmod_key = \"Alt\"", {"general", "show_cheatsheet"}, false, &err);
    expect(out && *out == "[general]\nmod_key = \"Alt\"\nshow_cheatsheet = false\n", "a file without a last newline");
    // Inside an inline table: the table written again
    out = tomledit::set(base, {"keybinds", "Mod+Q", "repeat"}, true, &err);
    expect(out && has(*out, "\"Mod+Q\" = { action = \"window-close\", repeat = true }"), "an inline table's entry set");
    // An inline table as a value
    out = tomledit::set(base, {"keybinds", "Mod+T"}, json{{"action", "spawn kitty"}, {"repeat", false}}, &err);
    expect(out && has(*out, "\"Mod+T\" = { action = \"spawn kitty\", repeat = false }"), "a value replaced by an inline table");

    // Removed with its line
    out = tomledit::remove(base, {"general", "mod_key"}, &err);
    expect(out && !has(*out, "mod_key") && has(*out, "autostart = [\"noctalia\", \"kitty\"]  # what starts with the session\n\n[appearance]"),
           "a line removed");
    out = tomledit::remove(base, {"appearance", "blur", "enabled"}, &err);
    expect(out && !has(*out, "blur.enabled") && has(*out, "corner_radius = 10\n\"é\""), "a dotted key's line removed");
    out = tomledit::remove(base, {"keybinds", "Mod+Q", "repeat"}, &err);
    expect(out && has(*out, "\"Mod+Q\" = { action = \"window-close\" }"), "an inline table's entry removed");
    out = tomledit::remove(base, {"keybinds", "list"}, &err);
    expect(out && !has(*out, "list") && !has(*out, "  2,"), "a multi-line array removed");
    out = tomledit::remove(base, {"layout", "gap"}, &err);
    expect(out && *out == base, "what isn't there: nothing changes");
    // A table emptied: its header goes too, and set then removed is as before
    {
        const std::string end = "[general]\nmod_key = \"Alt\"\n\n";
        auto added = tomledit::set(end, {"appearance", "title_bar", "mode"}, "on", &err);
        expect(added && *added == "[general]\nmod_key = \"Alt\"\n\n[appearance.title_bar]\nmode = \"on\"\n", "one blank line before a new table");
        auto back = added ? tomledit::remove(*added, {"appearance", "title_bar", "mode"}, &err) : std::nullopt;
        expect(back && *back == "[general]\nmod_key = \"Alt\"\n", "an emptied table's header removed");
        auto mid = tomledit::set(base, {"appearance", "shadow", "softness"}, 20, &err);
        auto gone = mid ? tomledit::remove(*mid, {"appearance", "shadow", "softness"}, &err) : std::nullopt;
        expect(gone && *gone == base, "a table added and taken out again leaves the file as it was");
    }

    // Arrays of tables: added at the end, and the nth taken out
    {
        const std::string rules = "[general]\nmod_key = \"Alt\"\n\n[[window_rule]]\nmatch.app_id = \"^firefox$\"\ndefault_workspace = 2\n\n# The terminal\n[[window_rule]]\nmatch.app_id = \"^foot$\"\nopacity = 0.9\n\n[keybinds]\n\"Mod+T\" = \"spawn foot\"\n";
        auto added = tomledit::appendTable(rules, "window_rule", json{{"match", {{"app_id", "^mpv$"}}}, {"default_floating", true}}, &err);
        expect(added && added->ends_with("\"Mod+T\" = \"spawn foot\"\n\n[[window_rule]]\nmatch.app_id = \"^mpv$\"\ndefault_floating = true\n"), "a table added at the end, dotted");
        expect(added && tomledit::get(*added, {"keybinds", "Mod+T"}) == "spawn foot", "the keys after the rules still the keys'");
        auto first = tomledit::removeTable(rules, "window_rule", 0, &err);
        expect(first && !has(*first, "firefox") && has(*first, "# The terminal\n[[window_rule]]\nmatch.app_id = \"^foot$\""), "the first taken out, the next's comment kept");
        auto second = tomledit::removeTable(rules, "window_rule", 1, &err);
        expect(second && !has(*second, "^foot$") && !has(*second, "# The terminal") && has(*second, "default_workspace = 2\n\n[keybinds]"),
               "the second taken out, with its comment");
        expect(second && tomledit::parse(*second)["window_rule"].size() == 1, "one left");
        expect(!tomledit::removeTable(rules, "window_rule", 5, &err), "one that isn't there: refused");
        auto onlyOne = tomledit::appendTable("", "window_rule", json{{"opacity", 0.5}}, &err);
        expect(onlyOne && *onlyOne == "[[window_rule]]\nopacity = 0.5\n", "into an empty file");
        auto none = onlyOne ? tomledit::removeTable(*onlyOne, "window_rule", 0, &err) : std::nullopt;
        expect(none && none->empty(), "the last taken out: nothing left");
    }

    // Refused, leaving nothing half done
    expect(!tomledit::set("[x\n", {"a"}, 1, &err), "an unreadable file isn't edited");
    expect(!tomledit::set(base, {"appearance"}, 1, &err), "a table isn't replaced by a value");
    expect(!tomledit::set(base, {"general", "mod_key", "x"}, 1, &err), "nothing goes inside a string");
    expect(!tomledit::remove("a = 1 b = 2\n", {"a"}, &err), "two on a line (unreadable) isn't edited");

    if (failures == 0) std::printf("toml edit: all passed\n");
    return failures == 0 ? 0 : 1;
}
