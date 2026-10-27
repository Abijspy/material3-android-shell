// niri's event stream as niri 26.04 sends it, applied to the model, and the
// shell's view of it: per-output workspaces, focus, the windows in focus order
#include "services/NiriModel.h"

#include <cstdio>

using namespace m3;

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static unsigned apply(niri::Model& m, const char* line) {
    return m.apply(nlohmann::json::parse(line));
}

int main() {
    niri::Model m;
    // The stream's start: the whole state
    apply(m, R"({"WorkspacesChanged":{"workspaces":[
        {"id":1,"idx":1,"name":null,"output":"DP-1","is_urgent":false,"is_active":true,"is_focused":true,"active_window_id":2},
        {"id":2,"idx":2,"name":"web","output":"DP-1","is_urgent":false,"is_active":false,"is_focused":false,"active_window_id":null},
        {"id":3,"idx":1,"name":null,"output":"HDMI-A-1","is_urgent":false,"is_active":true,"is_focused":false,"active_window_id":null},
        {"id":4,"idx":2,"name":null,"output":"HDMI-A-1","is_urgent":false,"is_active":false,"is_focused":false,"active_window_id":null}]}})");
    const unsigned c = apply(m, R"({"WindowsChanged":{"windows":[
        {"id":2,"title":"Terminal","app_id":"Alacritty","pid":10,"workspace_id":1,"is_focused":true,"is_floating":false,"is_urgent":false,
         "layout":{},"focus_timestamp":{"secs":100,"nanos":0}},
        {"id":5,"title":"Docs","app_id":"firefox","pid":11,"workspace_id":2,"is_focused":false,"is_floating":true,"is_urgent":false,
         "layout":{},"focus_timestamp":{"secs":90,"nanos":500}},
        {"id":7,"title":"Files","app_id":"nemo","pid":12,"workspace_id":1,"is_focused":false,"is_floating":false,"is_urgent":false,
         "layout":{},"focus_timestamp":{"secs":95,"nanos":0}}]}})");
    expect(c & niri::Model::Windows && c & niri::Model::Focus, "the windows change focus too");
    m.setOutputs(nlohmann::json::parse(R"({"DP-1":{"name":"DP-1","logical":{"x":0,"y":0,"width":1920,"height":1080,"scale":1.0,"transform":"Normal"}},
        "HDMI-A-1":{"name":"HDMI-A-1","logical":{"x":1920,"y":0,"width":1280,"height":720,"scale":1.5,"transform":"Normal"}},
        "eDP-1":{"name":"eDP-1","logical":null}})"));

    // Workspaces: numbered per output
    const auto ws = m.shellWorkspaces();
    expect(ws.size() == 4, "four workspaces");
    if (ws.size() == 4) {
        expect(ws[0].output == "DP-1" && ws[0].number == 1 && ws[0].windows == 2, "DP-1's first, two windows");
        expect(ws[1].name == "web" && ws[1].windows == 1, "a named one");
        expect(ws[2].output == "HDMI-A-1" && ws[2].number == 1 && ws[2].windows == 0, "the other output's own numbers");
    }
    const auto outs = m.shellOutputs();
    expect(outs.size() == 2, "two outputs on the desktop (one off)");
    if (outs.size() == 2) {
        expect(outs[0].name == "DP-1" && outs[0].focused && outs[0].activeWorkspace == 1, "the focused output");
        expect(outs[1].x == 1920 && outs[1].w == 1280 && outs[1].scale == 1.5f && !outs[1].focused, "the other's place");
    }
    expect(m.focusedOutput() == "DP-1", "focused output from the focused workspace");
    expect(m.workspaceAt(2, "HDMI-A-1") && m.workspaceAt(2, "HDMI-A-1")->id == 4, "a workspace by number and output");
    expect(m.workspaceAt(2, "") && m.workspaceAt(2, "")->id == 2, "by number on the focused output");

    // Windows: the focused first, then by when they had focus
    auto wins = m.shellWindows();
    expect(wins.size() == 3, "three windows");
    if (wins.size() == 3) {
        expect(wins[0].id == "2" && wins[0].focusOrder == 0, "the focused one first");
        expect(wins[1].id == "7" && wins[2].id == "5", "then the most recent");
        expect(wins[2].workspace == 2 && wins[2].workspaceName == "web" && wins[2].output == "DP-1" && wins[2].floating, "where it is");
    }
    expect(m.focusedWindow() && m.focusedWindow()->appId == "Alacritty", "the focused window");

    // Switching workspace on another output, with focus
    expect(apply(m, R"({"WorkspaceActivated":{"id":4,"focused":true}})") == niri::Model::Workspaces, "a workspace activated");
    expect(m.focusedOutput() == "HDMI-A-1", "focus moved with it");
    const auto outs2 = m.shellOutputs();
    expect(outs2.size() == 2 && outs2[1].activeWorkspace == 2 && outs2[0].activeWorkspace == 1, "each output's active one");
    expect(apply(m, R"({"WindowFocusChanged":{"id":null}})") & niri::Model::Focus, "focus to nothing");
    expect(m.focusedWindow() == nullptr, "no focused window on an empty workspace");

    // A window opened there, focused; then its title changing; then closed
    unsigned opened = apply(m, R"({"WindowOpenedOrChanged":{"window":{"id":9,"title":"New","app_id":"foot","pid":13,"workspace_id":4,
        "is_focused":true,"is_floating":false,"is_urgent":false,"layout":{},"focus_timestamp":{"secs":120,"nanos":0}}}})");
    expect(opened & niri::Model::Workspaces && opened & niri::Model::Focus, "opened: counts and focus");
    expect(m.shellWorkspaces()[3].windows == 1, "counted on its workspace");
    unsigned retitled = apply(m, R"({"WindowOpenedOrChanged":{"window":{"id":9,"title":"Renamed","app_id":"foot","pid":13,"workspace_id":4,
        "is_focused":true,"is_floating":false,"is_urgent":false,"layout":{},"focus_timestamp":{"secs":120,"nanos":0}}}})");
    expect(retitled & niri::Model::Focus && !(retitled & niri::Model::Workspaces), "a new title: focus, not workspaces");
    expect(m.focusedWindow() && m.focusedWindow()->title == "Renamed", "its new title");
    expect(apply(m, R"({"WindowClosed":{"id":9}})") & niri::Model::Workspaces, "closed");
    expect(m.windows.size() == 3, "gone");

    // Keyboard layouts and the overview
    expect(apply(m, R"j({"KeyboardLayoutsChanged":{"keyboard_layouts":{"names":["English (US)","German"],"current_idx":0}}})j") ==
               niri::Model::Layouts,
           "layouts");
    apply(m, R"({"KeyboardLayoutSwitched":{"idx":1}})");
    expect(m.layouts.size() == 2 && m.layout == 1, "switched layout");
    apply(m, R"({"OverviewOpenedOrClosed":{"is_open":true}})");
    expect(m.overview, "the overview open");
    expect(apply(m, R"({"CastsChanged":{"casts":[]}})") == niri::Model::None && apply(m, R"({"SomethingNew":{}})") == niri::Model::None,
           "others, and ones from later niri, ignored");

    if (failures) std::fprintf(stderr, "%d failed\n", failures);
    return failures ? 1 : 0;
}
