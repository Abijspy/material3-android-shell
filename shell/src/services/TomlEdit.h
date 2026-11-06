#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

// Edits to a TOML file that keep everything else as it was: its comments,
// order, spacing and quoting. A value is replaced where it's written; a new
// one goes beside its table's others (a dotted key where the table is only
// made by dotted keys, a new [table] after its parent's where it's new).
// Values come and go as JSON. For Umbriel's config.toml (UmbrielConfig)
namespace m3::tomledit {

using Path = std::vector<std::string>; // keys, as written ("keybinds", "Mod+T")

// The whole document as JSON: tables objects, arrays arrays, dates strings;
// null (and `error`) when it doesn't parse
nlohmann::json parse(const std::string& text, std::string* error = nullptr);
// The value at `path`, null when it isn't there (or the text doesn't parse)
nlohmann::json get(const std::string& text, const Path& path);
// A value as TOML writes it: "text", 12, 0.5, true, ["a", "b"],
// { action = "spawn kitty", repeat = false }
std::string literal(const nlohmann::json& value);
// A key as TOML writes it: bare where it can be (gap), else quoted ("Mod+T")
std::string key(const std::string& name);

// `text` with `path` set to `value`; nullopt (with `error`) when the text
// doesn't parse, or the edit would leave it unreadable
std::optional<std::string> set(const std::string& text, const Path& path, const nlohmann::json& value, std::string* error = nullptr);
// `text` without `path` (its line, or its entry in an inline table): as if
// never set. The text as it was when `path` isn't in it
std::optional<std::string> remove(const std::string& text, const Path& path, std::string* error = nullptr);

} // namespace m3::tomledit
