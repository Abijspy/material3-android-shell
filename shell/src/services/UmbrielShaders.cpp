#include "services/UmbrielShaders.h"

#include "core/EventLoop.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "services/Compositor.h"
#include "services/Umbriel.h"
#include "services/UmbrielConfig.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <openssl/evp.h>
#include <sstream>
#include <unistd.h>

namespace m3 {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kCatalogue = "https://raw.githubusercontent.com/noctalia-dev/community-umbriel-shaders/catalogue/catalogue.json";
constexpr const char* kRaw = "https://raw.githubusercontent.com/noctalia-dev/community-umbriel-shaders/";
// Where config.toml's include finds them, from its own folder
constexpr const char* kIncludeRoot = "shaders/community/";

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string sha256(const std::string& data) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(data.data(), data.size(), out, &len, EVP_sha256(), nullptr);
    std::string hex;
    for (unsigned int i = 0; i < len; ++i) hex += std::format("{:02x}", out[i]);
    return hex;
}

// A repository path that stays inside the folder it's written to
bool safe(const std::string& p) {
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos) return false;
    for (const auto& part : fs::path(p))
        if (part == ".." || part == ".") return false;
    return true;
}

// A path in a URL: each part's unusual characters escaped
std::string urlPath(const std::string& p) {
    std::string out;
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

std::vector<std::string> strings(const json& j) {
    std::vector<std::string> out;
    if (j.is_array())
        for (const auto& v : j)
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

// A member that's an object, else an empty one (null where none is given).
// A reference, not a copy: `for (… : obj(j, k).items())` iterates j itself
// (a temporary there would be gone before the loop's first step)
const json& obj(const json& j, const char* key) {
    static const json empty = json::object();
    const auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : empty;
}

std::string str(const json& j, const char* key, const std::string& fallback = {}) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

tomledit::Path split(const std::string& dotted) {
    tomledit::Path out;
    std::stringstream ss(dotted);
    for (std::string part; std::getline(ss, part, '.');) out.push_back(part);
    return out;
}

// "effects.cursor", "animation.windows_in" from a recipe's keys
std::vector<std::string> slotsOf(const json& activation) {
    std::vector<std::string> out;
    for (const auto& [k, v] : obj(activation, "settings").items()) {
        const auto p = split(k);
        std::string slot;
        if (p.size() == 2 && p[0] == "effects") slot = k;
        else if (p.size() == 3 && p[0] == "animation" && p[2] == "effect") slot = "animation." + p[1];
        if (!slot.empty() && std::ranges::find(out, slot) == out.end()) out.push_back(slot);
    }
    return out;
}

const char* kSlots[] = {"effects.border",         "effects.window",      "effects.screen",    "effects.cursor",
                        "animation.windows_in",   "animation.windows_out", "animation.windows_move", "animation.workspaces",
                        "animation.overview",     "animation.scratchpad", "animation.border",   "animation.dim_unfocused",
                        "animation.layers"};

} // namespace

UmbrielShaders& UmbrielShaders::get() {
    static UmbrielShaders instance;
    return instance;
}

std::string UmbrielShaders::root() const {
    return UmbrielConfig::get().dir() + "/shaders/community";
}

std::string UmbrielShaders::cacheDir() const {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    return (xdg && *xdg ? std::string(xdg) : paths::home() + "/.cache") + "/material3ui/umbriel-shaders";
}

// ── The catalogue ──

bool UmbrielShaders::load(const std::string& text, std::string* why) {
    try {
        const auto c = json::parse(text);
        if (c.value("schema_version", 0) != 1) {
            if (why) *why = "a catalogue in a format this shell doesn't know yet";
            return false;
        }
        if (c.value("draft", true)) {
            if (why) *why = "a draft catalogue";
            return false;
        }
        const std::string rev = str(obj(c, "source"), "revision");
        if (rev.size() != 40 || !std::ranges::all_of(rev, [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); })) {
            if (why) *why = "a catalogue without its revision";
            return false;
        }
        std::vector<Effect> effects;
        for (const auto& e : c.value("effects", json::array())) {
            Effect x;
            if (!e.is_object()) continue;
            x.id = str(e, "id");
            x.name = str(e, "name", x.id);
            x.description = str(e, "description");
            x.author = str(e, "author");
            x.license = str(e, "license");
            x.kind = str(e, "kind");
            x.preset = str(e, "preset");
            x.role = str(e, "role", "effect");
            x.path = str(e, "path", x.id);
            x.uses = strings(e.value("uses", json::array()));
            x.tags = strings(e.value("tags", json::array()));
            x.dependencies = strings(e.value("dependencies", json::array()));
            x.features = strings(obj(e, "requirements").value("features", json::array()));
            x.installFiles = strings(e.value("install_files", json::array()));
            x.preview = str(obj(e, "preview"), "image");
            x.activation = e.value("activation", json());
            if (x.id.empty() || x.preset.empty() || !safe(x.path)) continue;
            effects.push_back(std::move(x));
        }
        std::vector<Family> families;
        for (const auto& f : c.value("families", json::array())) {
            if (!f.is_object()) continue;
            Family x{str(f, "id"), str(f, "name"), str(f, "description"), strings(f.value("members", json::array())),
                     strings(f.value("authors", json::array())), f.value("activation", json())};
            if (!x.id.empty()) families.push_back(std::move(x));
        }
        std::map<std::string, std::pair<long long, std::string>> files;
        for (const auto& [p, info] : obj(c, "files").items())
            if (info.is_object() && info.contains("bytes") && info["bytes"].is_number_integer()) files[p] = {info["bytes"].get<long long>(), str(info, "sha256")};
        m_effects = std::move(effects);
        m_families = std::move(families);
        m_files = std::move(files);
        m_revision = rev;
        return true;
    } catch (const std::exception& e) {
        if (why) *why = std::string("an unreadable catalogue: ") + e.what();
        return false;
    }
}

