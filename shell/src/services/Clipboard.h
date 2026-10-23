#pragma once

#include "core/Signal.h"

#include <functional>
#include <string>
#include <vector>

namespace m3 {

// Clipboard history through cliphist: the entries newest first, copying one
// back, deleting and wiping. Images (screenshots) are decoded once into a
// thumbnail cache; a copied picture or video file (a file:// URI, as file
// managers copy) gets a thumbnail too, a video's from one of its frames.
// The wl-paste watchers that feed cliphist are started here when nothing
// else (Hyprland's autostart) runs them.
class Clipboard {
public:
    struct Entry {
        std::string line;    // cliphist's "id<TAB>preview", what decode takes
        std::string id, preview;
        bool image = false;  // image data (a screenshot, a copied picture)
        std::string file;    // a picture or video file the entry names
        bool video = false;  // that file is a video
        std::string thumb;   // a picture to show for it, once ready
    };

    static Clipboard& get();

    bool available() const { return m_available; }
    const std::vector<Entry>& entries() const { return m_entries; }

    void refresh(std::function<void()> done = {});
    void copy(const Entry& e);
    void remove(const Entry& e);
    void wipe();
    void ensureWatchers();

    Signal<> changed;

private:
    Clipboard() = default;
    void decodeThumb(Entry& e);
    void fileThumb(Entry& e);
    void videoThumb(Entry& e);

    bool m_available = true;
    std::vector<Entry> m_entries;
};

} // namespace m3
