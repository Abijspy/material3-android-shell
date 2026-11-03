#pragma once

#include "core/Signal.h"

#include <string>
#include <vector>

namespace m3 {

// A to-do list: the Notes panel, its desktop widget and `msg notes`. Kept
// in ~/.local/state/material3ui/notes.json, newest first
class Notes {
public:
    struct Item {
        std::string text;
        bool done = false;
    };
    static Notes& get();
    const std::vector<Item>& items() const { return m_items; }
    void add(const std::string& text);
    void toggle(size_t i);
    void remove(size_t i);
    void clearDone();
    size_t open() const; // not done yet
    Signal<> changed;

private:
    Notes();
    void save() const;
    std::vector<Item> m_items;
};

} // namespace m3