void UmbrielShaders::refresh(bool force) {
    const std::string file = cacheDir() + "/catalogue.json";
    std::error_code ec;
    if (m_state == State::Empty && fs::exists(file, ec) && load(readFile(file), nullptr)) {
        m_state = State::Ready;
        changed.emit();
    }
    // A day old at most
    if (!force && m_state == State::Ready) {
        const auto age = fs::file_time_type::clock::now() - fs::last_write_time(file, ec);
        if (!ec && age < std::chrono::hours(24)) return;
    }
    if (m_fetching) return;
    m_fetching = true;
    if (m_state != State::Ready) m_state = State::Loading;
    changed.emit();
    fs::create_directories(cacheDir(), ec);
    const std::string tmp = file + ".part";
    run({"curl", "-fsSL", "--max-time", "30", "-o", tmp, kCatalogue}, [this, file, tmp](int code, const std::string&) {
        m_fetching = false;
        std::string why;
        std::error_code ec;
        if (code == 0 && load(readFile(tmp), &why)) {
            fs::rename(tmp, file, ec);
            m_state = State::Ready;
            m_error.clear();
        } else {
            fs::remove(tmp, ec);
            m_error = code != 0 ? "Couldn't reach GitHub for the shader catalogue" : "GitHub sent " + why;
            log::warn("umbriel shaders: {}", m_error);
            if (m_state != State::Ready) m_state = State::Failed;
        }
        changed.emit();
    });
}

const UmbrielShaders::Effect* UmbrielShaders::find(const std::string& id) const {
    for (const auto& e : m_effects)
        if (e.id == id) return &e;
    return nullptr;
}

const UmbrielShaders::Effect* UmbrielShaders::byPreset(const std::string& preset) const {
    for (const auto& e : m_effects)
        if (e.preset == preset) return &e;
    return nullptr;
}

