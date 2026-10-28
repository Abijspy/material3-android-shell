#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A reader for KDL as niri's configs use it (version 1, with version 2's
// #true and raw strings read too): nodes with arguments, key=value
// properties and child blocks; //, /* */ and /- comments. For the values
// Niri Settings starts from. Apart from the service for the unit tests.
namespace m3::kdl {

struct Value {
    enum class Type { String, Number, Bool, Null } type = Type::Null;
    std::string text; // a string's text, or a number as written
    double number = 0;
    bool boolean = false;
};

struct Node {
    std::string name;
    std::vector<Value> args;
    std::vector<std::pair<std::string, Value>> props;
    std::vector<Node> children;

    const Value* prop(std::string_view key) const;
};

// The nodes of `text`; on a syntax error, the ones read before it (and
// `error`, if given, says where)
std::vector<Node> parse(std::string_view text, std::string* error = nullptr);

// Queries over a whole config, in file order, sections merged the way niri
// merges them (a later value wins). `path` names a node through its
// parents: {"layout", "border", "width"}.
const Node* last(const std::vector<Node>& doc, std::initializer_list<std::string_view> path);
// A number, string or bool at `path` (its first argument)
std::optional<double> number(const std::vector<Node>& doc, std::initializer_list<std::string_view> path);
std::optional<std::string> string(const std::vector<Node>& doc, std::initializer_list<std::string_view> path);
// A flag: there (true), or with a bool argument ("prefer-no-csd false")
std::optional<bool> flag(const std::vector<Node>& doc, std::initializer_list<std::string_view> path);
// A section's on/off, whichever of its `on` and `off` children comes last
// ({"layout", "border"})
std::optional<bool> onOff(const std::vector<Node>& doc, std::initializer_list<std::string_view> path);

} // namespace m3::kdl
