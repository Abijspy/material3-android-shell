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

    // The edge it's on, from statusBar.position: top, bottom, left or right
    static uint32_t anchor();
    // How thick it is: its height across the screen, or its width down a side
    static int thickness();
    // The space it keeps on its edge: its thickness, and a floating bar's gap
    static int zone();
    // The Framed style (Caelestia's look): the bar's surface runs round the
    // screen as a thin frame (Frame draws it, the bar its widgets)
    static bool framed();

    // Modules the bar can show, and where they are: statusBar.layout holds
    // {"left": [ids], "center": [ids], "right": [ids]} (left is the top on a
    // side bar, right the bottom). A side bar keeps its own, layoutSide,
    // once it has one: switching edges leaves each arrangement be
    static const char* layoutKey();
    struct Module {
        const char* id;
        const char* icon;
        const char* name;
        const char* about = "";
    };
    static const std::vector<Module>& modules();
    // Edit mode, on every bar at once: widgets get move/remove controls,
    // each zone a "+" that opens the widget picker, and a Done button (a
    // side bar is edited in Settings instead)
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
    // The surface: the bar, plus the gap a floating bar keeps from the edge
    int totalHeight() const { return zone(); }
    void addModule(ui::Flex* zone, const std::string& id, bool filled);

    wl::Output* m_output;
    OpenPopup m_openPopup;
    std::unique_ptr<wl::LayerSurface> m_surface;
};

} // namespace m3::shell