bool UmbrielShaders::installed(const std::string& id) const {
    const auto* e = find(id);
    std::error_code ec;
    return e && fs::exists(root() + "/" + e->path + "/effect.toml", ec);
}

void UmbrielShaders::closure(const std::string& id, std::vector<const Effect*>& out) const {
    const auto* e = find(id);
    if (!e || std::ranges::find(out, e) != out.end()) return;
    out.push_back(e);
    for (const auto& d : e->dependencies) closure(d, out);
}

std::vector<std::pair<std::string, std::string>> UmbrielShaders::inUse() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const char* slot : kSlots) {
        auto path = split(slot);
        if (path[0] == "animation") path.push_back("effect");
        const auto v = UmbrielConfig::get().value(path);
        if (v.is_string() && !v.get<std::string>().empty()) out.push_back({slot, v.get<std::string>()});
    }
    return out;
}

// ── Downloading ──

void UmbrielShaders::download(const std::vector<std::string>& files, const std::string& dir, std::function<void(std::string)> done) {
    std::vector<std::string> argv = {"curl", "-fsSL", "--max-time", "180", "--create-dirs", "--parallel", "--parallel-max", "6"};
    for (const auto& f : files) {
        if (!safe(f) || !m_files.contains(f)) {
            done("the catalogue lists a file it can't vouch for (" + f + ")");
            return;
        }
        argv.insert(argv.end(), {kRaw + m_revision + "/" + urlPath(f), "-o", dir + "/" + f});
    }
    run(argv, [this, files, dir, done = std::move(done)](int code, const std::string&) {
        std::string error = code != 0 ? "Couldn't download it from GitHub" : "";
        // Each exactly as the catalogue says; one that isn't, gone
        for (const auto& f : files) {
            const std::string p = dir + "/" + f;
            std::error_code ec;
            if (!fs::exists(p, ec)) continue;
            const std::string data = readFile(p);
            const auto& [bytes, hash] = m_files[f];
            if (static_cast<long long>(data.size()) != bytes || sha256(data) != hash) {
                fs::remove(p, ec);
                if (error.empty()) error = f + " didn't match the catalogue";
            }
        }
        done(error);
    });
}

void UmbrielShaders::install(const std::vector<const Effect*>& effects, std::function<void(std::string)> done) {
    std::vector<std::string> files;
    for (const auto* e : effects)
        for (const auto& f : e->installFiles)
            if (std::ranges::find(files, f) == files.end()) files.push_back(f);
    const std::string tmp = std::format("{}/download-{}-{}", cacheDir(), getpid(), std::chrono::steady_clock::now().time_since_epoch().count());
    download(files, tmp, [this, files, tmp, done = std::move(done)](std::string error) {
        std::error_code ec;
        if (error.empty()) {
            // Checked: into place
            for (const auto& f : files) {
                const fs::path to = root() + "/" + f;
                fs::create_directories(to.parent_path(), ec);
                fs::copy_file(tmp + "/" + f, to, fs::copy_options::overwrite_existing, ec);
                if (ec && error.empty()) error = "Couldn't write " + to.string() + ": " + ec.message();
            }
        }
        fs::remove_all(tmp, ec);
        done(error);
    });
}

void UmbrielShaders::finish(const std::string& message, bool error) {
    m_busy.clear();
    m_message = message;
    m_messageError = error;
    if (error) log::warn("umbriel shaders: {}", message);
    changed.emit();
}

// ── Turning on and off ──

