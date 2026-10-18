#pragma once

#include "core/Signal.h"

#include <string>
#include <vector>

namespace m3 {

// Clipboard history through cliphist: the entries newest first, copying one
// back, deleting and wiping. Images are decoded once into a thumbnail cache.
// The wl-paste watchers that feed cliphist are started here when nothing
// else (Hyprland's autostart) runs them.
class Clipboard {
public:
    struct Entry {
        std::string line;    // cliphist's "id<TAB>preview", what decode takes
        std::string id, preview;
        bool image = false;
        std::string thumb;   // decoded image file, once ready
    };

    static Clipboard& get();

    bool available() const { return m_available; }
    const std::vector<Entry>& entries() const { return m_entries; }

    void refresh();
    void copy(const Entry& e);
    void remove(const Entry& e);
    void wipe();
    void ensureWatchers();

    Signal<> changed;

private:
    Clipboard() = default;
    void decodeThumb(Entry& e);

    bool m_available = true;
    std::vector<Entry> m_entries;
};

} // namespace m3
