#pragma once

#include "core/Signal.h"

#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace m3 {

// Noctalia's community shaders for Umbriel
// (github.com/noctalia-dev/community-umbriel-shaders), for Umbriel
// Settings → Effects. Their published catalogue (catalogue.json, schema 1)
// lists each effect with its files, their SHA-256 hashes at one revision,
// its companions and how to turn it on. Installing downloads an effect's
// files and its companions' at that revision, checks each against its
// hash, and puts them where the collection's README does
// (~/.config/umbriel/shaders/community). Turning one on is its recipe
// applied to config.toml in one change (UmbrielConfig: checked by the
// installed Umbriel first): the include, under [include.optional] so a
// folder deleted later never stops Umbriel, and its selector. The running
// Umbriel is then asked whether it compiled the shader; one it couldn't is
// turned off again
class UmbrielShaders {
public:
    struct Effect {
        std::string id, name, description, author, license, kind, preset, role, path, preview;
        std::vector<std::string> uses, tags, dependencies, features, installFiles;
        nlohmann::json activation; // {"include": […], "settings": {"effects.cursor": "glow"}}, or null
    };
    struct Family {
        std::string id, name, description;
        std::vector<std::string> members, authors;
        nlohmann::json activation;
    };
    enum class State { Empty, Loading, Ready, Failed };

    static UmbrielShaders& get();
    std::string root() const; // ~/.config/umbriel/shaders/community

    // The catalogue: the cached one at once, a fresh one fetched when it's
    // a day old (or `force`)
    void refresh(bool force = false);
    State state() const { return m_state; }
    const std::string& error() const { return m_error; }
    const std::string& revision() const { return m_revision; }
    const std::vector<Effect>& effects() const { return m_effects; }
    const std::vector<Family>& families() const { return m_families; }
    const Effect* find(const std::string& id) const;
    const Effect* byPreset(const std::string& preset) const;

    bool installed(const std::string& id) const; // its effect.toml here
    // What's being done ("cursor/glow"), "" when nothing
    const std::string& busy() const { return m_busy; }
    // Installs it (its companions too) and turns it on; a family: its
    // members, and its recipe
    void use(const std::string& id);
    void useFamily(const std::string& id);
    // Off: the slot's selector cleared ("effects.cursor"), or an animation
    // event's effect and timing taken out ("animation.windows_in")
    void turnOff(const std::string& slot);
    // Its files gone (and companions nothing else installed needs), its
    // include out of config.toml, and off where it was in use
    void remove(const std::string& id);
    // The slots that have a selection, and what's selected ("glow")
    std::vector<std::pair<std::string, std::string>> inUse() const;
    // What the last use() or remove() ended with, for the page to show
    const std::string& message() const { return m_message; }
    bool messageIsError() const { return m_messageError; }

    // A preview image, once downloaded ("" until then; asks for it)
    std::string preview(const Effect& e);
    void fetchPreviews(const std::vector<const Effect*>& effects);

    Signal<> changed;

private:
    UmbrielShaders() = default;
    std::string cacheDir() const;
    bool load(const std::string& text, std::string* why);
    // An effect and its companions, depth first, each once
    void closure(const std::string& id, std::vector<const Effect*>& out) const;
    // Downloads `files` (repository paths) into `dir`, checks them; `done(error)`
    void download(const std::vector<std::string>& files, const std::string& dir, std::function<void(std::string)> done);
    void install(const std::vector<const Effect*>& effects, std::function<void(std::string)> done);
    void activate(const std::string& label, const nlohmann::json& activation, const std::vector<std::string>& presets);
    void checkCompiled(const std::string& label, std::vector<std::string> presets, std::vector<std::string> slots, int tries);
    void finish(const std::string& message, bool error);

    State m_state = State::Empty;
    std::string m_error, m_revision, m_busy, m_message;
    bool m_messageError = false, m_fetching = false;
    std::vector<Effect> m_effects;
    std::vector<Family> m_families;
    std::map<std::string, std::pair<long long, std::string>> m_files; // path → (bytes, sha256)
    std::set<std::string> m_previewsWanted;
    bool m_previewsFetching = false;
};

} // namespace m3
