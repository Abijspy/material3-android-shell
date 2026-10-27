#include "services/NiriModel.h"

#include <algorithm>

namespace m3::niri {

namespace {

std::string str(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::optional<uint64_t> id(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned()) return std::nullopt;
    return it->get<uint64_t>();
}

Model::Win window(const nlohmann::json& w) {
    Model::Win x;
    x.id = id(w, "id").value_or(0);
    x.title = str(w, "title");
    x.appId = str(w, "app_id");
    x.workspace = id(w, "workspace_id");
    x.focused = w.value("is_focused", false);
    x.floating = w.value("is_floating", false);
    x.urgent = w.value("is_urgent", false);
    if (const auto t = w.find("focus_timestamp"); t != w.end() && t->is_object())
        x.focusTime = static_cast<double>(t->value("secs", 0ULL)) + static_cast<double>(t->value("nanos", 0U)) / 1e9;
    return x;
}

Model::Ws workspace(const nlohmann::json& w) {
    Model::Ws x;
    x.id = id(w, "id").value_or(0);
    x.idx = w.value("idx", 0);
    x.name = str(w, "name");
    x.output = str(w, "output");
    x.active = w.value("is_active", false);
    x.focused = w.value("is_focused", false);
    x.urgent = w.value("is_urgent", false);
    return x;
}

} // namespace

void Model::setWorkspaces(const nlohmann::json& list) {
    workspaces.clear();
    if (!list.is_array()) return;
    for (const auto& w : list)
        if (w.is_object()) workspaces[id(w, "id").value_or(0)] = workspace(w);
}

void Model::setWindows(const nlohmann::json& list) {
    windows.clear();
    if (!list.is_array()) return;
    for (const auto& w : list)
        if (w.is_object()) windows[id(w, "id").value_or(0)] = window(w);
}

void Model::setOutputs(const nlohmann::json& reply) {
    outputs.clear();
    if (!reply.is_object()) return;
    for (const auto& [name, o] : reply.items()) {
        Out x;
        x.name = name;
        // Off (no logical size): not on the desktop
        const auto l = o.find("logical");
        if (l == o.end() || !l->is_object()) continue;
        x.x = l->value("x", 0.0f);
        x.y = l->value("y", 0.0f);
        x.w = l->value("width", 0.0f);
        x.h = l->value("height", 0.0f);
        x.scale = l->value("scale", 1.0f);
        outputs.push_back(x);
    }
    std::ranges::sort(outputs, {}, &Out::name);
}

unsigned Model::apply(const nlohmann::json& event) {
    if (!event.is_object() || event.size() != 1) return None;
    const auto& [name, e] = *event.items().begin();
    if (name == "WorkspacesChanged") {
        setWorkspaces(e.value("workspaces", nlohmann::json::array()));
        return Workspaces;
    }
    if (name == "WorkspaceActivated") {
        const auto ws = id(e, "id");
        const auto it = ws ? workspaces.find(*ws) : workspaces.end();
        if (it == workspaces.end()) return None;
        // The active one on its output; with focus, the focused one anywhere
        for (auto& [_, w] : workspaces)
            if (w.output == it->second.output) w.active = false;
        it->second.active = true;
        if (e.value("focused", false)) {
            for (auto& [_, w] : workspaces) w.focused = false;
            it->second.focused = true;
        }
        return Workspaces;
    }
    if (name == "WorkspaceUrgencyChanged") {
        if (const auto ws = id(e, "id"); ws && workspaces.contains(*ws)) workspaces[*ws].urgent = e.value("urgent", false);
        return None;
    }
    if (name == "WindowsChanged") {
        setWindows(e.value("windows", nlohmann::json::array()));
        return Windows | Workspaces | Focus;
    }
    if (name == "WindowOpenedOrChanged") {
        const auto w = window(e.value("window", nlohmann::json::object()));
        const auto before = windows.find(w.id);
        // A title change of the focused window is a focus change to the shell
        unsigned change = Windows;
        if (before == windows.end() || before->second.workspace != w.workspace) change |= Workspaces;
        if (w.focused) {
            for (auto& [_, x] : windows) x.focused = false;
            change |= Focus;
        } else if (before != windows.end() && before->second.focused) {
            change |= Focus;
        }
        windows[w.id] = w;
        return change;
    }
    if (name == "WindowClosed") {
        const auto w = id(e, "id");
        const auto it = w ? windows.find(*w) : windows.end();
        if (it == windows.end()) return None;
        const bool focused = it->second.focused;
        windows.erase(it);
        return Windows | Workspaces | (focused ? Focus : None);
    }
    if (name == "WindowFocusChanged") {
        const auto w = id(e, "id");
        for (auto& [i, x] : windows) x.focused = w && i == *w;
        return Windows | Focus;
    }
    if (name == "WindowFocusTimestampChanged") {
        if (const auto w = id(e, "id"); w && windows.contains(*w))
            if (const auto t = e.find("focus_timestamp"); t != e.end() && t->is_object())
                windows[*w].focusTime = static_cast<double>(t->value("secs", 0ULL)) + static_cast<double>(t->value("nanos", 0U)) / 1e9;
        return None;
    }
    if (name == "WindowUrgencyChanged") {
        if (const auto w = id(e, "id"); w && windows.contains(*w)) windows[*w].urgent = e.value("urgent", false);
        return None;
    }
    if (name == "KeyboardLayoutsChanged") {
        const auto& k = e.value("keyboard_layouts", nlohmann::json::object());
        layouts = k.value("names", std::vector<std::string>{});
        layout = k.value("current_idx", 0);
        return Layouts;
    }
    if (name == "KeyboardLayoutSwitched") {
        layout = e.value("idx", 0);
        return Layouts;
    }
    if (name == "OverviewOpenedOrClosed") {
        overview = e.value("is_open", false);
        return Overview;
    }
    return None; // layouts of windows, config, screenshots, screencasts
}

const Model::Win* Model::focusedWindow() const {
    for (const auto& [_, w] : windows)
        if (w.focused) return &w;
    return nullptr;
}

std::string Model::focusedOutput() const {
    for (const auto& [_, w] : workspaces)
        if (w.focused) return w.output;
    return outputs.empty() ? std::string() : outputs.front().name;
}

const Model::Ws* Model::workspaceAt(int idx, const std::string& output) const {
    const std::string on = output.empty() ? focusedOutput() : output;
    for (const auto& [_, w] : workspaces)
        if (w.output == on && w.idx == idx) return &w;
    return nullptr;
}

std::vector<Compositor::Workspace> Model::shellWorkspaces() const {
    std::vector<Compositor::Workspace> out;
    for (const auto& [i, w] : workspaces) {
        Compositor::Workspace x;
        x.number = w.idx;
        x.name = w.name;
        x.output = w.output;
        for (const auto& [_, win] : windows) x.windows += win.workspace == i;
        out.push_back(x);
    }
    std::ranges::sort(out, [](const auto& a, const auto& b) { return a.output != b.output ? a.output < b.output : a.number < b.number; });
    return out;
}

std::vector<Compositor::Output> Model::shellOutputs() const {
    std::vector<Compositor::Output> out;
    const std::string focused = focusedOutput();
    auto add = [&](const std::string& name) -> Compositor::Output& {
        for (auto& o : out)
            if (o.name == name) return o;
        Compositor::Output o;
        o.name = name;
        o.focused = name == focused;
        out.push_back(o);
        return out.back();
    };
    for (const auto& o : outputs) {
        auto& x = add(o.name);
        x.x = o.x;
        x.y = o.y;
        x.w = o.w;
        x.h = o.h;
        x.scale = o.scale;
    }
    // Every output with workspaces, its active one's number
    for (const auto& [_, w] : workspaces) {
        auto& x = add(w.output);
        if (w.active) x.activeWorkspace = w.idx;
    }
    return out;
}

std::vector<Compositor::Window> Model::shellWindows() const {
    std::vector<std::pair<double, Compositor::Window>> found;
    for (const auto& [i, w] : windows) {
        Compositor::Window x;
        x.id = std::to_string(i);
        x.appId = w.appId;
        x.title = w.title;
        x.floating = w.floating;
        if (w.workspace)
            if (const auto ws = workspaces.find(*w.workspace); ws != workspaces.end()) {
                x.workspace = ws->second.idx;
                x.workspaceName = ws->second.name;
                x.output = ws->second.output;
            }
        // The focused one first, then the most recently focused
        found.push_back({w.focused ? 1e18 : w.focusTime, x});
    }
    std::ranges::stable_sort(found, std::greater{}, &std::pair<double, Compositor::Window>::first);
    std::vector<Compositor::Window> out;
    for (size_t k = 0; k < found.size(); ++k) {
        found[k].second.focusOrder = static_cast<int>(k);
        out.push_back(std::move(found[k].second));
    }
    return out;
}

} // namespace m3::niri
