#pragma once

#include <string>
#include <vector>

// Where the lock screen's widgets sit: a grid of 3 columns (left, centre,
// right) by 4 rows above the password, one widget per cell. Stored as
// lock.grid in config.json, so the greeter (which reads a synced copy) lays
// out the same screen. Edited in Settings → Lock screen.
namespace m3::shell::lockgrid {

constexpr int kCols = 3, kRows = 4;

struct Item {
    std::string id;
    int col = 1, row = 0;
};

struct Def {
    const char* id;
    const char* icon;
    const char* name;
    const char* about;
};

// Every widget a cell can hold
const std::vector<Def>& widgets();
const Def* find(const std::string& id);
// Its kind, for the editor's library: "Clock & date", "Calendar",
// "Weather", "Music", "Status"
const char* groupOf(const std::string& id);
const std::vector<const char*>& groups();
// When it shows, if not always ("While music plays"); empty: always
const char* whenShown(const std::string& id);

// The saved grid; without one, the v4.1.0 list (lock.widgets) stacked in
// one column under the clock
std::vector<Item> load();
void save(const std::vector<Item>& items);

// Edits, each saved at once (config.json; the lock screen and greeter follow)
void put(const std::string& id, int col, int row);
void remove(int col, int row);
// Moves a widget one cell; one already there swaps places with it
void move(int col, int row, int dcol, int drow);
const char* cellName(int col, int row); // "top left"…

} // namespace m3::shell::lockgrid
