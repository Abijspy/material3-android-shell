#include "services/Compositor.h"

#include "services/Hyprland.h"

namespace m3 {

Compositor& Compositor::get() {
    // Hyprland's backend; outside Hyprland it stands in, with nothing in it
    return Hyprland::get();
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
