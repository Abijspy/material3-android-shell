#pragma once

#include "core/Signal.h"
#include "services/TomlEdit.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace m3 {

// Umbriel's config.toml, for Umbriel Settings: what's in effect (config.toml
// over the files it includes, in their order), and changes written into
// config.toml itself, which Umbriel applies last and reloads as it's saved.
// Everything else in the file stays as it was (services/TomlEdit). With no
// config.toml yet, the first change starts it from Umbriel's packaged one.
// The file as it was is kept once per session in
// ~/.config/material3ui/backups; after each change `umbriel config
// validate` checks it, and what it says is kept for the pages to show
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

    // Into config.toml; false (and lastError()) when it can't be
    bool set(const tomledit::Path& path, const nlohmann::json& value);
    // Out of config.toml: an include's value, or the default, again
    bool unset(const tomledit::Path& path);
    const std::string& lastError() const { return m_lastError; }

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

    struct Doc {
        std::string name;
        nlohmann::json data;
    };
    std::vector<Doc> m_docs; // what config.toml includes, in order, then config.toml
    std::string m_text;      // config.toml as it is
    std::string m_readError, m_lastError;
    std::vector<std::string> m_problems;
    bool m_backedUp = false;
};

} // namespace m3