void UmbrielShaders::activate(const std::string& label, const json& activation, const std::vector<std::string>& presets) {
    auto& cfg = UmbrielConfig::get();
    UmbrielConfig::Changes changes;
    // Its includes, optional: a folder removed later is passed over, not an error
    json optional = cfg.own({"include", "optional", "files"});
    if (!optional.is_array()) optional = json::array();
    const json required = cfg.own({"include", "files"});
    bool added = false;
    for (const auto& inc : strings(activation.value("include", json::array()))) {
        const std::string p = kIncludeRoot + inc;
        if (std::ranges::find(optional, json(p)) != optional.end() || (required.is_array() && std::ranges::find(required, json(p)) != required.end())) continue;
        optional.push_back(p);
        added = true;
    }
    if (added) changes.push_back({{"include", "optional", "files"}, optional});
    for (const auto& [k, v] : obj(activation, "settings").items()) changes.push_back({split(k), v});
    const auto slots = slotsOf(activation);
    cfg.apply(changes, [this, label, presets, slots](bool saved) {
        if (!saved) return finish("Umbriel wouldn't take " + label + ": " + UmbrielConfig::get().lastError(), true);
        // Umbriel reloads as the file is saved; then whether it compiled
        EventLoop::get().addTimer(1200, [this, label, presets, slots] { checkCompiled(label, presets, slots, 0); });
    });
}

void UmbrielShaders::checkCompiled(const std::string& label, std::vector<std::string> presets, std::vector<std::string> slots, int tries) {
    if (Compositor::session() != "umbriel") return finish(label + " is on, from your next Umbriel session", false);
    const auto report = Umbriel::get().request({{"cmd", "effects"}});
    if (!report || !report->is_object()) return finish(label + " is on", false);
    bool waiting = false;
    const auto list = report->contains("presets") && (*report)["presets"].is_array() ? (*report)["presets"] : json::array();
    for (const auto& name : presets) {
        std::string state;
        for (const auto& p : list)
            if (p.is_object() && str(p, "name") == name) state = str(p, "state");
        if (state == "failed" || state == "inert") {
            for (const auto& s : slots) turnOff(s);
            return finish(std::format("Umbriel {} couldn't {} {}: it's off again. It may need a newer Umbriel",
                                      UmbrielConfig::get().umbrielVersion(), state == "failed" ? "compile" : "read the shader of", label),
                          true);
        }
        if (state != "compiled") waiting = true;
    }
    // Still loading: a look again, a few times
    if (waiting && tries < 4) {
        EventLoop::get().addTimer(1000, [this, label, presets, slots, tries] { checkCompiled(label, presets, slots, tries + 1); });
        return;
    }
    finish(label + " is on", false);
}

void UmbrielShaders::use(const std::string& id) {
    const auto* e = find(id);
    if (!e || !m_busy.empty()) return;
    if (!e->activation.is_object()) return finish(e->name + " is a part of another effect, turned on with it", true);
    m_busy = id;
    m_message.clear();
    changed.emit();
    std::vector<const Effect*> all;
    closure(id, all);
    const std::string name = e->name, preset = e->preset;
    const json activation = e->activation;
    install(all, [this, name, preset, activation](std::string error) {
        if (!error.empty()) return finish(error, true);
        activate(name, activation, {preset});
    });
}

void UmbrielShaders::useFamily(const std::string& id) {
    if (!m_busy.empty()) return;
    const Family* f = nullptr;
    for (const auto& x : m_families)
        if (x.id == id) f = &x;
    if (!f || !f->activation.is_object()) return;
    m_busy = "family:" + id;
    m_message.clear();
    changed.emit();
    std::vector<const Effect*> all;
    for (const auto& m : f->members) closure(m, all);
    // The presets its recipe selects
    std::vector<std::string> presets;
    for (const auto& [k, v] : obj(f->activation, "settings").items())
        if (v.is_string() && byPreset(v.get<std::string>())) presets.push_back(v.get<std::string>());
    const std::string name = f->name;
    const json activation = f->activation;
    install(all, [this, name, activation, presets](std::string error) {
        if (!error.empty()) return finish(error, true);
        activate(name, activation, presets);
    });
}

