#pragma once

#include "core/Signal.h"
#include "shell/bar/Status.h"
#include "wayland/LayerSurface.h"

namespace m3::ui {
class Flex;
}

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace m3::wl {
struct Output;
}

namespace m3::shell {

// The status bar on one output: workspaces and the window title on the left,
// the clock in the centre, status on the right.
class Bar {
public:
    Bar(wl::Output* output, OpenPopup openPopup);

    void rebuild();
    void setVisible(bool visible);
    bool visible() const { return m_surface->visible(); }
    void redraw() { m_surface->requestRedraw(); }

    // Top or bottom edge, from statusBar.position
    static uint32_t anchor();

    // Modules the bar can show, and where they are: statusBar.layout holds
    // {"left": [ids], "center": [ids], "right": [ids]}
    struct Module {
        const char* id;
        const char* icon;
        const char* name;
        const char* about = "";
    };
    static const std::vector<Module>& modules();
    // Edit mode, on every bar at once: widgets get move/remove controls,
    // each zone a "+" that opens the widget picker, and a Done button
    static inline bool editing = false;
    // A zone is a list of groups; a group of two or more widgets shares one
    // capsule on the bar. In config.json a zone lists ids, or arrays of ids
    // for groups.
    using Group = std::vector<std::string>;
    using Layout = std::array<std::vector<Group>, 3>; // left, centre, right
    static Layout layout();
    static void setLayout(const Layout& layout);

    // Edits on a layout (callers save it with setLayout)
    static bool placed(const Layout& l, const std::string& id);
    static void removeWidget(Layout& l, const std::string& id);
    // One step left or right, out of its group, across zones at the ends
    static void moveWidget(Layout& l, const std::string& id, int dir);
    // Joins these widgets (in their zone's order) into one group, placed where the first was
    static void groupWidgets(Layout& l, size_t zone, const std::vector<std::string>& ids);
    // Splits the group holding `id` back into single widgets
    static void ungroup(Layout& l, const std::string& id);
    static void add(Layout& l, size_t zone, const std::string& id);

private:
    int height() const;
    void addModule(ui::Flex* zone, const std::string& id, bool floating);

    wl::Output* m_output;
    OpenPopup m_openPopup;
    std::unique_ptr<wl::LayerSurface> m_surface;
};

} // namespace m3::shell
