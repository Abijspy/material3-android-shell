#include "shell/LockLayout.h"

#include "core/Config.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace m3::shell::lockgrid {

const std::vector<Def>& widgets() {
    static const std::vector<Def> all = {
        {"clock", "schedule", "Clock", "The time, styled below"},
        {"date", "calendar_month", "Date", "Day and date"},
        {"weather", "partly_cloudy_day", "Weather", "Temperature and sky now"},
        {"forecast", "date_range", "Forecast", "The next three days"},
        {"media", "music_note", "Media", "Now playing, with controls"},
        {"player", "album", "Music player", "The full player: cover, progress and controls"},
        {"cal-day", "today", "Day tile", "Today as a big tile"},
        {"cal-week", "view_week", "Week", "This week, today circled"},
        {"cal-month", "calendar_view_month", "Month", "This month as a grid"},
        {"cal-agenda", "event_upcoming", "Agenda", "Your next events"},
        {"music-disc", "album", "Music disc", "The cover in a wavy progress ring"},
        {"music-bar", "graphic_eq", "Music bar", "A slim player with wavy progress"},
        {"weather-big", "thermostat", "Big weather", "The temperature, large"},
        {"weather-card", "partly_cloudy_day", "Weather card", "Now and the next four days"},
        {"battery", "battery_full", "Battery", "Charge level"},
        {"notifications", "notifications", "Notifications", "How many are waiting"},
        {"screentime", "digital_wellbeing", "Screen time", "Today's time, by app"},
    };
    return all;
}

const Def* find(const std::string& id) {
    for (const auto& d : widgets())
        if (id == d.id) return &d;
    return nullptr;
}

std::vector<Item> load() {
    std::vector<Item> out;
    const auto& c = Config::get();
    const auto grid = c.value<nlohmann::json>("/lock/grid", nlohmann::json());
    if (grid.is_array()) {
        for (const auto& j : grid) {
            Item it{j.value("id", ""), std::clamp(j.value("col", 1), 0, kCols - 1), std::clamp(j.value("row", 0), 0, kRows - 1)};
            const bool taken = std::ranges::any_of(out, [&](const Item& o) { return o.col == it.col && o.row == it.row; });
            if (find(it.id) && !taken) out.push_back(it);
        }
        return out;
    }
    // v4.1.0: a clock, then a list of widgets under it, centred or left
    const int col = c.value<std::string>("/lock/clock/align", "center") == "left" ? 0 : 1;
    out.push_back({"clock", col, 0});
    auto ids = c.value<nlohmann::json>("/lock/widgets", nlohmann::json());
    if (!ids.is_array()) ids = {"date", "weather", "media"};
    int row = 1;
    for (const auto& id : ids)
        if (id.is_string() && find(id.get<std::string>()) && row < kRows) out.push_back({id.get<std::string>(), col, row++});
    return out;
}

void save(const std::vector<Item>& items) {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& it : items) j.push_back({{"id", it.id}, {"col", it.col}, {"row", it.row}});
    Config::get().set("/lock/grid", j);
}

void put(const std::string& id, int col, int row) {
    auto items = load();
    std::erase_if(items, [&](const Item& o) { return o.id == id || (o.col == col && o.row == row); });
    items.push_back({id, col, row});
    save(items);
}

void remove(int col, int row) {
    auto items = load();
    std::erase_if(items, [&](const Item& o) { return o.col == col && o.row == row; });
    save(items);
}

void move(int col, int row, int dcol, int drow) {
    const int nc = col + dcol, nr = row + drow;
    if (nc < 0 || nc >= kCols || nr < 0 || nr >= kRows) return;
    auto items = load();
    for (auto& o : items) {
        if (o.col == nc && o.row == nr) {
            o.col = col;
            o.row = row;
        } else if (o.col == col && o.row == row) {
            o.col = nc;
            o.row = nr;
        }
    }
    save(items);
}

const char* cellName(int col, int row) {
    static const char* names[kRows][kCols] = {{"top left", "top centre", "top right"},
                                              {"upper left", "upper centre", "upper right"},
                                              {"lower left", "lower centre", "lower right"},
                                              {"bottom left", "bottom centre", "bottom right"}};
    return names[std::clamp(row, 0, kRows - 1)][std::clamp(col, 0, kCols - 1)];
}

} // namespace m3::shell::lockgrid