void UmbrielShaders::turnOff(const std::string& slot) {
    auto& cfg = UmbrielConfig::get();
    const auto path = split(slot);
    if (path.size() == 2 && path[0] == "effects") {
        cfg.apply({{path, ""}});
        return;
    }
    if (path.size() == 2 && path[0] == "animation") {
        // Its effect and the timing that came with it: Umbriel's own animation again
        UmbrielConfig::Changes changes;
        for (const char* k : {"effect", "duration_ms", "curve"})
            if (!cfg.own({"animation", path[1], k}).is_null()) changes.push_back({{"animation", path[1], k}, nullptr});
        // An effect set by an include: cleared instead
        if (cfg.own({"animation", path[1], "effect"}).is_null()) changes.push_back({{"animation", path[1], "effect"}, ""});
        cfg.apply(changes);
    }
}

void UmbrielShaders::remove(const std::string& id) {
    const auto* e = find(id);
    if (!e || !m_busy.empty()) return;
    auto& cfg = UmbrielConfig::get();
    // It, and companions no other installed effect needs
    std::vector<const Effect*> mine;
    closure(id, mine);
    std::vector<const Effect*> others;
    for (const auto& x : m_effects)
        if (x.id != id && x.role != "companion" && installed(x.id)) closure(x.id, others);
    std::vector<std::string> dirs;
    for (const auto* x : mine)
        if (x == e || std::ranges::find(others, x) == others.end()) dirs.push_back(root() + "/" + x->path);
    // Out of config.toml: its include, and its selection where it's in use
    UmbrielConfig::Changes changes;
    const std::string inc = kIncludeRoot + e->path + "/effect.toml";
    for (const tomledit::Path& list : {tomledit::Path{"include", "optional", "files"}, tomledit::Path{"include", "files"}}) {
        json files = cfg.own(list);
        if (!files.is_array()) continue;
        const auto before = files.size();
        files.erase(std::remove(files.begin(), files.end(), json(inc)), files.end());
        if (files.size() != before) changes.push_back({list, files});
    }
    for (const auto& [slot, preset] : inUse()) {
        if (preset != e->preset) continue;
        const auto p = split(slot);
        if (p[0] == "effects") changes.push_back({p, ""});
        else
            for (const char* k : {"effect", "duration_ms", "curve"})
                if (!cfg.own({"animation", p[1], k}).is_null()) changes.push_back({{"animation", p[1], k}, nullptr});
    }
    m_busy = id;
    m_message.clear();
    changed.emit();
    const std::string name = e->name;
    auto removeFiles = [this, dirs, name] {
        std::error_code ec;
        for (const auto& d : dirs) fs::remove_all(d, ec);
        finish("Removed " + name, false);
    };
    if (changes.empty()) return removeFiles();
    cfg.apply(changes, [this, removeFiles, name](bool saved) {
        if (!saved) return finish("Couldn't take " + name + " out of config.toml: " + UmbrielConfig::get().lastError(), true);
        removeFiles();
    });
}

// ── Previews ──

std::string UmbrielShaders::preview(const Effect& e) {
    if (e.preview.empty() || m_revision.empty()) return {};
    const std::string p = cacheDir() + "/previews/" + m_revision + "/" + e.preview;
    std::error_code ec;
    return fs::exists(p, ec) ? p : std::string();
}

void UmbrielShaders::fetchPreviews(const std::vector<const Effect*>& effects) {
    for (const auto* e : effects)
        if (e && !e->preview.empty() && preview(*e).empty()) m_previewsWanted.insert(e->preview);
    if (m_previewsFetching || m_previewsWanted.empty() || m_revision.empty()) return;
    m_previewsFetching = true;
    const std::vector<std::string> files(m_previewsWanted.begin(), m_previewsWanted.end());
    m_previewsWanted.clear();
    download(files, cacheDir() + "/previews/" + m_revision, [this](const std::string&) {
        m_previewsFetching = false;
        changed.emit();
        if (!m_previewsWanted.empty()) fetchPreviews({});
    });
}

} // namespace m3

