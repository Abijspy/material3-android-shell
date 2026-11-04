#include "services/SessionApps.h"

#include "core/Config.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "services/Compositor.h"
#include "services/Search.h"
#include "services/Session.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace m3 {

namespace {
std::string file() {
    return paths::stateDir() + "/session-apps.json";
}
bool enabled() {
    return Config::get().value<bool>("/session/reopenApps", false);
}
std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

SessionApps& SessionApps::get() {
    static SessionApps instance;
    return instance;
}

void SessionApps::start() {
    EventLoop::get().addTimer(120 * 1000, [this] {
        if (enabled()) save();
    }, true);
    Session::get().ending.connectForever([this] {
        if (enabled()) save();
    });
    // A login, not a restart of the shell: the runtime folder starts empty
    const std::string mark = paths::runtimeDir() + "/session-apps-done";
    std::error_code ec;
    const bool fresh = !std::filesystem::exists(mark, ec);
    std::filesystem::create_directories(paths::runtimeDir(), ec);
    std::ofstream(mark) << "1\n";
    // The compositor's windows first (and its own autostart's)
    if (fresh && enabled()) EventLoop::get().addTimer(4000, [this] { restore(); });
}

void SessionApps::save() {
    std::vector<Entry> out;
    for (const auto& w : Compositor::get().windows(true)) {
        if (w.appId.empty() || lower(w.appId).starts_with("material3ui")) continue; // the shell's own
        const auto* app = Apps::get().forClass(w.appId);
        if (!app) continue;
        auto it = std::ranges::find(out, app->id, &Entry::app);
        if (it == out.end()) {
            out.push_back({app->id, w.appId, {}});
            it = out.end() - 1;
        }
        if (w.workspace > 0) it->workspaces.push_back(w.workspace);
    }
    nlohmann::json apps = nlohmann::json::array();
    for (const auto& e : out) apps.push_back({{"app", e.app}, {"class", e.cls}, {"workspaces", e.workspaces}});
    std::error_code ec;
    std::filesystem::create_directories(paths::stateDir(), ec);
    std::ofstream(file() + ".tmp") << nlohmann::json{{"saved", std::time(nullptr)}, {"apps", apps}}.dump(1) << "\n";
    std::filesystem::rename(file() + ".tmp", file(), ec);
}

std::vector<SessionApps::Entry> SessionApps::saved() const {
    std::vector<Entry> out;
    try {
        std::ifstream in(file());
        if (!in) return out;
        for (const auto& a : nlohmann::json::parse(in).value("apps", nlohmann::json::array())) {
            Entry e{a.value("app", ""), a.value("class", ""), {}};
            for (const auto& w : a.value("workspaces", nlohmann::json::array()))
                if (w.is_number()) e.workspaces.push_back(w.get<int>());
            if (!e.app.empty()) out.push_back(std::move(e));
        }
    } catch (const std::exception& e) {
        log::warn("session-apps.json: {}", e.what());
    }
    return out;
}

std::time_t SessionApps::savedAt() const {
    try {
        std::ifstream in(file());
        if (in) return nlohmann::json::parse(in).value("saved", static_cast<std::time_t>(0));
    } catch (...) {
    }
    return 0;
}

void SessionApps::restore() {
    const auto entries = saved();
    if (entries.empty()) return;
    // What's open already (autostarted) stays where it is
    for (const auto& w : Compositor::get().windows(true)) m_seen.push_back(w.id);
    for (const auto& e : entries) {
        const auto* app = [&]() -> const Apps::App* {
            for (const auto& a : Apps::get().all())
                if (a.id == e.app) return &a;
            return nullptr;
        }();
        if (!app) continue;
        // Open already (the compositor's autostart): not again
        const bool open = std::ranges::any_of(Compositor::get().windows(true), [&](const auto& w) { return lower(w.appId) == lower(e.cls); });
        if (open) continue;
        log::info("reopening {}", e.app);
        Apps::get().launch(*app);
        if (!e.workspaces.empty()) m_pending.push_back({e.cls, e.workspaces});
    }
    if (m_pending.empty()) return;
    // Each new window of theirs, onto the workspace it had, for a minute
    m_windows = Compositor::get().windowsChanged.connect([this] {
        for (const auto& w : Compositor::get().windows(true)) {
            if (std::ranges::find(m_seen, w.id) != m_seen.end()) continue;
            m_seen.push_back(w.id);
            auto p = std::ranges::find_if(m_pending, [&](const Pending& x) { return lower(x.cls) == lower(w.appId); });
            if (p == m_pending.end() || p->workspaces.empty()) continue;
            Compositor::get().moveWindow(w.id, p->workspaces.front());
            p->workspaces.erase(p->workspaces.begin());
        }
    });
    EventLoop::get().addTimer(60 * 1000, [this] {
        m_windows = {};
        m_pending.clear();
    });
}

} // namespace m3
