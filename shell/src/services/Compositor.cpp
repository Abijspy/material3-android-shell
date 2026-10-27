#include "services/Compositor.h"

#include "services/Hyprland.h"
#include "services/Niri.h"

#include <cstdlib>

namespace m3 {

Compositor& Compositor::get() {
    // niri's session sets NIRI_SOCKET (and passes on a Hyprland's variable
    // when nested in one: niri's wins); else Hyprland's backend, which
    // outside Hyprland stands in with nothing in it
    static Compositor& chosen = []() -> Compositor& {
        if (const char* niri = std::getenv("NIRI_SOCKET"); niri && *niri) return Niri::get();
        return Hyprland::get();
    }();
    return chosen;
}

int Compositor::activeWorkspace(const std::string& output) const {
    for (const auto& o : outputs())
        if (o.name == output || (output.empty() && o.focused)) return o.activeWorkspace;
    return 0;
}

std::string Compositor::focusedOutput() const {
    for (const auto& o : outputs())
        if (o.focused) return o.name;
    return {};
}

} // namespace m3
