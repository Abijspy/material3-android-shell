#pragma once

#include "core/Signal.h"
#include "services/TomlEdit.h"

#include <deque>
#include <set>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace m3 {

// Umbriel's config.toml, for Umbriel Settings: what's in effect (config.toml
// over the files it includes, in their order), and changes written into
// config.toml itself, which Umbriel applies last and reloads as it's saved.
// Everything else in the file stays as it was (services/TomlEdit). With no
// config.toml yet, the first change starts it from Umbriel's packaged one.
// The file as it was is kept once per session in
// ~/.config/material3ui/backups.
//
// Umbriel changes as it's developed, so nothing is written that the
// installed one would refuse: a change goes to a copy beside config.toml
// first, and `umbriel config validate` checks it; with an error, or
// Umbriel saying it would ignore that very setting, config.toml stays as
// it was and lastError() says why. `umbriel config schema` says which
// options this Umbriel has, with its ranges, choices and defaults: the
// pages show only those
class UmbrielConfig {
public:
    static UmbrielConfig& get();

    std::string dir() const;  // ~/.config/umbriel
    std::string file() const; // its config.toml
    bool exists() const;
    // Why it can't be read ("line 12: …"), "" when it can
    const std::string& readError() const { return m_readError; }

    // In effect: null where nothing sets it (Umbriel's default then)
    nlohmann::json value(const tomledit::Path& path) const;
    // The file that sets it ("config.toml", "material3ui.toml"); "" none
    std::string from(const tomledit::Path& path) const;

    // Into config.toml, once the installed Umbriel accepts it (changed then,
    // or lastError() says why not); false when it can't even be tried
    bool set(const tomledit::Path& path, const nlohmann::json& value);
    // Out of config.toml: an include's value, or the default, again
    bool unset(const tomledit::Path& path);

    // Lists of tables ([[window_rule]]): each in Umbriel's order (the
    // includes', then config.toml's), with its file and, for config.toml's,
    // its place among config.toml's own (for removeTable)
    struct Table {
        std::string file;
        nlohmann::json data;
        int index = -1; // in config.toml; -1 for an include's
    };
    std::vector<Table> tables(const std::string& name) const;
    bool addTable(const std::string& name, const nlohmann::json& value); // at config.toml's end
    bool removeTable(const std::string& name, int index);
    // What config.toml itself says at `path` (not its includes); null when nothing
    nlohmann::json own(const tomledit::Path& path) const;
    const std::string& lastError() const { return m_lastError; }

    // The installed Umbriel's own description of an option ("appearance.
    // blur.radius": type, min, max, values, default); null when it has
    // none, or there's no schema (no `umbriel` to ask: everything shown)
    const nlohmann::json* schema(const std::string& dotted) const;
    bool hasSchema() const { return !m_schema.empty(); }
    const std::string& umbrielVersion() const { return m_version; }

    // What `umbriel config validate` said after the last change, a line
    // each ("error: line 12: …"); empty when it was happy (or isn't there)
    const std::vector<std::string>& problems() const { return m_problems; }

    Signal<> changed; // read again (a change here, or the file edited elsewhere)

private:
    UmbrielConfig();
    void load();
    void validate();
    bool write(const std::string& text);
    std::string packaged() const; // Umbriel's own config.toml, to start from
    void loadSchema();
    // A change to config.toml's text, checked by Umbriel before it's
    // written; one at a time, in order (a slider dragged twice)
    bool change(std::function<std::optional<std::string>(const std::string&)> edit, std::string dotted);
    void next();

    struct Doc {
        std::string name;
        nlohmann::json data;
    };
    std::vector<Doc> m_docs; // what config.toml includes, in order, then config.toml
    std::string m_text;      // config.toml as it is
    std::string m_readError, m_lastError;
    std::vector<std::string> m_problems;
    std::set<std::string> m_known; // Umbriel's words about the file as it is: not a change's fault
    bool m_backedUp = false;
    std::map<std::string, nlohmann::json> m_schema;
    std::string m_version;
    struct Edit {
        std::function<std::optional<std::string>(const std::string&)> apply;
        std::string dotted; // the option, for Umbriel's words about it
    };
    std::deque<Edit> m_queue;
    bool m_checking = false;
};

} // namespace m3
