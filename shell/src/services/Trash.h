#pragma once

#include "core/Signal.h"

#include <string>

namespace m3 {

// Your trash (the freedesktop one, ~/.local/share/Trash, where file
// managers put what you delete), for the dock: how many things are in it
// (watched), opening it and emptying it
class Trash {
public:
    static Trash& get();
    void start(); // the count, and a watch on it (once)

    int count() const { return m_count; }
    std::string dir() const;
    void open() const; // in the file manager (trash:/// with gvfs, so things can be restored)
    void empty();      // everything in it, for good
    Signal<> changed;

private:
    Trash() = default;
    void recount();
    int m_count = 0;
    bool m_started = false;
};

} // namespace m3
